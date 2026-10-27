package org.studytracker;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.InterfaceAddress;
import java.net.NetworkInterface;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Base64;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

// Strona telefonu w synchronizacji z komputerem; ten sam protokół co w Sync.cpp.
// Działa i z otwartej aplikacji, i z pracy w tle (SyncJob), gdy aplikacja jest zamknięta.
public final class SyncClient {
    private static final int DEFAULT_PORT = 47815;
    private static final int FIND_TIMEOUT_MS = 1500;
    private static final int CONNECT_TIMEOUT_MS = 3000;
    private static final int TIMEOUT_MS = 15000;
    private static final String ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    private static final int CODE_LENGTH = 20;
    private static final byte[] PHONE = "phone".getBytes(StandardCharsets.UTF_8);
    private static final byte[] COMPUTER = "computer".getBytes(StandardCharsets.UTF_8);
    private static final SecureRandom random = new SecureRandom();

    // Naraz trwa jedna synchronizacja; prośba w jej trakcie powtarza ją po zakończeniu.
    private static final Object lock = new Object();
    private static boolean running;
    private static boolean again;
    // Gdy działa program Qt, to on zapisuje bazę: dwie kopie SQLite w jednym procesie
    // mogą sobie nawzajem zwolnić blokady pliku i uszkodzić bazę.
    private static volatile boolean qtAttached;

    private static native byte[] exportFromQt();
    private static native int mergeIntoQt(byte[] json);
    private static native void onSyncFinished(String error, boolean changed);

    private SyncClient() {}

    // ----- Ustawienia, wspólne dla programu Qt i pracy w tle.

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences("sync", Context.MODE_PRIVATE);
    }

    public static String getSetting(Context context, String key) {
        return prefs(context).getString(key, "");
    }

    public static void setSetting(Context context, String key, String value) {
        prefs(context).edit().putString(key, value).commit();
    }

    // ----- Wołane z programu Qt.

    // Czeka, aż praca w tle skończy ze swoją kopią SQLite, i od tej chwili oddaje bazę Qt.
    public static void attachQt(Context context, String databasePath) {
        setSetting(context, "db", databasePath);
        synchronized (lock) {
            while (running) {
                try {
                    lock.wait();
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
            qtAttached = true;
        }
        SyncJob.schedule(context);
    }

    public static void requestSync(Context context) {
        final Context app = context.getApplicationContext();
        new Thread(() -> runSync(app), "StudyTracker sync").start();
    }

    // ----- Właściwa synchronizacja.

    // Wraca dopiero po skończonej synchronizacji (albo od razu, gdy inna już trwa).
    static void runSync(Context context) {
        synchronized (lock) {
            if (running) {
                again = true;
                return;
            }
            running = true;
        }
        try {
            while (true) {
                final String[] result = syncOnce(context);
                if (qtAttached)
                    onSyncFinished(result[0], result[1] != null);
                synchronized (lock) {
                    if (!again) break;
                    again = false;
                }
            }
        } finally {
            synchronized (lock) {
                running = false;
                lock.notifyAll();
            }
        }
    }

    // Zwraca {błąd albo "", "changed" albo null}.
    private static String[] syncOnce(Context context) {
        final String code = normalizeCode(getSetting(context, "code"));
        if (code.isEmpty())
            return new String[] {"This phone is not paired with a computer yet.", null};
        int port = DEFAULT_PORT;
        try {
            final int p = Integer.parseInt(getSetting(context, "port"));
            if (p > 0 && p < 65536) port = p;
        } catch (NumberFormatException ignored) {
        }

        final byte[] key = sha256("StudyTracker sync key v2\n" + code);
        final String finderId = hex(sha256("StudyTracker discovery v2\n" + code), 8);

        // Najpierw komputer znaleziony w sieci, potem adres wpisany ręcznie i ostatnio znany.
        final String found = findComputer(finderId, port);
        if (found != null) setSetting(context, "found", found);
        final Set<String> hosts = new LinkedHashSet<>();
        for (String host : new String[] {found, getSetting(context, "host"),
                                         getSetting(context, "found")}) {
            if (host != null && !host.isEmpty()) hosts.add(host);
        }
        if (hosts.isEmpty()) {
            return new String[] {"Could not find the computer on this network. Make sure the "
                    + "program is running there and both devices use the same Wi-Fi.", null};
        }

        String lastError = null;
        for (String host : hosts) {
            try {
                final boolean changed = exchange(context, host, port, key);
                return new String[] {"", changed ? "changed" : null};
            } catch (FatalError e) {
                return new String[] {e.getMessage(), null};
            } catch (Exception e) {
                lastError = "Could not sync with " + host + ":\n" + e.getMessage();
            }
        }
        return new String[] {lastError, null};
    }

    // Błąd, którego nie naprawi próba pod innym adresem.
    private static final class FatalError extends Exception {
        private static final long serialVersionUID = 1L;

        FatalError(String message) {
            super(message);
        }
    }

    private static String findComputer(String finderId, int port) {
        try (DatagramSocket socket = new DatagramSocket()) {
            socket.setBroadcast(true);
            final byte[] question = (new JSONObject().put("find", finderId).toString() + "\n")
                    .getBytes(StandardCharsets.UTF_8);
            // Niektóre sieci nie przepuszczają adresu 255.255.255.255, więc też adres każdej
            // sieci, również hotspotu udostępnianego przez ten telefon.
            final Set<InetAddress> targets = new LinkedHashSet<>();
            targets.add(InetAddress.getByName("255.255.255.255"));
            final Enumeration<NetworkInterface> ifaces = NetworkInterface.getNetworkInterfaces();
            for (NetworkInterface iface : ifaces == null ? Collections.<NetworkInterface>emptyList()
                                                         : Collections.list(ifaces)) {
                if (!iface.isUp() || iface.isLoopback()) continue;
                for (InterfaceAddress address : iface.getInterfaceAddresses()) {
                    if (address.getBroadcast() != null) targets.add(address.getBroadcast());
                }
            }
            for (InetAddress target : targets) {
                try {
                    socket.send(new DatagramPacket(question, question.length, target, port));
                } catch (IOException ignored) {
                    // Ta sieć nie przyjmuje rozgłoszeń; zostają pozostałe.
                }
            }

            final long deadline = System.currentTimeMillis() + FIND_TIMEOUT_MS;
            final byte[] buffer = new byte[1024];
            while (true) {
                final long left = deadline - System.currentTimeMillis();
                if (left <= 0) return null;
                socket.setSoTimeout((int) left);
                final DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                socket.receive(packet);
                try {
                    final String text = new String(packet.getData(), 0, packet.getLength(),
                            StandardCharsets.UTF_8);
                    if (new JSONObject(text.trim()).optBoolean("here"))
                        return packet.getAddress().getHostAddress();
                } catch (JSONException ignored) {
                }
            }
        } catch (IOException | JSONException e) {
            // Także koniec czasu na odpowiedź (SocketTimeoutException).
            return null;
        }
    }

    private static boolean exchange(Context context, String host, int port, byte[] key)
            throws Exception {
        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(host, port), CONNECT_TIMEOUT_MS);
            socket.setSoTimeout(TIMEOUT_MS);

            final Database db = Database.open(context);
            if (db == null) throw new FatalError("Could not open the data on this phone.");
            try {
                final String challenge = hex(randomBytes(16), 16);
                final JSONObject request = new JSONObject()
                        .put("challenge", challenge)
                        .put("since", parseLong(getSetting(context, "since")))
                        .put("data", db.export());
                final OutputStream out = socket.getOutputStream();
                out.write(seal(key, PHONE, request).getBytes(StandardCharsets.UTF_8));
                out.flush();

                final String line = new BufferedReader(new InputStreamReader(
                        socket.getInputStream(), StandardCharsets.UTF_8)).readLine();
                if (line == null) throw new IOException("The computer closed the connection.");
                final JSONObject reply = unseal(key, COMPUTER, line);
                if (reply == null) {
                    if (isWrongCode(line))
                        throw new FatalError("The computer does not accept this pairing code.");
                    throw new IOException("Unexpected answer from the computer.");
                }
                if (!challenge.equals(reply.optString("answer")))
                    throw new IOException("Unexpected answer from the computer.");
                if (reply.has("error")) throw new FatalError(reply.optString("error"));
                final JSONObject data = reply.optJSONObject("data");
                if (data == null) throw new IOException("Unexpected answer from the computer.");

                final int result = db.merge(data);
                if (result == 0) throw new FatalError("Could not save the data.");
                setSetting(context, "since", Long.toString(reply.optLong("now")));
                return result == 2;
            } finally {
                db.close();
            }
        }
    }

    private static boolean isWrongCode(String line) {
        try {
            return "pairing".equals(new JSONObject(line.trim()).optString("error"));
        } catch (JSONException e) {
            return false;
        }
    }

    // ----- Baza danych: przez program Qt, gdy działa, a inaczej bezpośrednio.

    private interface Database {
        JSONObject export() throws Exception;
        // 0: błąd, 1: bez zmian, 2: dane się zmieniły.
        int merge(JSONObject snapshot) throws Exception;
        void close();

        static Database open(Context context) {
            if (qtAttached) return new QtDatabase();
            final String path = getSetting(context, "db");
            if (path.isEmpty()) return null;
            try {
                return new DirectDatabase(SQLiteDatabase.openDatabase(path, null,
                        SQLiteDatabase.OPEN_READWRITE | SQLiteDatabase.NO_LOCALIZED_COLLATORS));
            } catch (RuntimeException e) {
                return null;
            }
        }
    }

    private static final class QtDatabase implements Database {
        @Override
        public JSONObject export() throws Exception {
            final byte[] json = exportFromQt();
            if (json == null) throw new FatalError("The app is closing.");
            return new JSONObject(new String(json, StandardCharsets.UTF_8));
        }

        @Override
        public int merge(JSONObject snapshot) {
            return mergeIntoQt(snapshot.toString().getBytes(StandardCharsets.UTF_8));
        }

        @Override
        public void close() {}
    }

    // Odpowiednik exportSnapshot() i mergeFromComputer() z Database.cpp. Telefon przyjmuje
    // wartości komputera, bo komputer odsyła stan już rozstrzygnięty.
    private static final class DirectDatabase implements Database {
        private final SQLiteDatabase db;

        DirectDatabase(SQLiteDatabase db) {
            this.db = db;
            // Baza mogła jeszcze nie przejść migracji z Database.cpp, jeśli po aktualizacji
            // aplikacja nie była otwierana, a synchronizacja ruszyła w tle.
            addColumnIfMissing("sessions", "edit_ts", "INTEGER NOT NULL DEFAULT 0");
            addColumnIfMissing("todos", "deleted_ts", "INTEGER");
        }

        private void addColumnIfMissing(String table, String column, String definition) {
            try (Cursor c = db.rawQuery("PRAGMA table_info(" + table + ")", null)) {
                while (c.moveToNext())
                    if (column.equals(c.getString(1))) return;
            }
            db.execSQL("ALTER TABLE " + table + " ADD COLUMN " + column + " " + definition);
        }

        @Override
        public void close() {
            db.close();
        }

        @Override
        public JSONObject export() throws JSONException {
            final JSONArray subjects = new JSONArray();
            try (Cursor c = db.rawQuery(
                    "SELECT name, color, color_ts FROM subjects ORDER BY position, id", null)) {
                while (c.moveToNext()) {
                    subjects.put(new JSONObject()
                            .put("name", c.getString(0))
                            .put("color", c.getString(1))
                            .put("color_ts", c.getLong(2)));
                }
            }
            final JSONArray sessions = new JSONArray();
            try (Cursor c = db.rawQuery("SELECT se.uid, s.name, se.start_ts, se.end_ts, se.edit_ts "
                    + "FROM sessions se JOIN subjects s ON s.id = se.subject_id", null)) {
                while (c.moveToNext()) {
                    sessions.put(new JSONObject()
                            .put("uid", c.getString(0))
                            .put("subject", c.getString(1))
                            .put("start", c.getLong(2))
                            .put("end", c.getLong(3))
                            .put("edit_ts", c.getLong(4)));
                }
            }
            final JSONArray todos = new JSONArray();
            try (Cursor c = db.rawQuery("SELECT t.uid, t.text, t.created_ts, t.done_ts, s.name, "
                    + "t.subject_ts, t.deadline, t.deadline_ts, t.deleted_ts "
                    + "FROM todos t LEFT JOIN subjects s ON s.id = t.subject_id", null)) {
                while (c.moveToNext()) {
                    todos.put(new JSONObject()
                            .put("uid", c.getString(0))
                            .put("text", c.getString(1))
                            .put("created", c.getLong(2))
                            .put("done", c.isNull(3) ? JSONObject.NULL : c.getLong(3))
                            .put("subject", c.isNull(4) ? JSONObject.NULL : c.getString(4))
                            .put("subject_ts", c.getLong(5))
                            .put("deadline", c.isNull(6) ? JSONObject.NULL : c.getString(6))
                            .put("deadline_ts", c.getLong(7))
                            .put("deleted", c.isNull(8) ? JSONObject.NULL : c.getLong(8)));
                }
            }
            return new JSONObject().put("subjects", subjects).put("sessions", sessions)
                    .put("todos", todos);
        }

        @Override
        public int merge(JSONObject snapshot) throws JSONException {
            final String before = export().toString();
            db.beginTransaction();
            try {
                mergeRows(snapshot);
                db.setTransactionSuccessful();
            } catch (RuntimeException e) {
                return 0;
            } finally {
                db.endTransaction();
            }
            return export().toString().equals(before) ? 1 : 2;
        }

        private Long findId(String sql, String arg) {
            try (Cursor c = db.rawQuery(sql, new String[] {arg})) {
                return c.moveToNext() ? c.getLong(0) : null;
            }
        }

        private void mergeRows(JSONObject snapshot) throws JSONException {
            final JSONArray subjects = snapshot.optJSONArray("subjects");
            for (int i = 0; subjects != null && i < subjects.length(); ++i) {
                final JSONObject s = subjects.getJSONObject(i);
                final String name = s.optString("name").trim();
                final String color = s.optString("color").toLowerCase(Locale.ROOT);
                if (name.isEmpty() || !color.matches("#[0-9a-f]{6}")) continue;
                final long colorTs = s.optLong("color_ts");
                final Long id = findId("SELECT id FROM subjects WHERE name = ? COLLATE NOCASE", name);
                if (id == null) {
                    db.execSQL("INSERT INTO subjects(name, color, color_ts, position) "
                            + "SELECT ?, ?, ?, COALESCE(MAX(position), -1) + 1 FROM subjects",
                            new Object[] {name, color, colorTs});
                } else {
                    db.execSQL("UPDATE subjects SET color = ?, color_ts = ? WHERE id = ?",
                            new Object[] {color, colorTs, id});
                }
            }

            final Map<String, Long> subjectIds = new HashMap<>();
            try (Cursor c = db.rawQuery("SELECT name, id FROM subjects", null)) {
                while (c.moveToNext())
                    subjectIds.put(c.getString(0).toLowerCase(Locale.ROOT), c.getLong(1));
            }

            final JSONArray sessions = snapshot.optJSONArray("sessions");
            for (int i = 0; sessions != null && i < sessions.length(); ++i) {
                final JSONObject s = sessions.getJSONObject(i);
                final String uid = s.optString("uid");
                final Long subject = subjectIds.get(
                        s.optString("subject").trim().toLowerCase(Locale.ROOT));
                if (uid.isEmpty() || subject == null) continue;
                // Ręczna poprawka z komputera zastępuje cały odcinek; bez niej rośnie koniec.
                db.execSQL("INSERT INTO sessions(uid, subject_id, start_ts, end_ts, edit_ts) "
                        + "VALUES(?, ?, ?, ?, ?) ON CONFLICT(uid) DO UPDATE SET "
                        + "subject_id = CASE WHEN edit_ts = excluded.edit_ts THEN subject_id "
                        + "ELSE excluded.subject_id END, "
                        + "start_ts = CASE WHEN edit_ts = excluded.edit_ts THEN start_ts "
                        + "ELSE excluded.start_ts END, "
                        + "end_ts = CASE WHEN edit_ts = excluded.edit_ts "
                        + "THEN MAX(end_ts, excluded.end_ts) ELSE excluded.end_ts END, "
                        + "edit_ts = excluded.edit_ts",
                        new Object[] {uid, subject, s.optLong("start"), s.optLong("end"),
                                      s.optLong("edit_ts")});
            }

            final JSONArray todos = snapshot.optJSONArray("todos");
            for (int i = 0; todos != null && i < todos.length(); ++i) {
                final JSONObject t = todos.getJSONObject(i);
                final String uid = t.optString("uid");
                if (uid.isEmpty()) continue;
                final Long subject = t.isNull("subject") ? null : subjectIds.get(
                        t.optString("subject").trim().toLowerCase(Locale.ROOT));
                final long subjectTs = t.optLong("subject_ts");
                final String deadlineText = t.isNull("deadline") ? "" : t.optString("deadline");
                final String deadline =
                        deadlineText.matches("\\d{4}-\\d{2}-\\d{2}") ? deadlineText : null;
                final long deadlineTs = t.optLong("deadline_ts");
                Long done = t.isNull("done") ? null : t.optLong("done");
                // Starszy komputer nie wysyła tego pola wcale.
                Long deleted = !t.has("deleted") || t.isNull("deleted") ? null : t.optLong("deleted");

                Long id = null;
                Long localDone = null;
                Long localDeleted = null;
                try (Cursor c = db.rawQuery("SELECT id, done_ts, deleted_ts FROM todos WHERE uid = ?",
                        new String[] {uid})) {
                    if (c.moveToNext()) {
                        id = c.getLong(0);
                        localDone = c.isNull(1) ? null : c.getLong(1);
                        localDeleted = c.isNull(2) ? null : c.getLong(2);
                    }
                }
                if (id == null) {
                    db.execSQL("INSERT INTO todos(uid, text, created_ts, done_ts, subject_id, "
                            + "subject_ts, deadline, deadline_ts, deleted_ts) "
                            + "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)",
                            new Object[] {uid, t.optString("text"), t.optLong("created"), done,
                                          subject, subjectTs, deadline, deadlineTs, deleted});
                    continue;
                }
                // Zadanie odhaczone albo usunięte na którymkolwiek urządzeniu jest takie na obu.
                if (done == null || (localDone != null && localDone < done)) done = localDone;
                if (deleted == null || (localDeleted != null && localDeleted < deleted))
                    deleted = localDeleted;
                db.execSQL("UPDATE todos SET done_ts = ?, deleted_ts = ?, subject_id = ?, "
                        + "subject_ts = ?, deadline = ?, deadline_ts = ? WHERE id = ?",
                        new Object[] {done, deleted, subject, subjectTs, deadline, deadlineTs, id});
            }
        }
    }

    // ----- Kod parowania i szyfrowanie; te same wzory są w Sync.cpp.

    static String normalizeCode(String code) {
        final StringBuilder result = new StringBuilder();
        for (char c : code.toUpperCase(Locale.ROOT).toCharArray()) {
            if (c == '-' || Character.isWhitespace(c)) continue;
            if (c == 'O') c = '0';
            if (c == 'I' || c == 'L') c = '1';
            if (ALPHABET.indexOf(c) < 0) return "";
            result.append(c);
        }
        return result.length() == CODE_LENGTH ? result.toString() : "";
    }

    private static byte[] sha256(String text) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(text.getBytes(StandardCharsets.UTF_8));
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException(e);
        }
    }

    private static byte[] randomBytes(int count) {
        final byte[] bytes = new byte[count];
        random.nextBytes(bytes);
        return bytes;
    }

    private static String hex(byte[] bytes, int count) {
        final StringBuilder result = new StringBuilder();
        for (int i = 0; i < count; ++i) result.append(String.format("%02x", bytes[i] & 0xff));
        return result.toString();
    }

    private static long parseLong(String text) {
        try {
            return Long.parseLong(text);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    // Wiersz base64: 12 bajtów jednorazowej liczby, zaszyfrowany JSON i 16 bajtów podpisu.
    private static String seal(byte[] key, byte[] role, JSONObject message)
            throws GeneralSecurityException {
        final byte[] nonce = randomBytes(12);
        final Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(key, "AES"),
                new GCMParameterSpec(128, nonce));
        cipher.updateAAD(role);
        final byte[] sealed = cipher.doFinal(message.toString().getBytes(StandardCharsets.UTF_8));
        final byte[] all = new byte[nonce.length + sealed.length];
        System.arraycopy(nonce, 0, all, 0, nonce.length);
        System.arraycopy(sealed, 0, all, nonce.length, sealed.length);
        return Base64.getEncoder().encodeToString(all) + "\n";
    }

    private static JSONObject unseal(byte[] key, byte[] role, String line) {
        try {
            final byte[] all = Base64.getDecoder().decode(line.trim());
            if (all.length < 12 + 16) return null;
            final Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, new SecretKeySpec(key, "AES"),
                    new GCMParameterSpec(128, all, 0, 12));
            cipher.updateAAD(role);
            final byte[] plain = cipher.doFinal(all, 12, all.length - 12);
            return new JSONObject(new String(plain, StandardCharsets.UTF_8));
        } catch (IllegalArgumentException | GeneralSecurityException | JSONException e) {
            return null;
        }
    }
}
