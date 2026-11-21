#include "transfer.h"
#include "protocol.h"
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QStorageInfo>

TransferSession::TransferSession(QObject *parent) : QObject(parent) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish(false, QStringLiteral("连接或等待接收超时，请重试")); });
}

TransferSession::~TransferSession() {
    if (m_socket) { m_socket->disconnect(this); m_socket->abort(); }
}

void TransferSession::refreshTimeout() { m_timeout.start(60000); }

void TransferSession::wireSocket() {
    m_socket->setParent(this);
    m_socket->setReadBufferSize(256 * 1024);
    connect(m_socket, &QTcpSocket::readyRead, this, &TransferSession::processInput);
    connect(m_socket, &QTcpSocket::bytesWritten, this, [this] { if (m_state == State::Sending) pump(); });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        if (m_state != State::Done) finish(false, QStringLiteral("对方已断开连接，传输未完成"));
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        // A final message may still be buffered when the peer closes normally.
        if (error == QAbstractSocket::RemoteHostClosedError) { processInput(); return; }
        if (m_state != State::Done) finish(false, QStringLiteral("连接失败：") + m_socket->errorString());
    });
    refreshTimeout();
}

void TransferSession::sendMessage(const QJsonObject &message) {
    if (m_socket) m_socket->write(Protocol::frame(message));
}

void TransferSession::startSend(const QString &host, quint16 port, const QString &path, const QString &device) {
    m_source.setFileName(path);
    const QFileInfo info(path);
    m_name = info.fileName();
    m_size = info.size();
    if (!info.isFile() || !Protocol::validFileName(m_name) || m_size > Protocol::MaxFileSize || !m_source.open(QIODevice::ReadOnly)) {
        finish(false, QStringLiteral("无法读取文件，或文件名不受支持")); return;
    }
    m_socket = new QTcpSocket(this);
    wireSocket();
    connect(m_socket, &QTcpSocket::connected, this, [this, device] {
        m_state = State::Acceptance;
        sendMessage({{"type", "offer"}, {"protocol", "FastSend"}, {"version", 1},
                     {"name", m_name}, {"size", QString::number(m_size)}, {"device", device.left(64)}});
        emit statusChanged(QStringLiteral("等待对方确认接收…"));
    });
    emit statusChanged(QStringLiteral("正在连接对方…"));
    m_socket->connectToHost(host, port);
}

void TransferSession::startReceive(QTcpSocket *socket) {
    m_incoming = true;
    m_state = State::Offer;
    m_socket = socket;
    wireSocket();
    if (m_socket->bytesAvailable()) processInput();
}

void TransferSession::accept(const QString &directory) {
    if (m_state != State::Decision) return;
    QDir folder(directory);
    if (!folder.exists() && !folder.mkpath(".")) {
        sendMessage({{"type", "error"}, {"message", QStringLiteral("接收目录无法创建")}});
        finish(false, QStringLiteral("接收目录无法创建")); return;
    }
    const QStorageInfo storage(folder.absolutePath());
    if (storage.isValid() && storage.bytesAvailable() >= 0 && storage.bytesAvailable() < m_size) {
        sendMessage({{"type", "error"}, {"message", QStringLiteral("接收方磁盘空间不足")}});
        finish(false, QStringLiteral("磁盘空间不足")); return;
    }
    m_directory = folder.absolutePath();
    m_target = std::make_unique<QTemporaryFile>(folder.filePath(".fastsend-XXXXXX.part"));
    if (!m_target->open()) {
        sendMessage({{"type", "error"}, {"message", QStringLiteral("接收目录不可写")}});
        finish(false, QStringLiteral("无法写入接收目录")); return;
    }
    m_state = State::Body;
    refreshTimeout();
    sendMessage({{"type", "accept"}});
    emit statusChanged(QStringLiteral("正在接收文件…"));
    emit progress(0, m_size);
    processInput();
}

void TransferSession::reject() {
    if (m_state != State::Decision) return;
    sendMessage({{"type", "reject"}, {"message", QStringLiteral("对方拒绝了接收请求")}});
    finish(false, QStringLiteral("已拒绝接收"));
}

void TransferSession::cancel() {
    if (m_state == State::Done) return;
    finish(false, QStringLiteral("已取消传输"));
    if (m_socket) m_socket->abort();
}

void TransferSession::processInput() {
    if (!m_socket || m_state == State::Done) return;
    if (m_socket->bytesAvailable()) refreshTimeout();
    // Read only the expected body bytes; the following JSON digest stays framed.
    if (m_state == State::Body) {
        qint64 budget = 1024 * 1024;
        while (m_done < m_size && m_socket->bytesAvailable() && budget > 0) {
            const auto chunk = m_socket->read(qMin<qint64>(65536, m_size - m_done));
            if (chunk.isEmpty()) break;
            if (m_target->write(chunk) != chunk.size()) {
                sendMessage({{"type", "error"}, {"message", QStringLiteral("接收方写入文件失败")}});
                finish(false, QStringLiteral("写入文件失败，临时文件已清理")); return;
            }
            m_hash.addData(chunk);
            m_done += chunk.size();
            budget -= chunk.size();
        }
        emit progress(m_done, m_size);
        if (m_state == State::Done) return;
        if (m_done < m_size) {
            if (m_socket->bytesAvailable()) QTimer::singleShot(0, this, &TransferSession::processInput);
            return;
        }
        m_state = State::Digest;
    }
    if (m_state == State::Decision) {
        if (m_socket->bytesAvailable()) finish(false, QStringLiteral("尚未确认接收，对方提前发送了数据"));
        return;
    }
    if (m_state == State::Sending || m_state == State::Connecting) return;
    m_buffer += m_socket->readAll();
    QJsonObject message;
    QString error;
    if (!Protocol::takeFrame(m_buffer, message, error)) {
        if (!error.isEmpty()) finish(false, error);
        return;
    }
    const QString type = message.value("type").toString();
    if (type == "error" || type == "reject") {
        finish(false, message.value("message").toString(QStringLiteral("对方终止了传输")).left(200)); return;
    }
    if (m_state == State::Offer) {
        if (!Protocol::readOffer(message, m_name, m_size) || !m_buffer.isEmpty()) {
            sendMessage({{"type", "error"}, {"message", QStringLiteral("无效的文件传输请求")}});
            finish(false, QStringLiteral("无效的文件传输请求")); return;
        }
        m_state = State::Decision;
        emit offered(message.value("device").toString(QStringLiteral("附近设备")).left(64), m_name, m_size);
    } else if (m_state == State::Acceptance && type == "accept" && m_buffer.isEmpty()) {
        m_state = State::Sending;
        emit statusChanged(QStringLiteral("正在发送文件…"));
        pump();
    } else if (m_state == State::Digest) {
        completeReceive(message);
    } else if (m_state == State::Completion && type == "complete" &&
               message.value("sha256").toString().toLatin1() == m_hash.result().toHex()) {
        emit progress(m_size, m_size);
        finish(true, QStringLiteral("发送完成，对方已保存并校验文件"));
    } else {
        finish(false, QStringLiteral("收到不符合当前状态的协议消息"));
    }
}

void TransferSession::pump() {
    if (m_state != State::Sending) return;
    refreshTimeout();
    while (m_done < m_size && m_socket->bytesToWrite() < 256 * 1024) {
        const auto chunk = m_source.read(qMin<qint64>(65536, m_size - m_done));
        if (chunk.isEmpty()) { finish(false, QStringLiteral("读取文件失败，文件可能已被修改")); return; }
        if (m_socket->write(chunk) != chunk.size()) { finish(false, QStringLiteral("发送文件失败")); return; }
        m_hash.addData(chunk);
        m_done += chunk.size();
    }
    emit progress(qMax<qint64>(0, m_done - m_socket->bytesToWrite()), m_size);
    if (m_state == State::Done) return;
    if (m_done == m_size) {
        m_source.close();
        m_state = State::Completion;
        sendMessage({{"type", "finish"}, {"sha256", QString::fromLatin1(m_hash.result().toHex())}});
        emit statusChanged(QStringLiteral("正在传输并等待对方校验…"));
        processInput();
    }
}

void TransferSession::completeReceive(const QJsonObject &message) {
    if (message.value("type") != "finish" || !m_buffer.isEmpty() ||
        message.value("sha256").toString().toLatin1() != m_hash.result().toHex()) {
        sendMessage({{"type", "error"}, {"message", QStringLiteral("文件完整性校验失败")}});
        finish(false, QStringLiteral("文件校验失败，临时文件已清理")); return;
    }
    m_target->close();
    const QFileInfo original(m_name);
    const QString stem = original.completeBaseName();
    const QString extension = original.suffix().isEmpty() ? QString() : '.' + original.suffix();
    QString destination;
    bool renamed = false;
    for (int index = 0; index < 10000; ++index) {
        const QString name = index == 0 ? m_name : stem + QStringLiteral(" (%1)").arg(index) + extension;
        destination = QDir(m_directory).filePath(name);
        if (QFile::exists(destination)) continue;
        if (m_target->rename(destination)) { renamed = true; break; }
        if (!QFile::exists(destination)) break;
    }
    if (!renamed) {
        sendMessage({{"type", "error"}, {"message", QStringLiteral("接收方保存文件失败")}});
        finish(false, QStringLiteral("保存文件失败，临时文件已清理")); return;
    }
    m_target->setAutoRemove(false);
    sendMessage({{"type", "complete"}, {"sha256", QString::fromLatin1(m_hash.result().toHex())}});
    emit progress(m_size, m_size);
    finish(true, QStringLiteral("接收完成，文件校验通过"), destination);
}

void TransferSession::finish(bool success, const QString &message, const QString &savedPath) {
    if (m_state == State::Done) return;
    m_state = State::Done;
    m_timeout.stop();
    m_source.close();
    m_target.reset();
    if (m_socket) {
        // Keep the session alive until the final reply has actually drained.
        connect(m_socket, &QTcpSocket::disconnected, this, &QObject::deleteLater);
        m_socket->disconnectFromHost();
    }
    emit statusChanged(message);
    emit finished(success, message, savedPath);
    if (!m_socket || m_socket->state() == QAbstractSocket::UnconnectedState) deleteLater();
    else QTimer::singleShot(3000, this, &QObject::deleteLater);
}

TransferService::TransferService(QObject *parent) : QObject(parent) {
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (m_server.hasPendingConnections()) {
            auto *socket = m_server.nextPendingConnection();
            if (busy()) {
                socket->write(Protocol::frame({{"type", "error"}, {"message", QStringLiteral("对方正在传输文件，请稍后重试")}}));
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                socket->disconnectFromHost();
                QTimer::singleShot(3000, socket, &QObject::deleteLater);
                continue;
            }
            auto *session = new TransferSession(this);
            track(session);
            emit sessionStarted(session);
            session->startReceive(socket);
        }
    });
}

bool TransferService::listen(quint16 port) { return m_server.listen(QHostAddress::AnyIPv4, port); }

void TransferService::track(TransferSession *session) {
    m_active = session;
    connect(session, &TransferSession::finished, this, [this, session] {
        if (m_active == session) m_active = nullptr;
    });
}

TransferSession *TransferService::send(const QString &host, quint16 port, const QString &path, const QString &device) {
    if (busy()) return nullptr;
    auto *session = new TransferSession(this);
    track(session);
    emit sessionStarted(session);
    QTimer::singleShot(0, session, [session, host, port, path, device] { session->startSend(host, port, path, device); });
    return session;
}
