#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QDateTime>
#include <QMap>

struct Peer {
    QString id;
    QString name;
    QString address;
    quint16 port = 0;
    QDateTime lastSeen;
};

class Discovery : public QObject {
    Q_OBJECT
public:
    Discovery(const QString &id, const QString &name, quint16 port, QObject *parent = nullptr);
    bool start();
    void announce();
    void setName(const QString &name) { m_name = name; announce(); }
    QList<Peer> peers() const { return m_peers.values(); }
    static QStringList localAddresses();
signals:
    void peersChanged();
private:
    void receive();
    QString m_id;
    QString m_name;
    quint16 m_port;
    QUdpSocket m_socket;
    QTimer m_timer;
    QMap<QString, Peer> m_peers;
};
