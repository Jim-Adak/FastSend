#include "discovery.h"
#include "protocol.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QNetworkDatagram>

Discovery::Discovery(const QString &id, const QString &name, quint16 port, QObject *parent)
    : QObject(parent), m_id(id), m_name(name), m_port(port) {
    connect(&m_socket, &QUdpSocket::readyRead, this, &Discovery::receive);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        announce();
        bool changed = false;
        for (auto it = m_peers.begin(); it != m_peers.end();) {
            if (it->lastSeen.msecsTo(QDateTime::currentDateTimeUtc()) > 12000) { it = m_peers.erase(it); changed = true; }
            else ++it;
        }
        if (changed) emit peersChanged();
    });
}

bool Discovery::start() {
    if (!m_socket.bind(QHostAddress::AnyIPv4, Protocol::DiscoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) return false;
    m_timer.start(3000);
    announce();
    return true;
}

QStringList Discovery::localAddresses() {
    QStringList addresses;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) addresses.append(entry.ip().toString());
        }
    }
    addresses.removeDuplicates();
    return addresses;
}

void Discovery::announce() {
    const auto data = QJsonDocument(QJsonObject{{"app", "FastSend"}, {"version", 1},
        {"id", m_id}, {"name", m_name.left(64)}, {"port", int(m_port)}}).toJson(QJsonDocument::Compact);
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull())
                m_socket.writeDatagram(data, entry.broadcast(), Protocol::DiscoveryPort);
        }
    }
}

void Discovery::receive() {
    bool changed = false;
    while (m_socket.hasPendingDatagrams()) {
        const auto datagram = m_socket.receiveDatagram(2048);
        const auto object = QJsonDocument::fromJson(datagram.data()).object();
        const QString id = object.value("id").toString();
        const int port = object.value("port").toInt();
        if (object.value("app") != "FastSend" || object.value("version").toInt() != 1 ||
            id.isEmpty() || id == m_id || id.size() > 80 || port < 1 || port > 65535 ||
            datagram.senderAddress().protocol() != QAbstractSocket::IPv4Protocol) continue;
        if (!m_peers.contains(id) && m_peers.size() >= 128) continue;
        const Peer peer{id, object.value("name").toString(QStringLiteral("附近设备")).left(64),
                        datagram.senderAddress().toString(), quint16(port), QDateTime::currentDateTimeUtc()};
        changed = changed || !m_peers.contains(id) || m_peers[id].name != peer.name ||
                  m_peers[id].address != peer.address || m_peers[id].port != peer.port;
        m_peers[id] = peer;
    }
    if (changed) emit peersChanged();
}
