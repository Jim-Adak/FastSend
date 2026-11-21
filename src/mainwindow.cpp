#include "mainwindow.h"
#include "protocol.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QHostInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace {
QLabel *label(const QString &text, const char *style, QWidget *parent = nullptr) {
    auto *widget = new QLabel(text, parent);
    widget->setObjectName(style);
    widget->setWordWrap(true);
    widget->setTextFormat(Qt::PlainText);
    return widget;
}
QFrame *card() { auto *widget = new QFrame; widget->setObjectName("card"); return widget; }
}

MainWindow::MainWindow(quint16 port, const QString &nameOverride, const QString &directoryOverride) {
    m_device = nameOverride.isEmpty() ? m_settings.value("device", QHostInfo::localHostName()).toString() : nameOverride;
    const QString defaultFolder = QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).filePath("FastSend");
    m_directory = directoryOverride.isEmpty() ? m_settings.value("directory", defaultFolder).toString() : directoryOverride;
    QString id = m_settings.value("deviceId").toString();
    if (id.isEmpty()) { id = QUuid::createUuid().toString(QUuid::WithoutBraces); m_settings.setValue("deviceId", id); }
    // Different listener ports also allow two instances on one computer for testing.
    id += ':' + QString::number(port);
    connect(&m_service, &TransferService::sessionStarted, this, &MainWindow::attachSession);
    buildUi();
    if (!m_service.listen(port)) {
        m_status->setText(QStringLiteral("接收服务启动失败：") + m_service.errorString());
        m_send->setEnabled(false);
        m_addresses->setText(QStringLiteral("端口被占用或不可用，请关闭其他实例后重新打开。"));
        return;
    }
    m_discovery = new Discovery(id, m_device, m_service.port(), this);
    connect(m_discovery, &Discovery::peersChanged, this, &MainWindow::updatePeers);
    if (!m_discovery->start()) m_discoveryWarning = QStringLiteral("设备发现暂不可用，可以填写 IP 发送。");
    updateIdentity();
    updatePeers();
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("FastSend · V-1"));
    resize(1020, 760);
    setMinimumSize(900, 700);
    auto *root = new QWidget;
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setCentralWidget(root);

    auto *sidebar = new QFrame;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(210);
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(22, 30, 22, 24);
    side->setSpacing(10);
    side->addWidget(label(QStringLiteral("↗  FastSend"), "brand"));
    side->addWidget(label(QStringLiteral("文件，轻松到达。"), "muted"));
    side->addSpacing(34);
    auto *navGroup = new QButtonGroup(this);
    const QStringList navNames{QStringLiteral("↑    发送"), QStringLiteral("↓    接收")};
    for (int index = 0; index < navNames.size(); ++index) {
        auto *button = new QPushButton(navNames[index]);
        button->setObjectName("nav");
        button->setCheckable(true);
        button->setChecked(index == 0);
        navGroup->addButton(button, index);
        side->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, index] { m_pages->setCurrentIndex(index); });
    }
    side->addStretch();
    side->addWidget(label(QStringLiteral("此设备"), "muted"));
    m_identity = label(m_device, "deviceName");
    side->addWidget(m_identity);
    auto *rename = new QPushButton(QStringLiteral("修改名称"));
    rename->setObjectName("textButton");
    side->addWidget(rename);
    side->addSpacing(16);
    side->addWidget(label(QStringLiteral("V-1 · 局域网文件传输"), "small"));
    rootLayout->addWidget(sidebar);
    connect(rename, &QPushButton::clicked, this, [this] {
        bool accepted = false;
        const auto name = QInputDialog::getText(this, QStringLiteral("设备名称"), QStringLiteral("给这台设备起个名字"), QLineEdit::Normal, m_device, &accepted).trimmed();
        if (accepted && !name.isEmpty()) {
            m_device = name.left(64);
            m_settings.setValue("device", m_device);
            if (m_discovery) m_discovery->setName(m_device);
            updateIdentity();
        }
    });

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(34, 28, 34, 26);
    contentLayout->setSpacing(20);
    m_pages = new QStackedWidget;
    contentLayout->addWidget(m_pages, 1);
    rootLayout->addWidget(content, 1);

    auto *sendPage = new QWidget;
    auto *sendLayout = new QVBoxLayout(sendPage);
    sendLayout->setContentsMargins(0, 0, 0, 0);
    sendLayout->setSpacing(15);
    sendLayout->addWidget(label(QStringLiteral("发送文件"), "title"));
    sendLayout->addWidget(label(QStringLiteral("选择一个文件，发送给同一网络中的另一台设备。"), "muted"));
    auto *fileCard = card();
    auto *fileLayout = new QHBoxLayout(fileCard);
    fileLayout->setContentsMargins(24, 24, 24, 24);
    auto *fileText = new QVBoxLayout;
    m_fileLabel = label(QStringLiteral("准备发送什么？"), "section");
    m_fileDetails = label(QStringLiteral("图片、文档或其他文件，每次发送一个。"), "muted");
    fileText->addWidget(m_fileLabel);
    fileText->addWidget(m_fileDetails);
    fileLayout->addLayout(fileText, 1);
    m_choose = new QPushButton(QStringLiteral("＋  选择文件"));
    m_choose->setObjectName("primary");
    m_choose->setAccessibleName(QStringLiteral("选择文件"));
    fileLayout->addWidget(m_choose);
    connect(m_choose, &QPushButton::clicked, this, &MainWindow::selectFile);
    sendLayout->addWidget(fileCard);

    auto *nearbyRow = new QHBoxLayout;
    nearbyRow->addWidget(label(QStringLiteral("附近设备"), "section"));
    nearbyRow->addStretch();
    auto *refresh = new QPushButton(QStringLiteral("刷新"));
    refresh->setObjectName("textButton");
    nearbyRow->addWidget(refresh);
    sendLayout->addLayout(nearbyRow);
    m_peerHint = label(QStringLiteral("正在寻找附近的 FastSend…"), "muted");
    sendLayout->addWidget(m_peerHint);
    m_peers = new QListWidget;
    m_peers->setMinimumHeight(90);
    m_peers->setMaximumHeight(150);
    m_peers->setAccessibleName(QStringLiteral("附近设备列表"));
    sendLayout->addWidget(m_peers, 1);
    connect(refresh, &QPushButton::clicked, this, [this] { if (m_discovery) m_discovery->announce(); updatePeers(); });
    connect(m_peers, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        m_ip->setText(item->data(Qt::UserRole).toString());
        m_port->setValue(item->data(Qt::UserRole + 1).toInt());
    });
    sendLayout->addWidget(label(QStringLiteral("也可以直接填写对方在“接收”页显示的 IP 和端口。"), "small"));
    auto *addressRow = new QHBoxLayout;
    m_ip = new QLineEdit;
    m_ip->setPlaceholderText(QStringLiteral("对方 IP，例如 192.168.1.20"));
    m_ip->setAccessibleName(QStringLiteral("对方 IP"));
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(Protocol::DefaultPort);
    m_port->setPrefix(QStringLiteral("端口 "));
    m_port->setAccessibleName(QStringLiteral("对方端口"));
    m_port->setFixedWidth(142);
    m_send = new QPushButton(QStringLiteral("发送文件  →"));
    m_send->setObjectName("primary");
    m_send->setAccessibleName(QStringLiteral("发送文件"));
    addressRow->addWidget(m_ip, 1);
    addressRow->addWidget(m_port);
    addressRow->addWidget(m_send);
    sendLayout->addLayout(addressRow);
    connect(m_send, &QPushButton::clicked, this, [this] { sendFile(m_ip->text().trimmed(), quint16(m_port->value())); });
    m_pages->addWidget(sendPage);

    auto *receivePage = new QWidget;
    auto *receiveLayout = new QVBoxLayout(receivePage);
    receiveLayout->setContentsMargins(0, 0, 0, 0);
    receiveLayout->setSpacing(20);
    receiveLayout->addWidget(label(QStringLiteral("接收文件"), "title"));
    receiveLayout->addWidget(label(QStringLiteral("保持 FastSend 打开，收到文件前会询问你的意见。"), "muted"));
    auto *ready = card();
    auto *readyLayout = new QVBoxLayout(ready);
    readyLayout->setContentsMargins(26, 28, 26, 28);
    readyLayout->setSpacing(12);
    readyLayout->addWidget(label(QStringLiteral("↓  随时准备接收"), "section"));
    m_addresses = label(QStringLiteral("正在启动接收服务…"), "muted");
    m_addresses->setTextInteractionFlags(Qt::TextSelectableByMouse);
    readyLayout->addWidget(m_addresses);
    receiveLayout->addWidget(ready);
    auto *folder = card();
    auto *folderLayout = new QVBoxLayout(folder);
    folderLayout->setContentsMargins(24, 22, 24, 22);
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(label(QStringLiteral("文件保存到"), "section"));
    folderRow->addStretch();
    auto *change = new QPushButton(QStringLiteral("更改目录"));
    change->setObjectName("textButton");
    folderRow->addWidget(change);
    folderLayout->addLayout(folderRow);
    m_pathLabel = label(QDir::toNativeSeparators(m_directory), "muted");
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    folderLayout->addWidget(m_pathLabel);
    receiveLayout->addWidget(folder);
    connect(change, &QPushButton::clicked, this, &MainWindow::chooseDirectory);
    receiveLayout->addWidget(label(QStringLiteral("同名文件会自动改名；接收完成后检查文件完整性。"), "small"));
    receiveLayout->addStretch();
    m_pages->addWidget(receivePage);

    auto *transferCard = card();
    auto *transferLayout = new QVBoxLayout(transferCard);
    transferLayout->setContentsMargins(22, 16, 22, 18);
    transferLayout->setSpacing(10);
    auto *statusRow = new QHBoxLayout;
    m_status = label(QStringLiteral("一切就绪，开始分享吧。"), "section");
    statusRow->addWidget(m_status, 1);
    m_cancel = new QPushButton(QStringLiteral("取消传输"));
    m_cancel->setObjectName("textButton");
    m_cancel->setEnabled(false);
    statusRow->addWidget(m_cancel);
    transferLayout->addLayout(statusRow);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(7);
    transferLayout->addWidget(m_progress);
    auto *detailRow = new QHBoxLayout;
    m_progressDetail = label(QStringLiteral("两台设备连接同一个局域网即可使用。"), "small");
    detailRow->addWidget(m_progressDetail, 1);
    m_open = new QPushButton(QStringLiteral("打开文件夹"));
    m_open->setObjectName("textButton");
    m_open->setVisible(false);
    detailRow->addWidget(m_open);
    transferLayout->addLayout(detailRow);
    contentLayout->addWidget(transferCard);
    connect(m_cancel, &QPushButton::clicked, this, [this] { if (m_session) m_session->cancel(); });
    connect(m_open, &QPushButton::clicked, this, [this] {
        if (!m_lastSaved.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastSaved).absolutePath()));
    });
    setStyleSheet(QStringLiteral(R"(
        QWidget { font-family: 'Microsoft YaHei UI', 'Segoe UI'; font-size: 13px; color: #24352e; }
        QMainWindow, QWidget#central { background: #f7f9f7; }
        QFrame#sidebar { background: #eef3ef; border-right: 1px solid #e0e7e2; }
        QLabel { background: transparent; }
        QLabel#brand { font-size: 24px; font-weight: 700; color: #22784c; }
        QLabel#title { font-size: 29px; font-weight: 700; }
        QLabel#section { font-size: 15px; font-weight: 600; }
        QLabel#muted { color: #718078; line-height: 1.5; }
        QLabel#small { color: #829087; font-size: 11px; }
        QLabel#deviceName { font-weight: 600; font-size: 15px; }
        QFrame#card { background: white; border: 1px solid #e2e9e3; border-radius: 14px; }
        QPushButton { border: 1px solid #dce5de; border-radius: 9px; padding: 10px 16px; background: white; }
        QPushButton:hover { background: #edf5ef; }
        QPushButton:disabled { color: #9aa59f; background: #e9eeea; }
        QPushButton#primary { background: #278957; color: white; border: none; font-weight: 600; }
        QPushButton#primary:hover { background: #207448; }
        QPushButton#primary:disabled { background: #b6cdbf; }
        QPushButton#textButton { border: none; background: transparent; color: #278957; padding: 5px 6px; }
        QPushButton#textButton:hover { background: #e2eee6; }
        QPushButton#nav { border: none; background: transparent; text-align: left; padding: 14px 18px; font-size: 16px; }
        QPushButton#nav:checked { background: #dcece1; color: #207448; font-weight: 600; }
        QLineEdit, QSpinBox { background: white; border: 1px solid #dce5de; border-radius: 8px; padding: 10px; }
        QLineEdit:focus, QSpinBox:focus { border: 1px solid #278957; }
        QListWidget { background: white; border: 1px solid #e2e9e3; border-radius: 10px; padding: 5px; outline: none; }
        QListWidget::item { padding: 12px; border-radius: 7px; }
        QListWidget::item:selected { background: #e1f0e6; color: #20673f; }
        QListWidget::item:hover { background: #f0f6f2; }
        QProgressBar { border: none; background: #e9efeb; border-radius: 3px; }
        QProgressBar::chunk { background: #2e9a63; border-radius: 3px; }
        QMessageBox, QInputDialog { background: #f7f9f7; }
    )"));
    root->setObjectName("central");
}

void MainWindow::selectFile() {
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择要发送的文件"));
    if (path.isEmpty()) return;
    m_file = path;
    const QFileInfo file(path);
    m_fileLabel->setText(file.fileName());
    m_fileLabel->setToolTip(path);
    m_fileDetails->setText(Protocol::readableSize(file.size()) + QStringLiteral(" · 已选择，准备发送"));
}

void MainWindow::sendFile(const QString &host, quint16 port) {
    if (m_service.busy()) { QMessageBox::information(this, QStringLiteral("正在传输"), QStringLiteral("请等待当前任务完成。")); return; }
    if (m_file.isEmpty()) { selectFile(); if (m_file.isEmpty()) return; }
    const QHostAddress address(host);
    if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isNull() || address.isMulticast() || address == QHostAddress::Broadcast || address == QHostAddress::AnyIPv4) {
        QMessageBox::information(this, QStringLiteral("填写设备地址"), QStringLiteral("请选择附近设备，或填写对方接收页面显示的 IPv4 地址。")); return;
    }
    m_service.send(host, port, m_file, m_device);
}

void MainWindow::attachSession(TransferSession *session) {
    m_session = session;
    m_progress->setValue(0);
    m_send->setEnabled(false);
    m_choose->setEnabled(false);
    m_cancel->setEnabled(true);
    m_open->setVisible(false);
    m_status->setText(QStringLiteral("正在建立传输…"));
    connect(session, &TransferSession::statusChanged, m_status, &QLabel::setText);
    connect(session, &TransferSession::progress, this, [this](qint64 done, qint64 total) {
        const int value = total == 0 ? 0 : int((double(done) / double(total)) * 1000);
        m_progress->setValue(qBound(0, value, 1000));
        m_progressDetail->setText(Protocol::readableSize(done) + " / " + Protocol::readableSize(total));
    });
    connect(session, &TransferSession::offered, this, [this, session](const QString &device, const QString &name, qint64 size) {
        m_status->setText(QStringLiteral("有一个文件等待接收"));
        auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("接收文件"),
            device + QStringLiteral(" 想发送一个文件\n\n") + name + "\n" + Protocol::readableSize(size) +
            QStringLiteral("\n\n保存到：") + QDir::toNativeSeparators(m_directory), QMessageBox::Yes | QMessageBox::No, this);
        box->setTextFormat(Qt::PlainText);
        box->setDefaultButton(QMessageBox::No);
        box->button(QMessageBox::Yes)->setText(QStringLiteral("接收"));
        box->button(QMessageBox::No)->setText(QStringLiteral("拒绝"));
        box->setAttribute(Qt::WA_DeleteOnClose);
        m_request = box;
        const QString receiveDirectory = m_directory;
        connect(box, &QMessageBox::finished, session, [session, receiveDirectory](int result) {
            if (result == QMessageBox::Yes) session->accept(receiveDirectory); else session->reject();
        });
        box->open();
        QApplication::alert(this);
    });
    connect(session, &TransferSession::finished, this, [this](bool success, const QString &message, const QString &path) {
        if (m_request) { m_request->disconnect(); m_request->close(); }
        m_status->setText(message);
        m_progress->setValue(success ? 1000 : 0);
        m_progressDetail->setText(success ? QStringLiteral("文件已完整传输，并通过 SHA-256 校验。") : QStringLiteral("可以重新选择设备再试一次。"));
        m_cancel->setEnabled(false);
        m_send->setEnabled(true);
        m_choose->setEnabled(true);
        m_lastSaved = path;
        m_open->setVisible(!path.isEmpty());
        m_session = nullptr;
    });
}

void MainWindow::updatePeers() {
    const auto selectedId = m_peers->currentItem() ? m_peers->currentItem()->data(Qt::UserRole + 2).toString() : QString();
    m_peers->clear();
    if (m_discovery) {
        for (const auto &peer : m_discovery->peers()) {
            auto *item = new QListWidgetItem(peer.name + "    ·    " + peer.address + ':' + QString::number(peer.port), m_peers);
            item->setData(Qt::UserRole, peer.address);
            item->setData(Qt::UserRole + 1, peer.port);
            item->setData(Qt::UserRole + 2, peer.id);
            if (peer.id == selectedId) m_peers->setCurrentItem(item);
        }
    }
    m_peerHint->setText(!m_discoveryWarning.isEmpty() ? m_discoveryWarning :
        (m_peers->count() ? QStringLiteral("点击设备，再按“发送文件”。") : QStringLiteral("暂无附近设备。请在另一台电脑打开 FastSend，或直接填写 IP。")));
}

void MainWindow::chooseDirectory() {
    const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择接收目录"), m_directory);
    if (path.isEmpty()) return;
    m_directory = path;
    m_settings.setValue("directory", path);
    m_pathLabel->setText(QDir::toNativeSeparators(path));
}

void MainWindow::updateIdentity() {
    m_identity->setText(m_device);
    const auto addresses = Discovery::localAddresses();
    m_addresses->setText((addresses.isEmpty() ? QStringLiteral("未找到局域网 IPv4 地址") : addresses.join("\n")) +
        QStringLiteral("\n端口：%1").arg(m_service.port()));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_session) {
        const auto result = QMessageBox::question(this, QStringLiteral("退出 FastSend"),
            QStringLiteral("文件还在传输或等待确认，退出将取消任务。确定退出吗？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result != QMessageBox::Yes) { event->ignore(); return; }
        m_session->cancel();
    }
    event->accept();
}
