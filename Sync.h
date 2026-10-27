// Sync.h
#ifndef LEARNINGTIMEANALIZER_SYNC_H
#define LEARNINGTIMEANALIZER_SYNC_H

#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>

class QObject;

// Sync over a shared Wi-Fi network: the computer waits for a connection and the phone starts it,
// sends its database and gets back the computer's database after merging. Messages are
// encrypted with a key derived from the pairing code; the code itself never goes over the network.
// On Android the phone side is handled by SyncClient.java so it also works in the background.

// Sync settings. On Android Java stores them, because the background job reads them too.
QVariant syncSetting(const QString& key);
void setSyncSetting(const QString& key, const QVariant& value);

// This computer's pairing code, generated randomly on first use, in the form XXXX-XXXX-…
QString pairingCode();
// The code in comparable form (no dashes, uppercase); empty when the code is invalid.
QString normalizePairingCode(const QString& code);
// The code in display form, with a dash every four characters.
QString formatPairingCode(const QString& code);
// Addresses at which the phone can find this computer.
QStringList syncAddresses();

// On the computer: waits for phones. merged is called when data from the phone changed something.
// false when the port is in use.
bool startSyncServer(QObject* parent, std::function<void()> merged);

// On the phone: exchanges data with the computer. done gets an empty string after a successful sync,
// otherwise an error description; changed tells whether data from the computer changed anything.
void syncNow(QObject* parent, std::function<void(const QString& error, bool changed)> done);
// On Android: call before opening the database. Waits until the background job is done with it,
// and from then on the background job writes data through this program.
void prepareSync();
// On Android: changed is called when background sync changed something.
void onBackgroundSync(std::function<void()> changed);

#endif
