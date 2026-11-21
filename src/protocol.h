#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace Protocol {
constexpr quint16 DefaultPort = 45455;
constexpr quint16 DiscoveryPort = 45454;
constexpr qsizetype MaxHeader = 16384;
constexpr qint64 MaxFileSize = 1024LL * 1024 * 1024 * 1024;
QByteArray frame(const QJsonObject &object);
bool takeFrame(QByteArray &buffer, QJsonObject &object, QString &error);
bool validFileName(const QString &name);
bool readOffer(const QJsonObject &object, QString &name, qint64 &size);
QString readableSize(qint64 size);
}
