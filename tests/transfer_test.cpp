#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QTcpSocket>
#include "protocol.h"
#include "transfer.h"

class TransferTest : public QObject {
    Q_OBJECT
private slots:
    void fileNames() {
        QVERIFY(Protocol::validFileName(QStringLiteral("中文 文件.txt")));
        QVERIFY(!Protocol::validFileName("../escape.txt"));
        QVERIFY(!Protocol::validFileName("..\\escape.txt"));
        QVERIFY(!Protocol::validFileName("CON.txt"));
        QVERIFY(!Protocol::validFileName("test:stream"));
        QVERIFY(!Protocol::validFileName("file."));
    }

    void framing() {
        QByteArray bytes("{\"type\":");
        QJsonObject object;
        QString error;
        QVERIFY(!Protocol::takeFrame(bytes, object, error));
        QVERIFY(error.isEmpty());
        bytes += "\"accept\"}\nremainder";
        QVERIFY(Protocol::takeFrame(bytes, object, error));
        QCOMPARE(object.value("type").toString(), "accept");
        QCOMPARE(bytes, "remainder");
        bytes = QByteArray(Protocol::MaxHeader + 1, 'x');
        QVERIFY(!Protocol::takeFrame(bytes, object, error));
        QVERIFY(!error.isEmpty());
    }

    void transfer_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<QString>("filename");
        QTest::addColumn<bool>("duplicate");
        QTest::newRow("empty") << QByteArray() << QString("empty.txt") << false;
        QTest::newRow("unicode") << QStringLiteral("你好，FastSend！\n").toUtf8() << QStringLiteral("课设 文件.txt") << false;
        QByteArray binary(5 * 1024 * 1024 + 37, Qt::Uninitialized);
        for (qsizetype i = 0; i < binary.size(); ++i) binary[i] = char((i * 13) % 251);
        QTest::newRow("binary-5MiB") << binary << QString("binary.dat") << false;
        QTest::newRow("duplicate") << QByteArray("new contents") << QString("document.txt") << true;
    }

    void transfer() {
        QFETCH(QByteArray, contents);
        QFETCH(QString, filename);
        QFETCH(bool, duplicate);
        QTemporaryDir sourceDir, destination;
        QVERIFY(sourceDir.isValid()); QVERIFY(destination.isValid());
        const auto path = QDir(sourceDir.path()).filePath(filename);
        QFile input(path);
        QVERIFY(input.open(QIODevice::WriteOnly)); QCOMPARE(input.write(contents), contents.size()); input.close();
        if (duplicate) {
            QFile old(QDir(destination.path()).filePath(filename));
            QVERIFY(old.open(QIODevice::WriteOnly)); old.write("keep this");
        }
        TransferService receiver, sender;
        QVERIFY(receiver.listen(0));
        bool receiveOk = false;
        QString savedPath;
        connect(&receiver, &TransferService::sessionStarted, this, [&](TransferSession *session) {
            connect(session, &TransferSession::offered, session, [session, &destination] { session->accept(destination.path()); });
            connect(session, &TransferSession::finished, this, [&](bool ok, const QString &, const QString &saved) { receiveOk = ok; savedPath = saved; });
        });
        auto *session = sender.send("127.0.0.1", receiver.port(), path, "Test Sender");
        QVERIFY(session);
        QSignalSpy done(session, &TransferSession::finished);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1, 15000);
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QVERIFY(receiveOk);
        QFile received(savedPath); QVERIFY(received.open(QIODevice::ReadOnly));
        QCOMPARE(received.readAll(), contents);
        if (duplicate) {
            QVERIFY(savedPath.endsWith("document (1).txt"));
            QFile old(QDir(destination.path()).filePath(filename));
            QVERIFY(old.open(QIODevice::ReadOnly)); QCOMPARE(old.readAll(), "keep this");
        }
        QVERIFY(QDir(destination.path()).entryList({"*.part"}, QDir::Files | QDir::Hidden).isEmpty());
    }

    void rejectRequest() {
        QTemporaryDir folder;
        QFile file(folder.filePath("example.txt")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("hi"); file.close();
        TransferService receiver, sender; QVERIFY(receiver.listen(0));
        connect(&receiver, &TransferService::sessionStarted, this, [](TransferSession *session) {
            connect(session, &TransferSession::offered, session, &TransferSession::reject);
        });
        auto *session = sender.send("127.0.0.1", receiver.port(), file.fileName(), "Test");
        QSignalSpy done(session, &TransferSession::finished);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QVERIFY(!done.at(0).at(0).toBool());
        QVERIFY(done.at(0).at(1).toString().contains(QStringLiteral("拒绝")));
    }

    void corruptDigest() {
        QTemporaryDir folder;
        TransferService receiver; QVERIFY(receiver.listen(0));
        bool finished = false, successful = true;
        connect(&receiver, &TransferService::sessionStarted, this, [&](TransferSession *session) {
            connect(session, &TransferSession::offered, session, [session, &folder] { session->accept(folder.path()); });
            connect(session, &TransferSession::finished, this, [&](bool ok) { successful = ok; finished = true; });
        });
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, receiver.port());
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        socket.write(Protocol::frame({{"type", "offer"}, {"protocol", "FastSend"}, {"version", 1}, {"name", "bad.bin"}, {"size", "3"}}));
        QTRY_VERIFY(socket.bytesAvailable() > 0);
        QVERIFY(socket.readAll().contains("accept"));
        socket.write("abc" + Protocol::frame({{"type", "finish"}, {"sha256", "incorrect"}}));
        QTRY_VERIFY(finished);
        QVERIFY(!successful);
        QVERIFY(QDir(folder.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }

    void interruptedTransfer() {
        QTemporaryDir folder;
        TransferService receiver; QVERIFY(receiver.listen(0));
        bool finished = false;
        connect(&receiver, &TransferService::sessionStarted, this, [&](TransferSession *session) {
            connect(session, &TransferSession::offered, session, [session, &folder] { session->accept(folder.path()); });
            connect(session, &TransferSession::finished, this, [&](bool ok) { QVERIFY(!ok); finished = true; });
        });
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, receiver.port());
        QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);
        socket.write(Protocol::frame({{"type", "offer"}, {"protocol", "FastSend"}, {"version", 1}, {"name", "partial.bin"}, {"size", "1000000"}}));
        QTRY_VERIFY(socket.bytesAvailable() > 0);
        socket.readAll();
        socket.write(QByteArray(1024, 'x'));
        QTRY_VERIFY(socket.bytesToWrite() == 0);
        socket.abort();
        QTRY_VERIFY(finished);
        QVERIFY(QDir(folder.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }

    void cancelDuringReceive() {
        QTemporaryDir folder, inputFolder;
        QFile file(inputFolder.filePath("large.bin"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(8 * 1024 * 1024, 'c')); file.close();
        TransferService receiver, sender; QVERIFY(receiver.listen(0));
        bool cancelled = false;
        connect(&receiver, &TransferService::sessionStarted, this, [&](TransferSession *session) {
            connect(session, &TransferSession::offered, session, [session, &folder] { session->accept(folder.path()); });
            connect(session, &TransferSession::progress, session, [session, &cancelled](qint64 done) {
                if (done > 0 && !cancelled) { cancelled = true; session->cancel(); }
            });
        });
        auto *session = sender.send("127.0.0.1", receiver.port(), file.fileName(), "Test");
        QSignalSpy done(session, &TransferSession::finished);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
        QVERIFY(cancelled);
        QVERIFY(!done.at(0).at(0).toBool());
        QVERIFY(QDir(folder.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }

    void invalidOfferAndBusy() {
        TransferService receiver; QVERIFY(receiver.listen(0));
        QTcpSocket first, second;
        first.connectToHost(QHostAddress::LocalHost, receiver.port());
        QTRY_COMPARE(first.state(), QAbstractSocket::ConnectedState);
        QTRY_VERIFY(receiver.busy());
        second.connectToHost(QHostAddress::LocalHost, receiver.port());
        QTRY_VERIFY(second.bytesAvailable() > 0);
        QVERIFY(second.readAll().contains("error"));
        first.write(Protocol::frame({{"type", "offer"}, {"protocol", "FastSend"}, {"version", 1}, {"name", "../escape"}, {"size", "4"}}));
        QTRY_VERIFY(first.bytesAvailable() > 0);
        QVERIFY(first.readAll().contains("error"));
        QTRY_VERIFY(!receiver.busy());
    }
};

QTEST_GUILESS_MAIN(TransferTest)
#include "transfer_test.moc"
