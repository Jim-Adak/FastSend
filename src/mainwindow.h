#pragma once
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include "discovery.h"
#include "transfer.h"

class QLabel;
class QPushButton;
class QLineEdit;
class QSpinBox;
class QListWidget;
class QProgressBar;
class QStackedWidget;
class QMessageBox;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(quint16 port, const QString &nameOverride = {}, const QString &directoryOverride = {});
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void buildUi();
    void selectFile();
    void sendFile(const QString &host, quint16 port);
    void attachSession(TransferSession *session);
    void updatePeers();
    void chooseDirectory();
    void updateIdentity();
    QSettings m_settings;
    TransferService m_service;
    Discovery *m_discovery = nullptr;
    QPointer<TransferSession> m_session;
    QPointer<QMessageBox> m_request;
    QString m_device;
    QString m_directory;
    QString m_file;
    QString m_lastSaved;
    QString m_discoveryWarning;
    QStackedWidget *m_pages = nullptr;
    QLabel *m_identity = nullptr;
    QLabel *m_addresses = nullptr;
    QLabel *m_fileLabel = nullptr;
    QLabel *m_fileDetails = nullptr;
    QLabel *m_pathLabel = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_progressDetail = nullptr;
    QLabel *m_peerHint = nullptr;
    QProgressBar *m_progress = nullptr;
    QListWidget *m_peers = nullptr;
    QLineEdit *m_ip = nullptr;
    QSpinBox *m_port = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_choose = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_open = nullptr;
};
