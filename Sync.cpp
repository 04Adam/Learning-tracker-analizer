// Sync.cpp
#include "Sync.h"
#include "Database.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>

// Pairing code: 20 characters from Crockford's alphabet (no I, L, O or U, which are easy to confuse),
// i.e. 100 random bits. That is enough to make the key impossible to guess.
static const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
static const int kCodeLength = 20;

QString normalizePairingCode(const QString& code) {
    QString result;
    for (QChar c : code.toUpper()) {
        if (c == '-' || c.isSpace()) continue;
        // Typing mistakes that Crockford's alphabet forgives.
        if (c == 'O') c = '0';
        if (c == 'I' || c == 'L') c = '1';
        if (!QByteArray(kAlphabet).contains(c.toLatin1())) return {};
        result.append(c);
    }
    return result.size() == kCodeLength ? result : QString();
}

QString formatPairingCode(const QString& code) {
    const QString normalized = normalizePairingCode(code);
    QStringList groups;
    for (int i = 0; i < normalized.size(); i += 4)
        groups.append(normalized.mid(i, 4));
    return groups.join('-');
}

QStringList syncAddresses() {
    QStringList result;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        // Docker's internal networks are not visible from the phone.
        const QString name = iface.name();
        if (name.startsWith("docker") || name.startsWith("br-") || name.startsWith("veth"))
            continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                result.append(entry.ip().toString());
        }
    }
    return result;
}

#ifndef Q_OS_ANDROID

// ---------------------------------------------------------------------------------------------
// Computer (and the phone layout run on the computer with --mobile, for testing).

#include <QDateTime>
#include <QDebug>
#include <QNetworkDatagram>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <memory>
#include <optional>

static const quint16 kDefaultPort = 47815;
static const int kTimeoutMs = 15000;
// A computer on the same network responds within milliseconds.
static const int kFindTimeoutMs = 1500;
static const int kConnectTimeoutMs = 3000;
// The whole database is a few megabytes at most; a larger message is not from our app.
static const qint64 kMaxMessage = 64 * 1024 * 1024;

static QSettings settings() {
    return QSettings(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + "/sync.ini", QSettings::IniFormat);
}

QVariant syncSetting(const QString& key) {
    return settings().value(key);
}

void setSyncSetting(const QString& key, const QVariant& value) {
    settings().setValue(key, value);
}

// Hidden setting for testing when another instance is already running on the same computer.
static quint16 syncPort() {
    const int port = syncSetting("port").toInt();
    return port > 0 && port < 65536 ? quint16(port) : kDefaultPort;
}

QString pairingCode() {
    QString code = normalizePairingCode(syncSetting("pairing").toString());
    if (code.isEmpty()) {
        for (int i = 0; i < kCodeLength; ++i)
            code.append(kAlphabet[QRandomGenerator::system()->bounded(32)]);
        setSyncSetting("pairing", code);
    }
    return formatPairingCode(code);
}

// Encryption key and identifier for discovering the computer; the same formula is in SyncClient.java.
static QByteArray keyFor(const QString& normalizedCode) {
    return QCryptographicHash::hash("StudyTracker sync key v2\n" + normalizedCode.toUtf8(),
                                    QCryptographicHash::Sha256);
}

static QString finderIdFor(const QString& normalizedCode) {
    return QCryptographicHash::hash("StudyTracker discovery v2\n" + normalizedCode.toUtf8(),
                                    QCryptographicHash::Sha256).left(8).toHex();
}

static QString randomHex(int bytes) {
    QByteArray data(bytes, 0);
    RAND_bytes(reinterpret_cast<unsigned char*>(data.data()), bytes);
    return data.toHex();
}

// One message is one base64 line: a 12-byte nonce, the encrypted JSON
// and a 16-byte tag (AES-256-GCM). role says who sent the message, so a message reflected back
// to its sender cannot be mistaken for a reply.
static QByteArray seal(const QByteArray& key, const QByteArray& role, const QJsonObject& message) {
    const QByteArray plain = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QByteArray nonce(12, 0);
    RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()), nonce.size());

    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),
                                                                        EVP_CIPHER_CTX_free);
    QByteArray cipher(plain.size(), 0);
    QByteArray tag(16, 0);
    int length = 0;
    const auto u = [](const QByteArray& a) { return reinterpret_cast<const unsigned char*>(a.data()); };
    if (!ctx
        || !EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, u(key), u(nonce))
        || !EVP_EncryptUpdate(ctx.get(), nullptr, &length, u(role), int(role.size()))
        || !EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(cipher.data()), &length,
                              u(plain), int(plain.size()))
        || !EVP_EncryptFinal_ex(ctx.get(), nullptr, &length)
        || !EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()))
        return {};
    return (nonce + cipher + tag).toBase64() + '\n';
}

static std::optional<QJsonObject> unseal(const QByteArray& key, const QByteArray& role,
                                         const QByteArray& line) {
    const auto decoded = QByteArray::fromBase64Encoding(
        line.trimmed(), QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() < 12 + 16) return std::nullopt;
    const QByteArray& raw = decoded.decoded;
    const QByteArray nonce = raw.left(12);
    const QByteArray cipher = raw.mid(12, raw.size() - 12 - 16);
    QByteArray tag = raw.right(16);

    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),
                                                                        EVP_CIPHER_CTX_free);
    QByteArray plain(cipher.size(), 0);
    int length = 0;
    const auto u = [](const QByteArray& a) { return reinterpret_cast<const unsigned char*>(a.data()); };
    if (!ctx
        || !EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, u(key), u(nonce))
        || !EVP_DecryptUpdate(ctx.get(), nullptr, &length, u(role), int(role.size()))
        || !EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char*>(plain.data()), &length,
                              u(cipher), int(cipher.size()))
        || !EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, tag.data())
        // This is where a wrong key or a message tampered with in transit shows up.
        || EVP_DecryptFinal_ex(ctx.get(), nullptr, &length) <= 0)
        return std::nullopt;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(plain, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    return document.object();
}

static const QByteArray kPhone = "phone";
static const QByteArray kComputer = "computer";
// Unencrypted reply to a message that cannot be read with the computer's key.
static const QByteArray kWrongCode = "{\"error\":\"pairing\"}\n";

bool startSyncServer(QObject* parent, std::function<void()> merged) {
    const QString code = normalizePairingCode(pairingCode());
    const QByteArray key = keyFor(code);
    const quint16 port = syncPort();

    auto* server = new QTcpServer(parent);
    QObject::connect(server, &QTcpServer::newConnection, server, [server, merged, key] {
        while (QTcpSocket* socket = server->nextPendingConnection()) {
            socket->setReadBufferSize(kMaxMessage);
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(kTimeoutMs, socket, [socket] { socket->abort(); });
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, merged, key] {
                if (!socket->canReadLine()) return;
                QObject::disconnect(socket, &QTcpSocket::readyRead, socket, nullptr);

                const std::optional<QJsonObject> request = unseal(key, kPhone, socket->readLine());
                if (!request) {
                    socket->write(kWrongCode);
                    socket->disconnectFromHost();
                    return;
                }
                // The phone checks that the reply answers its request and is not a replay.
                QJsonObject reply{{"answer", (*request)["challenge"].toString()}};
                // The computer's time, which the phone sends back on the next sync.
                const qint64 now = QDateTime::currentSecsSinceEpoch();
                const MergeResult result = mergeFromPhone((*request)["data"].toObject(),
                                                          (*request)["since"].toInteger());
                if (result == MergeResult::Failed) {
                    reply["error"] = "The computer could not save the data.";
                } else {
                    reply["data"] = exportSnapshot();
                    reply["now"] = now;
                    if (result == MergeResult::Changed)
                        merged();
                }
                socket->write(seal(key, kComputer, reply));
                socket->disconnectFromHost();
            });
        }
    });
    if (!server->listen(QHostAddress::Any, port)) return false;

    // Address queries arrive over UDP on the same port number.
    auto* finder = new QUdpSocket(parent);
    const QString finderId = finderIdFor(code);
    QObject::connect(finder, &QUdpSocket::readyRead, finder, [finder, finderId] {
        while (finder->hasPendingDatagrams()) {
            const QNetworkDatagram datagram = finder->receiveDatagram(1024);
            const QJsonObject question = QJsonDocument::fromJson(datagram.data()).object();
            if (question["find"].toString() == finderId)
                finder->writeDatagram("{\"here\":true}\n", datagram.senderAddress(),
                                      datagram.senderPort());
        }
    });
    // Without this the phone can still enter the address manually.
    if (!finder->bind(QHostAddress::AnyIPv4, port))
        qWarning() << "Sync: cannot listen for phones looking for this computer.";
    return true;
}

// found gets the computer's address, or an empty string when nobody responded.
static void findComputer(QObject* parent, const QString& finderId, quint16 port,
                         std::function<void(const QString& host)> found) {
    auto* socket = new QUdpSocket(parent);
    const auto finish = [socket, found](const QString& host) {
        if (socket->property("finished").toBool()) return;
        socket->setProperty("finished", true);
        socket->close();
        socket->deleteLater();
        found(host);
    };

    QObject::connect(socket, &QUdpSocket::readyRead, socket, [socket, finish] {
        while (socket->hasPendingDatagrams()) {
            const QNetworkDatagram datagram = socket->receiveDatagram(1024);
            if (QJsonDocument::fromJson(datagram.data()).object()["here"].toBool()) {
                finish(QHostAddress(datagram.senderAddress().toIPv4Address()).toString());
                return;
            }
        }
    });
    QTimer::singleShot(kFindTimeoutMs, socket, [finish] { finish({}); });

    if (!socket->bind(QHostAddress::AnyIPv4, 0)) {
        QTimer::singleShot(0, socket, [finish] { finish({}); });
        return;
    }
    // Some networks block 255.255.255.255, so also each network's broadcast address.
    QList<QHostAddress> targets{QHostAddress::Broadcast};
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!iface.flags().testFlag(QNetworkInterface::IsRunning)
            || iface.flags().testFlag(QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol
                && !entry.broadcast().isNull() && !targets.contains(entry.broadcast()))
                targets.append(entry.broadcast());
        }
    }
    const QByteArray question = QJsonDocument(QJsonObject{{"find", finderId}})
                                    .toJson(QJsonDocument::Compact) + '\n';
    for (const QHostAddress& target : targets)
        socket->writeDatagram(question, target, port);
}

// fatal means an error that trying another address will not fix.
using ExchangeDone = std::function<void(const QString& error, bool fatal, bool changed)>;

static void exchangeWith(QObject* parent, const QString& host, quint16 port,
                         const QByteArray& key, ExchangeDone done) {
    auto* socket = new QTcpSocket(parent);
    socket->setReadBufferSize(kMaxMessage);
    const QString challenge = randomHex(16);

    // Dropping the connection reports another error, but there must be only one result.
    const auto finish = [socket, done](const QString& error, bool fatal = false,
                                       bool changed = false) {
        if (socket->property("finished").toBool()) return;
        socket->setProperty("finished", true);
        socket->abort();
        socket->deleteLater();
        done(error, fatal, changed);
    };

    QObject::connect(socket, &QTcpSocket::connected, socket, [socket, key, challenge] {
        socket->write(seal(key, kPhone, {{"challenge", challenge},
                                         {"since", syncSetting("since").toLongLong()},
                                         {"data", exportSnapshot()}}));
    });
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, finish, key, challenge] {
        if (!socket->canReadLine()) return;
        const QByteArray line = socket->readLine();
        const std::optional<QJsonObject> reply = unseal(key, kComputer, line);
        if (!reply) {
            if (QJsonDocument::fromJson(line).object()["error"].toString() == "pairing")
                finish("The computer does not accept this pairing code.", true);
            else
                finish("Unexpected answer from the computer.");
            return;
        }
        if ((*reply)["answer"].toString() != challenge)
            finish("Unexpected answer from the computer.");
        else if (reply->contains("error"))
            finish((*reply)["error"].toString(), true);
        else if (!(*reply)["data"].isObject())
            finish("Unexpected answer from the computer.");
        else {
            const MergeResult result = mergeFromComputer((*reply)["data"].toObject());
            if (result == MergeResult::Failed) {
                finish("Could not save the data.", true);
                return;
            }
            setSyncSetting("since", (*reply)["now"].toInteger());
            finish({}, false, result == MergeResult::Changed);
        }
    });
    QObject::connect(socket, &QTcpSocket::errorOccurred, socket,
                     [socket, finish] { finish(socket->errorString()); });
    QTimer::singleShot(kConnectTimeoutMs, socket, [socket, finish] {
        if (socket->state() != QAbstractSocket::ConnectedState)
            finish("The computer did not answer.");
    });
    QTimer::singleShot(kTimeoutMs, socket, [finish] { finish("The computer did not answer."); });

    socket->connectToHost(host, port);
}

// Tries addresses one after another until one responds.
static void exchangeWithAny(QObject* parent, QStringList hosts, quint16 port,
                            const QByteArray& key, const QString& lastError,
                            std::function<void(const QString&, bool)> done) {
    if (hosts.isEmpty()) {
        done(lastError.isEmpty() ? "Could not find the computer on this network. Make sure the "
                                   "program is running there and both devices use the same Wi-Fi."
                                 : lastError,
             false);
        return;
    }
    const QString host = hosts.takeFirst();
    exchangeWith(parent, host, port, key,
                 [=](const QString& error, bool fatal, bool changed) {
        if (error.isEmpty() || fatal)
            done(error, changed);
        else
            exchangeWithAny(parent, hosts, port, key,
                            QString("Could not sync with %1:\n%2").arg(host, error), done);
    });
}

void syncNow(QObject* parent, std::function<void(const QString& error, bool changed)> done) {
    const QString code = normalizePairingCode(syncSetting("code").toString());
    if (code.isEmpty()) {
        done("This phone is not paired with a computer yet.", false);
        return;
    }
    const QByteArray key = keyFor(code);
    const quint16 port = syncPort();
    // The address from the router can change, so first look for the computer on the network, then
    // try the manually entered address and the one where the computer was last seen.
    findComputer(parent, finderIdFor(code), port, [=](const QString& found) {
        if (!found.isEmpty())
            setSyncSetting("found", found);
        QStringList hosts;
        for (const QString& host : {found, syncSetting("host").toString(),
                                    syncSetting("found").toString()}) {
            if (!host.isEmpty() && !hosts.contains(host))
                hosts.append(host);
        }
        exchangeWithAny(parent, hosts, port, key, {}, done);
    });
}

void prepareSync() {}
void onBackgroundSync(std::function<void()>) {}

#else

// ---------------------------------------------------------------------------------------------
// Android phone: SyncClient.java handles networking and encryption, and this program only
// writes data while it is running, so two SQLite copies never work on the database at once.

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>
#include <QList>
#include <QSemaphore>

#include <atomic>
#include <memory>
#include <optional>

static const char* const kClient = "org/studytracker/SyncClient";
// False once the program is shutting down: the background job can no longer rely on its thread.
static std::atomic<bool> qtAlive{true};
static QList<std::function<void(const QString&, bool)>> waiting;
static std::function<void()> backgroundChanged;

static QJniObject androidContext() {
    return QNativeInterface::QAndroidApplication::context();
}

QVariant syncSetting(const QString& key) {
    const QString value = QJniObject::callStaticMethod<jstring>(
        kClient, "getSetting", "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        androidContext().object(), QJniObject::fromString(key).object()).toString();
    return value.isEmpty() ? QVariant() : QVariant(value);
}

void setSyncSetting(const QString& key, const QVariant& value) {
    QJniObject::callStaticMethod<void>(
        kClient, "setSetting", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
        androidContext().object(), QJniObject::fromString(key).object(),
        QJniObject::fromString(value.toString()).object());
}

QString pairingCode() {
    return {};
}

bool startSyncServer(QObject*, std::function<void()>) {
    return false;
}

// Database work must happen in the Qt thread that opened the database connection.
template <typename T>
static std::optional<T> onQtThread(std::function<T()> work) {
    struct State {
        QSemaphore done;
        std::optional<T> result;
    };
    auto state = std::make_shared<State>();
    if (!qtAlive || !qApp) return std::nullopt;
    QMetaObject::invokeMethod(qApp, [state, work] {
        if (qtAlive)
            state->result = work();
        state->done.release();
    }, Qt::QueuedConnection);
    if (!state->done.tryAcquire(1, 20000)) return std::nullopt;
    return state->result;
}

static jbyteArray toJava(JNIEnv* env, const QByteArray& data) {
    jbyteArray array = env->NewByteArray(jsize(data.size()));
    env->SetByteArrayRegion(array, 0, jsize(data.size()),
                            reinterpret_cast<const jbyte*>(data.constData()));
    return array;
}

static jbyteArray exportFromQt(JNIEnv* env, jclass) {
    const auto json = onQtThread<QByteArray>([] {
        return QJsonDocument(exportSnapshot()).toJson(QJsonDocument::Compact);
    });
    return json ? toJava(env, *json) : nullptr;
}

// 0: error, 1: no changes, 2: data changed.
static jint mergeIntoQt(JNIEnv* env, jclass, jbyteArray json) {
    QByteArray data(env->GetArrayLength(json), 0);
    env->GetByteArrayRegion(json, 0, jsize(data.size()), reinterpret_cast<jbyte*>(data.data()));
    const auto result = onQtThread<MergeResult>([data] {
        return mergeFromComputer(QJsonDocument::fromJson(data).object());
    });
    if (!result || *result == MergeResult::Failed) return 0;
    return *result == MergeResult::Changed ? 2 : 1;
}

static void onSyncFinished(JNIEnv*, jclass, jstring error, jboolean changed) {
    const QString message = QJniObject(error).toString();
    const bool dataChanged = changed;
    if (!qtAlive || !qApp) return;
    QMetaObject::invokeMethod(qApp, [message, dataChanged] {
        const auto callbacks = std::exchange(waiting, {});
        for (const auto& done : callbacks)
            done(message, dataChanged);
        if (callbacks.isEmpty() && dataChanged && backgroundChanged)
            backgroundChanged();
    }, Qt::QueuedConnection);
}

void prepareSync() {
    const JNINativeMethod methods[] = {
        {"exportFromQt", "()[B", reinterpret_cast<void*>(exportFromQt)},
        {"mergeIntoQt", "([B)I", reinterpret_cast<void*>(mergeIntoQt)},
        {"onSyncFinished", "(Ljava/lang/String;Z)V", reinterpret_cast<void*>(onSyncFinished)},
    };
    QJniEnvironment().registerNativeMethods(kClient, methods, 3);
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, [] { qtAlive = false; });
    QJniObject::callStaticMethod<void>(kClient, "attachQt",
                                       "(Landroid/content/Context;Ljava/lang/String;)V",
                                       androidContext().object(),
                                       QJniObject::fromString(databasePath()).object());
}

void onBackgroundSync(std::function<void()> changed) {
    backgroundChanged = std::move(changed);
}

void syncNow(QObject*, std::function<void(const QString& error, bool changed)> done) {
    waiting.append(std::move(done));
    QJniObject::callStaticMethod<void>(kClient, "requestSync", "(Landroid/content/Context;)V",
                                       androidContext().object());
}

#endif
