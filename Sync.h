// Sync.h
#ifndef LEARNINGTIMEANALIZER_SYNC_H
#define LEARNINGTIMEANALIZER_SYNC_H

#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>

class QObject;

// Synchronizacja przez wspólną sieć Wi-Fi: komputer czeka na połączenie, a telefon je zaczyna,
// wysyła swoją bazę i dostaje w odpowiedzi bazę komputera po scaleniu. Wiadomości są
// szyfrowane kluczem wyliczonym z kodu parowania; sam kod nigdy nie idzie przez sieć.
// Na Androidzie stronę telefonu obsługuje SyncClient.java, żeby działała też w tle.

// Ustawienia synchronizacji. Na Androidzie trzyma je Java, bo czyta je też praca w tle.
QVariant syncSetting(const QString& key);
void setSyncSetting(const QString& key, const QVariant& value);

// Kod parowania tego komputera, losowany przy pierwszym użyciu, w postaci XXXX-XXXX-…
QString pairingCode();
// Kod w postaci do porównań (bez kresek, wielkie litery); pusty, gdy kod jest błędny.
QString normalizePairingCode(const QString& code);
// Kod w postaci do wyświetlenia, z kreskami co cztery znaki.
QString formatPairingCode(const QString& code);
// Adresy, pod którymi telefon może znaleźć ten komputer.
QStringList syncAddresses();

// Na komputerze: czeka na telefony. merged jest wołane, gdy dane z telefonu coś zmieniły.
// false, gdy port jest zajęty.
bool startSyncServer(QObject* parent, std::function<void()> merged);

// Na telefonie: wymienia dane z komputerem. done dostaje pusty tekst po udanej synchronizacji,
// a inaczej opis błędu; changed mówi, czy dane z komputera coś zmieniły.
void syncNow(QObject* parent, std::function<void(const QString& error, bool changed)> done);
// Na Androidzie: woła się przed otwarciem bazy. Czeka, aż praca w tle skończy z nią pracować,
// i od tej chwili praca w tle zapisuje dane przez ten program.
void prepareSync();
// Na Androidzie: changed jest wołane, gdy synchronizacja w tle coś zmieniła.
void onBackgroundSync(std::function<void()> changed);

#endif
