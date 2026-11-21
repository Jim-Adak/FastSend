#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QTemporaryFile>
#include <QCryptographicHash>
#include <QTimer>
#include <memory>

class TransferSession : public QObject {
    Q_OBJECT
public:
    explicit TransferSession(QObject *parent = nullptr);
    ~TransferSession() override;
    void startSend(const QString &host, quint16 port, const QString &path, const QString &device);
    void startReceive(QTcpSocket *socket);
    void accept(const QString &directory);
    void reject();
    void cancel();
    QString fileName() const { return m_name; }
    qint64 size() const { return m_size; }
    bool incoming() const { return m_incoming; }
signals:
    void offered(const QString &device, const QString &name, qint64 size);
    void statusChanged(const QString &text);
    void progress(qint64 done, qint64 total);
    void finished(bool success, const QString &message, const QString &savedPath);
private:
    enum class State { Connecting, Offer, Decision, Acceptance, Sending, Body, Digest, Completion, Done };
    void wireSocket();
    void processInput();
    void pump();
    void completeReceive(const QJsonObject &message);
    void finish(bool success, const QString &message, const QString &savedPath = {});
    void sendMessage(const QJsonObject &message);
    void refreshTimeout();
    QTcpSocket *m_socket = nullptr;
    State m_state = State::Connecting;
    bool m_incoming = false;
    QByteArray m_buffer;
    QString m_name;
    QString m_directory;
    qint64 m_size = 0;
    qint64 m_done = 0;
    QFile m_source;
    std::unique_ptr<QTemporaryFile> m_target;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QTimer m_timeout;
};

class TransferService : public QObject {
    Q_OBJECT
public:
    explicit TransferService(QObject *parent = nullptr);
    bool listen(quint16 port);
    quint16 port() const { return m_server.serverPort(); }
    QString errorString() const { return m_server.errorString(); }
    bool busy() const { return m_active != nullptr; }
    TransferSession *send(const QString &host, quint16 port, const QString &path, const QString &device);
signals:
    void sessionStarted(TransferSession *session);
private:
    void track(TransferSession *session);
    QTcpServer m_server;
    TransferSession *m_active = nullptr;
};
