// Sync.cpp
#include "Sync.h"
#include "Database.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

static const quint16 kPort = 47815;
static const int kTimeoutMs = 15000;
// Cała baza to najwyżej kilka megabajtów; większa wiadomość nie jest od naszej aplikacji.
static const qint64 kMaxMessage = 64 * 1024 * 1024;

// Jedna wiadomość to jeden wiersz: zwarty JSON nie zawiera znaków nowej linii.
static QByteArray toLine(const QJsonObject& message) {
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

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

QString syncCode() {
    QString code = syncSetting("code").toString();
    if (code.isEmpty()) {
        code = QString::number(QRandomGenerator::system()->bounded(100000, 1000000));
        setSyncSetting("code", code);
    }
    return code;
}

QStringList syncAddresses() {
    QStringList result;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        // Wewnętrzne sieci Dockera nie są widoczne z telefonu.
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

bool startSyncServer(QObject* parent, std::function<void()> merged) {
    auto* server = new QTcpServer(parent);
    QObject::connect(server, &QTcpServer::newConnection, server, [server, merged] {
        while (QTcpSocket* socket = server->nextPendingConnection()) {
            socket->setReadBufferSize(kMaxMessage);
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(kTimeoutMs, socket, [socket] { socket->abort(); });
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, merged] {
                if (!socket->canReadLine()) return;
                QObject::disconnect(socket, &QTcpSocket::readyRead, socket, nullptr);

                const QJsonObject request = QJsonDocument::fromJson(socket->readLine()).object();
                QJsonObject reply;
                if (request["code"].toString() != syncCode()) {
                    reply = {{"error", "Wrong code."}};
                } else if (!mergeSnapshot(request["data"].toObject())) {
                    reply = {{"error", "The computer could not save the data."}};
                } else {
                    reply = {{"data", exportSnapshot()}};
                    merged();
                }
                socket->write(toLine(reply));
                socket->disconnectFromHost();
            });
        }
    });
    return server->listen(QHostAddress::Any, kPort);
}

void syncWithHost(QObject* parent, const QString& host, const QString& code,
                  std::function<void(const QString& error)> done) {
    auto* socket = new QTcpSocket(parent);
    socket->setReadBufferSize(kMaxMessage);

    // Zerwanie połączenia zgłasza kolejny błąd, a wynik ma być jeden.
    const auto finish = [socket, done](const QString& error) {
        if (socket->property("finished").toBool()) return;
        socket->setProperty("finished", true);
        socket->abort();
        socket->deleteLater();
        done(error);
    };

    QObject::connect(socket, &QTcpSocket::connected, socket, [socket, code] {
        socket->write(toLine({{"code", code}, {"data", exportSnapshot()}}));
    });
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, finish] {
        if (!socket->canReadLine()) return;
        const QJsonObject reply = QJsonDocument::fromJson(socket->readLine()).object();
        if (reply.contains("error"))
            finish(reply["error"].toString());
        else if (!reply["data"].isObject())
            finish("Unexpected answer from the computer.");
        else
            finish(mergeSnapshot(reply["data"].toObject()) ? QString()
                                                           : "Could not save the data.");
    });
    QObject::connect(socket, &QTcpSocket::errorOccurred, socket,
                     [socket, finish] { finish(socket->errorString()); });
    QTimer::singleShot(kTimeoutMs, socket, [finish] { finish("The computer did not answer."); });

    socket->connectToHost(host, kPort);
}
