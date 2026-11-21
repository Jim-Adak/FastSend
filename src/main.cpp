#include "mainwindow.h"
#include "protocol.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QIcon>
#include <QPainter>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("FastSend");
    QCoreApplication::setApplicationName("FastSend");
    QCoreApplication::setApplicationVersion("V-1");
    QApplication::setStyle("Fusion");
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    QPixmap icon(128, 128);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#278957"));
    painter.drawRoundedRect(4, 4, 120, 120, 30, 30);
    painter.setPen(QPen(Qt::white, 11, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(38, 89, 88, 39);
    painter.drawLine(49, 39, 88, 39);
    painter.drawLine(88, 39, 88, 78);
    painter.end();
    app.setWindowIcon(QIcon(icon));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("FastSend V-1 · 局域网文件发送与接收"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"port", "TCP listening port", "port", QString::number(Protocol::DefaultPort)});
    parser.addOption({"name", "Device name", "name"});
    parser.addOption({"receive-dir", "Receive directory", "directory"});
    parser.process(app);
    bool valid = false;
    const uint port = parser.value("port").toUInt(&valid);
    if (!valid || port < 1 || port > 65535) return 2;
    MainWindow window(quint16(port), parser.value("name"), parser.value("receive-dir"));
    window.show();
    return app.exec();
}
