// Sync.h
#ifndef LEARNINGTIMEANALIZER_SYNC_H
#define LEARNINGTIMEANALIZER_SYNC_H

#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>

class QObject;

// Synchronizacja po sieci domowej: komputer czeka na połączenie, a telefon je zaczyna,
// wysyła swoją bazę i dostaje w odpowiedzi bazę komputera. Obie strony scalają to, co dostały.

// Ustawienia synchronizacji leżą w pliku obok bazy danych.
QVariant syncSetting(const QString& key);
void setSyncSetting(const QString& key, const QVariant& value);

// Kod, który telefon musi podać komputerowi; losowany przy pierwszym użyciu.
QString syncCode();
// Adresy, pod którymi telefon może znaleźć ten komputer.
QStringList syncAddresses();

// merged jest wołane po każdym scaleniu danych z telefonu. false, gdy port jest zajęty.
bool startSyncServer(QObject* parent, std::function<void()> merged);
// done dostaje pusty tekst po udanej synchronizacji, a inaczej opis błędu.
void syncWithHost(QObject* parent, const QString& host, const QString& code,
                  std::function<void(const QString& error)> done);

#endif
