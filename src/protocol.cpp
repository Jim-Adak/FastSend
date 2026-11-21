#include "protocol.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>

QByteArray Protocol::frame(const QJsonObject &object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

bool Protocol::takeFrame(QByteArray &buffer, QJsonObject &object, QString &error) {
    const qsizetype newline = buffer.indexOf('\n');
    if (newline < 0) {
        if (buffer.size() > MaxHeader) error = QStringLiteral("协议消息过长");
        return false;
    }
    if (newline > MaxHeader) { error = QStringLiteral("协议消息过长"); return false; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(buffer.left(newline), &parseError);
    buffer.remove(0, newline + 1);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("无效的协议消息");
        return false;
    }
    object = document.object();
    return true;
}

bool Protocol::validFileName(const QString &name) {
    if (name.isEmpty() || name.size() > 240 || name == "." || name == ".." ||
        name.endsWith('.') || name.endsWith(' ')) return false;
    for (const QChar ch : name) {
        if (ch.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(ch)) return false;
    }
    const auto base = name.section('.', 0, 0).trimmed().toUpper();
    static const QRegularExpression reserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"));
    return !reserved.match(base).hasMatch();
}

bool Protocol::readOffer(const QJsonObject &object, QString &name, qint64 &size) {
    bool validSize = false;
    name = object.value("name").toString();
    size = object.value("size").toString().toLongLong(&validSize);
    return object.value("type") == "offer" && object.value("protocol") == "FastSend" &&
           object.value("version").toInt() == 1 && validFileName(name) &&
           validSize && size >= 0 && size <= MaxFileSize;
}

QString Protocol::readableSize(qint64 size) {
    static const QStringList units{"B", "KB", "MB", "GB", "TB"};
    double value = double(size);
    int index = 0;
    while (value >= 1024 && index < units.size() - 1) { value /= 1024; ++index; }
    return QString::number(value, 'f', index == 0 ? 0 : 1) + ' ' + units[index];
}
