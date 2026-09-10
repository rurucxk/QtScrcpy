// #include <QDesktopWidget>
#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <QtWidgets/QHBoxLayout>

#if defined(Q_OS_WIN32)
#include <Windows.h>
#endif

#include "config.h"
#include "adbprocess.h"
#include "../QtScrcpyCore/src/device/filehandler/filehandler.h"
#include "iconhelper.h"
#include "qyuvopenglwidget.h"
#include "toolform.h"
#include "mousetap/mousetap.h"
#include "ui_videoform.h"
#include "videoform.h"

#ifdef Q_OS_MACOS
#include "metalvideowindow.h"
#endif

namespace {
const int FILE_PANEL_DEFAULT_WIDTH = 320;
const int FILE_NAME_ROLE = Qt::UserRole;
const int FILE_DIRECTORY_ROLE = Qt::UserRole + 1;

QString shellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace('\'', "'\\''");
    return QString("'") + quoted + "'";
}
}

VideoForm::VideoForm(bool framelessWindow, bool skin, bool showToolbar, int decodeMode, QWidget *parent) : QWidget(parent), ui(new Ui::videoForm), m_skin(skin), m_decodeMode(decodeMode)
{
    ui->setupUi(this);
    m_flexResizeTimer.setSingleShot(true);
    m_flexResizeTimer.setInterval(300);
    connect(&m_flexResizeTimer, &QTimer::timeout, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (device && device->isFlexDisplay() && !m_pendingDisplaySize.isEmpty()) {
            device->resizeDisplay(m_pendingDisplaySize);
        }
    });
    initUI();
    installShortcut();
    updateShowSize(size());
    bool vertical = size().height() > size().width();
    this->show_toolbar = showToolbar;
    if (m_skin) {
        updateStyleSheet(vertical);
    }
    if (framelessWindow) {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    }
}

VideoForm::~VideoForm()
{
    delete ui;
}

bool VideoForm::isMetalMode() const
{
#ifdef Q_OS_MACOS
    return !m_metalWidget.isNull();
#else
    return false;
#endif
}

QWidget* VideoForm::videoWidget() const
{
#ifdef Q_OS_MACOS
    if (isMetalMode()) {
        return m_metalWidget.data();
    }
#endif
    return m_videoWidget.data();
}

void VideoForm::initUI()
{
    if (m_skin) {
        QPixmap phone;
        if (phone.load(":/res/phone.png")) {
            m_widthHeightRatio = 1.0f * phone.width() / phone.height();
        }

#ifndef Q_OS_MACOS
        // mac下去掉标题栏影响showfullscreen
        // 去掉标题栏
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        // 根据图片构造异形窗口
        setAttribute(Qt::WA_TranslucentBackground);
#endif
    }

    initFilePanel();

    m_apkStatus = new QLabel(ui->keepRatioWidget);
    m_apkStatus->setTextFormat(Qt::PlainText);
    m_apkStatus->setWordWrap(true);
    m_apkStatus->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_apkStatus->setStyleSheet("color: #ffffff; background: #252525; border: 1px solid #454545; "
                               "border-radius: 6px; padding: 8px 12px; font-size: 13px;");
    m_apkStatus->hide();
    ui->keepRatioWidget->installEventFilter(this);
    m_apkStatusTimer.setSingleShot(true);
    connect(&m_apkStatusTimer, &QTimer::timeout, m_apkStatus, &QLabel::hide);

#ifdef Q_OS_MACOS
    // Apple Silicon: 使用 VideoToolbox + Metal 渲染
    if (m_decodeMode == 1) {
        m_metalWidget = new MetalVideoWidget();
        ui->keepRatioWidget->setWidget(m_metalWidget);

        // FPS label 作为 Metal widget 的子控件
        m_fpsLabel = new QLabel(m_metalWidget);
    } else
#endif
    {
        // OpenGL 路径（原有逻辑）
        m_videoWidget = new QYUVOpenGLWidget();
        m_videoWidget->hide();
        ui->keepRatioWidget->setWidget(m_videoWidget);

        // FPS label 作为 OpenGL widget 的子控件
        m_fpsLabel = new QLabel(m_videoWidget);
    }

    ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);

    QFont ft;
    ft.setPointSize(15);
    ft.setWeight(QFont::Light);
    ft.setBold(true);
    m_fpsLabel->setFont(ft);
    m_fpsLabel->move(5, 15);
    m_fpsLabel->setMinimumWidth(100);
    m_fpsLabel->setStyleSheet(R"(QLabel {color: #00FF00;})");

    setMouseTracking(true);
    if (m_videoWidget) {
        m_videoWidget->setMouseTracking(true);
    }
    ui->keepRatioWidget->setMouseTracking(true);
}

void VideoForm::initFilePanel()
{
    ui->verticalLayout->removeWidget(ui->keepRatioWidget);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(ui->keepRatioWidget);

    m_filePanel = new QWidget(m_splitter);
    m_filePanel->setMinimumWidth(280);
    auto *panelLayout = new QVBoxLayout(m_filePanel);
    panelLayout->setContentsMargins(6, 6, 6, 6);
    panelLayout->setSpacing(6);

    m_filePathEdit = new QLineEdit("/sdcard", m_filePanel);
    m_filePathEdit->setPlaceholderText(tr("device path"));
    panelLayout->addWidget(m_filePathEdit);

    auto *toolLayout = new QHBoxLayout;
    toolLayout->setContentsMargins(0, 0, 0, 0);
    toolLayout->setSpacing(4);
    auto makeButton = [this, toolLayout](QChar icon, const QString &toolTip) {
        auto *button = new QPushButton(m_filePanel);
        button->setFixedSize(30, 30);
        button->setStyleSheet("padding: 0;");
        button->setToolTip(toolTip);
        button->installEventFilter(this);
        IconHelper::Instance()->SetIcon(button, icon, 14);
        toolLayout->addWidget(button);
        return button;
    };
    m_fileUpBtn = makeButton(QChar(0xf062), tr("parent directory"));
    m_fileRefreshBtn = makeButton(QChar(0xf021), tr("refresh"));
    m_fileSortBtn = makeButton(QChar(0xf15d), tr("sort descending"));
    m_fileSortBtn->setCheckable(true);
    m_fileUploadBtn = makeButton(QChar(0xf093), tr("upload file"));
    m_fileDownloadBtn = makeButton(QChar(0xf019), tr("download file"));
    m_fileMkdirBtn = makeButton(QChar(0xf07b), tr("new directory"));
    m_fileRemoveBtn = makeButton(QChar(0xf1f8), tr("delete"));
    toolLayout->addStretch();
    panelLayout->addLayout(toolLayout);

    m_fileToolTip = new QLabel(m_filePanel);
    m_fileToolTip->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_fileToolTip->setStyleSheet("color: #ffffff; background: #252525; border: 1px solid #3f3f3f; "
                                 "border-radius: 4px; padding: 5px 7px;");
    m_fileToolTip->hide();

    m_fileList = new QListWidget(m_filePanel);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setUniformItemSizes(true);
    panelLayout->addWidget(m_fileList, 1);

    m_fileStatus = new QLabel(m_filePanel);
    m_fileStatus->setWordWrap(true);
    m_fileStatus->setMinimumHeight(m_fileStatus->fontMetrics().height());
    panelLayout->addWidget(m_fileStatus);

    m_splitter->addWidget(m_filePanel);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setSizes(QList<int>() << width() << FILE_PANEL_DEFAULT_WIDTH);
    ui->verticalLayout->addWidget(m_splitter);

    m_showFilePanel = Config::getInstance().getUserBootConfig().showFilePanel;
    m_filePanel->setVisible(m_showFilePanel);

    m_fileAdb = new qsc::AdbProcess(this);
    connect(m_fileAdb, &qsc::AdbProcess::adbProcessResult, this,
            [this](qsc::AdbProcess::ADB_EXEC_RESULT result) { onFileAdbResult(static_cast<int>(result)); });
    connect(m_filePathEdit, &QLineEdit::returnPressed, this, [this]() { loadFilePath(m_filePathEdit->text()); });
    connect(m_fileUpBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentFilePath == "/") {
            return;
        }
        const int slash = m_currentFilePath.lastIndexOf('/');
        loadFilePath(slash <= 0 ? "/" : m_currentFilePath.left(slash));
    });
    connect(m_fileRefreshBtn, &QPushButton::clicked, this, &VideoForm::refreshFileList);
    connect(m_fileSortBtn, &QPushButton::toggled, this, [this](bool descending) {
        IconHelper::Instance()->SetIcon(m_fileSortBtn, QChar(descending ? 0xf15e : 0xf15d), 14);
        m_fileSortBtn->setToolTip(descending ? tr("sort ascending") : tr("sort descending"));
        sortFileList();
    });
    connect(m_fileUploadBtn, &QPushButton::clicked, this, &VideoForm::uploadFile);
    connect(m_fileDownloadBtn, &QPushButton::clicked, this, &VideoForm::downloadFile);
    connect(m_fileMkdirBtn, &QPushButton::clicked, this, &VideoForm::createDirectory);
    connect(m_fileRemoveBtn, &QPushButton::clicked, this, &VideoForm::removeFile);
    connect(m_fileList, &QListWidget::itemDoubleClicked, this, &VideoForm::openFile);
    connect(m_fileList, &QListWidget::itemSelectionChanged, this, [this]() { updateFileButtons(); });
    updateFileButtons();
}

bool VideoForm::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->keepRatioWidget && event->type() == QEvent::Resize
        && m_apkStatus && !m_apkStatus->isHidden()) {
        showApkStatus(m_apkStatus->text());
    }
    auto *button = qobject_cast<QPushButton *>(watched);
    if (button && event->type() == QEvent::Enter) {
        m_fileToolTip->setText(button->toolTip());
        m_fileToolTip->adjustSize();
        m_fileToolTip->move(button->mapTo(m_filePanel, QPoint(0, button->height() + 2)));
        m_fileToolTip->raise();
        m_fileToolTip->show();
    } else if (button && event->type() == QEvent::Leave) {
        m_fileToolTip->hide();
    } else if (button && event->type() == QEvent::ToolTip) {
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void VideoForm::sortFileList()
{
    const auto selected = m_fileList->selectedItems();
    m_fileList->sortItems(m_fileSortBtn->isChecked() ? Qt::DescendingOrder : Qt::AscendingOrder);
    auto *current = m_fileList->currentItem();
    QList<QListWidgetItem *> directories;
    QList<QListWidgetItem *> files;
    while (m_fileList->count() > 0) {
        auto *item = m_fileList->takeItem(0);
        (item->data(FILE_DIRECTORY_ROLE).toBool() ? directories : files).append(item);
    }
    for (auto *item : directories) {
        m_fileList->addItem(item);
    }
    for (auto *item : files) {
        m_fileList->addItem(item);
    }
    if (current) {
        m_fileList->setCurrentItem(current, QItemSelectionModel::NoUpdate);
    }
    for (auto *item : selected) {
        item->setSelected(true);
    }
}

QString VideoForm::normalizeRemotePath(const QString &path) const
{
    if (path.trimmed().isEmpty() || !path.startsWith('/') || path.contains('\\')) {
        return QString();
    }
    for (const QChar character : path) {
        if (character.unicode() < 0x20) {
            return QString();
        }
    }
    const QStringList parts = path.split('/', Qt::SkipEmptyParts);
    if (parts.contains(".") || parts.contains("..")) {
        return QString();
    }
    const QString cleanPath = QDir::cleanPath(path);
    return cleanPath.startsWith('/') ? cleanPath : QString();
}

bool VideoForm::isValidChildName(const QString &name) const
{
    if (name.trimmed().isEmpty() || name == "." || name == ".."
        || name.contains('/') || name.contains('\\')) {
        return false;
    }
    for (const QChar character : name) {
        if (character.unicode() < 0x20) {
            return false;
        }
    }
    return true;
}

QString VideoForm::remoteChildPath(const QString &name) const
{
    return m_currentFilePath == "/" ? "/" + name : m_currentFilePath + "/" + name;
}

int VideoForm::filePanelWidth() const
{
    if (!m_filePanel || m_filePanel->isHidden()) {
        return 0;
    }
    return qMax(m_filePanel->minimumWidth(), m_filePanel->width()) + m_splitter->handleWidth();
}

void VideoForm::setFilePanelVisible(bool visible, bool persist)
{
    if (!m_filePanel) {
        return;
    }

    const bool changed = m_showFilePanel != visible;
    const int panelWidth = visible ? FILE_PANEL_DEFAULT_WIDTH + m_splitter->handleWidth() : filePanelWidth();
    m_showFilePanel = visible;
    if (!isFullScreen()) {
        m_filePanel->setVisible(visible);
        if (changed) {
            resize(qMax(1, width() + (visible ? panelWidth : -panelWidth)), height());
            if (visible) {
                m_splitter->setSizes(QList<int>() << qMax(1, width() - panelWidth) << FILE_PANEL_DEFAULT_WIDTH);
                refreshFileList();
            }
        }
    }

    if (m_toolForm) {
        m_toolForm->setFilePanelVisible(visible);
        m_toolForm->move(pos().x() + geometry().width(), pos().y() + 30);
    }
    if (persist) {
        UserBootConfig config = Config::getInstance().getUserBootConfig();
        config.showFilePanel = visible;
        Config::getInstance().setUserBootConfig(config);
    }
}

void VideoForm::toggleFilePanel()
{
    setFilePanelVisible(!m_showFilePanel);
}

void VideoForm::loadFilePath(const QString &path)
{
    if (m_fileOperation != FO_NONE || m_serial.isEmpty()) {
        return;
    }
    const QString normalized = normalizeRemotePath(path);
    if (normalized.isEmpty()) {
        QMessageBox::warning(this, "QtScrcpy", tr("invalid device path"), QMessageBox::Ok);
        return;
    }

    m_pendingRemotePath = normalized;
    m_pendingLocalPath.clear();
    m_fileOperation = FO_LIST;
    setFileBusy(true, tr("loading..."));
    const QString command = QString("set -o pipefail && ls -1Ap -- %1 | base64").arg(shellQuote(normalized));
    m_fileAdb->execute(m_serial, QStringList() << "shell" << command);
}

void VideoForm::refreshFileList()
{
    loadFilePath(m_currentFilePath);
}

void VideoForm::uploadFile()
{
    if (m_fileOperation != FO_NONE) {
        return;
    }
    const QString localPath = QFileDialog::getOpenFileName(this, tr("upload file"));
    if (localPath.isEmpty()) {
        return;
    }
    const QString name = QFileInfo(localPath).fileName();
    if (!isValidChildName(name)) {
        QMessageBox::warning(this, "QtScrcpy", tr("invalid file name"), QMessageBox::Ok);
        return;
    }

    m_pendingLocalPath = localPath;
    m_pendingRemotePath = remoteChildPath(name);
    m_fileOperation = FO_PUSH;
    setFileBusy(true, tr("uploading..."));
    m_fileAdb->push(m_serial, localPath, m_pendingRemotePath);
}

void VideoForm::downloadFile()
{
    const auto selected = m_fileList->selectedItems();
    auto *item = selected.size() == 1 ? selected.first() : nullptr;
    if (!item || item->data(FILE_DIRECTORY_ROLE).toBool() || m_fileOperation != FO_NONE) {
        return;
    }
    const QString name = item->data(FILE_NAME_ROLE).toString();
    const QString localPath = QFileDialog::getSaveFileName(this, tr("download file"), QDir::home().filePath(name));
    if (localPath.isEmpty()) {
        return;
    }

    m_pendingRemotePath = remoteChildPath(name);
    m_pendingLocalPath = localPath;
    m_fileOperation = FO_PULL;
    setFileBusy(true, tr("downloading..."));
    m_fileAdb->execute(m_serial, QStringList() << "pull" << m_pendingRemotePath << localPath);
}

void VideoForm::openFile(QListWidgetItem *item)
{
    if (!item || m_fileOperation != FO_NONE) {
        return;
    }
    const QString name = item->data(FILE_NAME_ROLE).toString();
    if (item->data(FILE_DIRECTORY_ROLE).toBool()) {
        loadFilePath(remoteChildPath(name));
        return;
    }
    if (!m_openTempDir.isValid()) {
        QMessageBox::warning(this, "QtScrcpy", tr("cannot create temporary directory"), QMessageBox::Ok);
        return;
    }

    m_pendingRemotePath = remoteChildPath(name);
    m_pendingLocalPath = QDir(m_openTempDir.path()).filePath(
        QString::number(QDateTime::currentMSecsSinceEpoch()) + "_" + name);
    m_fileOperation = FO_OPEN;
    setFileBusy(true, tr("opening..."));
    m_fileAdb->execute(m_serial, QStringList() << "pull" << m_pendingRemotePath << m_pendingLocalPath);
}

void VideoForm::createDirectory()
{
    if (m_fileOperation != FO_NONE) {
        return;
    }
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("new directory"), tr("directory name"),
                                               QLineEdit::Normal, QString(), &accepted);
    if (!accepted) {
        return;
    }
    if (!isValidChildName(name)) {
        QMessageBox::warning(this, "QtScrcpy", tr("invalid directory name"), QMessageBox::Ok);
        return;
    }

    m_pendingRemotePath = remoteChildPath(name);
    m_fileOperation = FO_MKDIR;
    setFileBusy(true, tr("creating..."));
    m_fileAdb->execute(m_serial, QStringList() << "shell" << "mkdir" << "--" << shellQuote(m_pendingRemotePath));
}

void VideoForm::removeFile()
{
    const auto selected = m_fileList->selectedItems();
    if (selected.isEmpty() || m_fileOperation != FO_NONE) {
        return;
    }
    QStringList paths;
    QStringList commands;
    for (auto *item : selected) {
        const QString name = item->data(FILE_NAME_ROLE).toString();
        const QString remotePath = remoteChildPath(name);
        if (!isValidChildName(name) || remotePath == "/" || normalizeRemotePath(remotePath).isEmpty()) {
            return;
        }
        paths.append(remotePath);
        commands.append(QString("rm %1 -- %2")
                            .arg(item->data(FILE_DIRECTORY_ROLE).toBool() ? "-r" : "-f", shellQuote(remotePath)));
    }
    QMessageBox confirmation(QMessageBox::Question, tr("delete"),
        paths.size() == 1 ? tr("Delete %1?\nThis action cannot be undone.").arg(paths.first())
                         : tr("Delete %1 selected items in %2?\nThis action cannot be undone.")
                               .arg(paths.size()).arg(m_currentFilePath),
        QMessageBox::Yes | QMessageBox::No, this);
    confirmation.setDefaultButton(QMessageBox::No);
    if (paths.size() > 1) {
        confirmation.setDetailedText(paths.join('\n'));
    }
    if (confirmation.exec() != QMessageBox::Yes) {
        return;
    }

    m_pendingRemotePath = m_currentFilePath;
    m_fileOperation = FO_REMOVE;
    setFileBusy(true, tr("deleting..."));
    m_fileAdb->execute(m_serial, QStringList() << "shell" << commands.join(" && "));
}

void VideoForm::updateFileButtons()
{
    if (!m_fileList) {
        return;
    }
    const bool busy = m_fileOperation != FO_NONE;
    const auto selected = m_fileList->selectedItems();
    const bool fileSelected = selected.size() == 1 && !selected.first()->data(FILE_DIRECTORY_ROLE).toBool();
    m_fileUpBtn->setEnabled(!busy && m_currentFilePath != "/");
    m_fileRefreshBtn->setEnabled(!busy);
    m_fileSortBtn->setEnabled(!busy);
    m_fileUploadBtn->setEnabled(!busy);
    m_fileDownloadBtn->setEnabled(!busy && fileSelected);
    m_fileMkdirBtn->setEnabled(!busy);
    m_fileRemoveBtn->setEnabled(!busy && !selected.isEmpty());
}

void VideoForm::setFileBusy(bool busy, const QString &status)
{
    m_filePathEdit->setEnabled(!busy);
    m_fileList->setEnabled(!busy);
    m_fileStatus->setText(status);
    updateFileButtons();
}

void VideoForm::onFileAdbResult(int processResult)
{
    const auto result = static_cast<qsc::AdbProcess::ADB_EXEC_RESULT>(processResult);
    if (result == qsc::AdbProcess::AER_SUCCESS_START || m_fileOperation == FO_NONE) {
        return;
    }

    if (result != qsc::AdbProcess::AER_SUCCESS_EXEC) {
        QString error = m_fileAdb->getErrorOut().trimmed();
        if (error.isEmpty()) {
            error = result == qsc::AdbProcess::AER_ERROR_MISSING_BINARY
                ? tr("adb not found") : tr("ADB operation failed");
        }
        const bool refreshAfterFailure = m_fileOperation == FO_REMOVE;
        m_fileOperation = FO_NONE;
        setFileBusy(false, error);
        if (refreshAfterFailure) {
            refreshFileList();
        }
        QMessageBox::warning(this, "QtScrcpy", error, QMessageBox::Ok);
        return;
    }

    const FileOperation operation = m_fileOperation;
    const QString output = m_fileAdb->getStdOut();
    const QString localPath = m_pendingLocalPath;
    const QString remotePath = m_pendingRemotePath;
    m_fileOperation = FO_NONE;
    setFileBusy(false, tr("complete"));

    if (operation == FO_LIST) {
        m_currentFilePath = remotePath;
        m_filePathEdit->setText(remotePath);
        m_fileList->clear();
        const QStringList entries = QString::fromUtf8(QByteArray::fromBase64(output.toLatin1()))
                                        .split('\n', Qt::SkipEmptyParts);
        for (QString name : entries) {
            if (name.endsWith('\r')) {
                name.chop(1);
            }
            const bool directory = name.endsWith('/');
            if (directory) {
                name.chop(1);
            }
            if (!isValidChildName(name)) {
                continue;
            }
            auto *item = new QListWidgetItem(
                style()->standardIcon(directory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon), name, m_fileList);
            item->setData(FILE_NAME_ROLE, name);
            item->setData(FILE_DIRECTORY_ROLE, directory);
        }
        sortFileList();
        m_fileStatus->setText(tr("%1 items").arg(m_fileList->count()));
        updateFileButtons();
        return;
    }

    if (operation == FO_OPEN) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(localPath))) {
            QMessageBox::warning(this, "QtScrcpy", tr("cannot open downloaded file"), QMessageBox::Ok);
        }
        return;
    }

    if (operation == FO_PUSH || operation == FO_MKDIR || operation == FO_REMOVE) {
        refreshFileList();
    }
}

QRect VideoForm::getGrabCursorRect()
{
    QRect rc;
    QWidget *vw = videoWidget();
#if defined(Q_OS_WIN32)
    rc = QRect(ui->keepRatioWidget->mapToGlobal(vw->pos()), vw->size());
    // high dpi support
    rc.setTopLeft(rc.topLeft() * vw->devicePixelRatioF());
    rc.setBottomRight(rc.bottomRight() * vw->devicePixelRatioF());

    rc.setX(rc.x() + 10);
    rc.setY(rc.y() + 10);
    rc.setWidth(rc.width() - 20);
    rc.setHeight(rc.height() - 20);
#elif defined(Q_OS_MACOS)
    rc = vw->geometry();
    rc.setTopLeft(ui->keepRatioWidget->mapToGlobal(rc.topLeft()));
    rc.setBottomRight(ui->keepRatioWidget->mapToGlobal(rc.bottomRight()));

    rc.setX(rc.x() + 10);
    rc.setY(rc.y() + 10);
    rc.setWidth(rc.width() - 20);
    rc.setHeight(rc.height() - 20);
#elif defined(Q_OS_LINUX)
    rc = QRect(ui->keepRatioWidget->mapToGlobal(vw->pos()), vw->size());
    // high dpi support -- taken from the WIN32 section and untested
    rc.setTopLeft(rc.topLeft() * vw->devicePixelRatioF());
    rc.setBottomRight(rc.bottomRight() * vw->devicePixelRatioF());

    rc.setX(rc.x() + 10);
    rc.setY(rc.y() + 10);
    rc.setWidth(rc.width() - 20);
    rc.setHeight(rc.height() - 20);
#endif
    return rc;
}

const QSize &VideoForm::frameSize()
{
    return m_frameSize;
}

void VideoForm::resizeSquare()
{
    QRect screenRect = getScreenRect();
    if (screenRect.isEmpty()) {
        qWarning() << "getScreenRect is empty";
        return;
    }
    resize(screenRect.height() + filePanelWidth(), screenRect.height());
}

void VideoForm::removeBlackRect()
{
    QSize size = ui->keepRatioWidget->goodSize();
    size.rwidth() += filePanelWidth();
    resize(size);
}

void VideoForm::showFPS(bool show)
{
    if (!m_fpsLabel) {
        return;
    }
    m_fpsLabel->setVisible(show);
}

void VideoForm::updateRender(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV)
{
    if (isMetalMode()) {
        // Metal 路径不通过此方法渲染，使用 onFrameMetal
        return;
    }

    if (!m_videoWidget) {
        return;
    }

    if (m_videoWidget->isHidden()) {
        if (m_loadingWidget) {
            m_loadingWidget->close();
        }
        m_videoWidget->show();
    }

    if (!m_flexDisplay) {
        updateShowSize(QSize(width, height));
    } else {
        m_frameSize = QSize(width, height);
    }
    m_videoWidget->setFrameSize(QSize(width, height));
    m_videoWidget->updateTextures(dataY, dataU, dataV, linesizeY, linesizeU, linesizeV);
}

void VideoForm::setSerial(const QString &serial)
{
    m_serial = serial;
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    auto *fileHandler = device ? device->findChild<FileHandler *>() : nullptr;
    if (fileHandler) {
        connect(fileHandler, &FileHandler::fileHandlerResult, this,
                [this](FileHandler::FILE_HANDLER_RESULT result, bool isApk) {
            if (!isApk || m_apkPending == 0
                || (result != FileHandler::FAR_SUCCESS_EXEC && result != FileHandler::FAR_ERROR_EXEC)) {
                return;
            }
            --m_apkPending;
            if (result == FileHandler::FAR_SUCCESS_EXEC) {
                ++m_apkSucceeded;
            } else {
                ++m_apkFailed;
            }
            const int completed = m_apkSucceeded + m_apkFailed;
            if (m_apkPending > 0) {
                showApkStatus(tr("Installing APKs... %1/%2 completed")
                                  .arg(completed).arg(completed + m_apkPending));
                return;
            }
            QString message = completed == 1
                ? (m_apkFailed ? tr("APK installation failed.") : tr("APK installed successfully."))
                : tr("APK installation finished: %1 succeeded, %2 failed.")
                      .arg(m_apkSucceeded).arg(m_apkFailed);
            if (m_apkFailed) {
                message += "\n" + tr("See the main window log for failure details.");
            }
            if (!m_apkFailed) {
                showApkStatus(message);
                m_apkStatusTimer.start(3000);
                return;
            }
            m_apkStatus->hide();
            auto *notice = new QMessageBox(QMessageBox::NoIcon, tr("APK installation"),
                                           message, QMessageBox::Ok, this);
            notice->setAttribute(Qt::WA_DeleteOnClose);
            notice->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
            notice->setTextFormat(Qt::PlainText);
            notice->setStyleSheet("QMessageBox { background: #252525; border: 1px solid #454545; } "
                                  "QLabel { color: #ffffff; background: transparent; border: none; "
                                  "padding: 12px; font-size: 14px; } "
                                  "QPushButton { min-width: 72px; min-height: 30px; padding: 0 12px; }");
            notice->button(QMessageBox::Ok)->setText(tr("OK"));
            notice->open();
        });
    }
    m_flexDisplay = device && device->isFlexDisplay();
    if (m_flexDisplay) {
        ui->keepRatioWidget->setWidthHeightRatio(-1.0f);
    }
    if (m_showFilePanel) {
        refreshFileList();
    }
}

void VideoForm::showToolForm(bool show)
{
    if (!m_toolForm) {
        m_toolForm = new ToolForm(this, ToolForm::AP_OUTSIDE_RIGHT);
        m_toolForm->setSerial(m_serial);
        const bool top = windowFlags().testFlag(Qt::WindowStaysOnTopHint);
        m_toolForm->setWindowFlag(Qt::WindowStaysOnTopHint, top);
        m_toolForm->setWindowOnTop(top);
    }
    m_toolForm->setFilePanelVisible(m_showFilePanel);
    m_toolForm->move(pos().x() + geometry().width(), pos().y() + 30);
    m_toolForm->setVisible(show);
}

void VideoForm::moveCenter()
{
    QRect screenRect = getScreenRect();
    if (screenRect.isEmpty()) {
        qWarning() << "getScreenRect is empty";
        return;
    }
    // 窗口居中
    move(screenRect.center() - QRect(0, 0, size().width(), size().height()).center());
}

void VideoForm::installShortcut()
{
    QShortcut *shortcut = nullptr;

    // switchFullScreen
    shortcut = new QShortcut(QKeySequence("Ctrl+f"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        switchFullScreen();
    });

    // resizeSquare
    shortcut = new QShortcut(QKeySequence("Ctrl+g"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() { resizeSquare(); });

    // removeBlackRect
    shortcut = new QShortcut(QKeySequence("Ctrl+w"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() { removeBlackRect(); });

    // postGoHome
    shortcut = new QShortcut(QKeySequence("Ctrl+h"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        device->postGoHome();
    });

    // postGoBack
    shortcut = new QShortcut(QKeySequence("Ctrl+b"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        device->postGoBack();
    });

    // postAppSwitch
    shortcut = new QShortcut(QKeySequence("Ctrl+s"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postAppSwitch();
    });

    // postGoMenu
    shortcut = new QShortcut(QKeySequence("Ctrl+m"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        device->postGoMenu();
    });

    // postVolumeUp
    shortcut = new QShortcut(QKeySequence("Ctrl+up"), this);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postVolumeUp();
    });

    // postVolumeDown
    shortcut = new QShortcut(QKeySequence("Ctrl+down"), this);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postVolumeDown();
    });

    // postPower
    shortcut = new QShortcut(QKeySequence("Ctrl+p"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postPower();
    });

    shortcut = new QShortcut(QKeySequence("Ctrl+o"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->setDisplayPower(false);
    });

    // expandNotificationPanel
    shortcut = new QShortcut(QKeySequence("Ctrl+n"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->expandNotificationPanel();
    });

    shortcut = new QShortcut(QKeySequence("Ctrl+Alt+n"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (device) {
            device->expandSettingsPanel();
        }
    });

    shortcut = new QShortcut(QKeySequence("Ctrl+r"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (device) {
            device->rotateDevice();
        }
    });

    // collapsePanel
    shortcut = new QShortcut(QKeySequence("Ctrl+Shift+n"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->collapsePanel();
    });

    // copy
    shortcut = new QShortcut(QKeySequence("Ctrl+c"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postCopy();
    });

    // cut
    shortcut = new QShortcut(QKeySequence("Ctrl+x"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->postCut();
    });

    // clipboardPaste
    shortcut = new QShortcut(QKeySequence("Ctrl+v"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->setDeviceClipboard();
    });

    // setDeviceClipboard
    shortcut = new QShortcut(QKeySequence("Ctrl+Shift+v"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
        if (!device) {
            return;
        }
        emit device->clipboardPaste();
    });
}

QRect VideoForm::getScreenRect()
{
    QRect screenRect;
    QScreen *screen = QGuiApplication::primaryScreen();
    QWidget *win = window();
    if (win) {
        QWindow *winHandle = win->windowHandle();
        if (winHandle) {
            screen = winHandle->screen();
        }
    }

    if (screen) {
        screenRect = screen->availableGeometry();
    }
    return screenRect;
}

void VideoForm::updateStyleSheet(bool vertical)
{
    if (vertical) {
        setStyleSheet(R"(
                 #videoForm {
                     border-image: url(:/image/videoform/phone-v.png) 150px 65px 85px 65px;
                     border-width: 150px 65px 85px 65px;
                 }
                 )");
    } else {
        setStyleSheet(R"(
                 #videoForm {
                     border-image: url(:/image/videoform/phone-h.png) 65px 85px 65px 150px;
                     border-width: 65px 85px 65px 150px;
                 }
                 )");
    }
    layout()->setContentsMargins(getMargins(vertical));
}

QMargins VideoForm::getMargins(bool vertical)
{
    QMargins margins;
    if (vertical) {
        margins = QMargins(10, 68, 12, 62);
    } else {
        margins = QMargins(68, 12, 62, 10);
    }
    return margins;
}

void VideoForm::updateShowSize(const QSize &newSize)
{
    if (m_frameSize != newSize) {
        m_frameSize = newSize;

        m_widthHeightRatio = 1.0f * newSize.width() / newSize.height();
        ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);

        bool vertical = m_widthHeightRatio < 1.0f ? true : false;
        QSize showSize = newSize;
        QRect screenRect = getScreenRect();
        if (screenRect.isEmpty()) {
            qWarning() << "getScreenRect is empty";
            return;
        }
        if (vertical) {
            showSize.setHeight(qMin(newSize.height(), screenRect.height() - 200));
            showSize.setWidth(showSize.height() * m_widthHeightRatio);
        } else {
            showSize.setWidth(qMin(newSize.width(), screenRect.width() / 2));
            showSize.setHeight(showSize.width() / m_widthHeightRatio);
        }

        if (isFullScreen() && qsc::IDeviceManage::getInstance().getDevice(m_serial)) {
            switchFullScreen();
        }

        if (isMaximized()) {
            showNormal();
        }

        if (m_skin) {
            QMargins m = getMargins(vertical);
            showSize.setWidth(showSize.width() + m.left() + m.right());
            showSize.setHeight(showSize.height() + m.top() + m.bottom());
        }
        showSize.rwidth() += filePanelWidth();

        if (showSize != size()) {
            resize(showSize);
            if (m_skin) {
                updateStyleSheet(vertical);
            }
            moveCenter();
        }
    }
}

void VideoForm::onVideoSessionChanged(const QSize &size, bool clientResized)
{
    if (m_flexDisplay) {
        m_frameSize = size;
        m_preventAutoResize = clientResized;
        ui->keepRatioWidget->setWidthHeightRatio(-1.0f);
        return;
    }
    // clientResized is only meaningful for flex display. Normal display
    // rotations must retain the longstanding auto-resize behavior.
    m_preventAutoResize = false;
    updateShowSize(size);
}

void VideoForm::switchFullScreen()
{
    if (isFullScreen()) {
        // 横屏全屏铺满全屏，恢复时，恢复保持宽高比
        if (m_widthHeightRatio > 1.0f) {
            ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);
        }

        showNormal();
        m_filePanel->setVisible(m_showFilePanel);
        // back to normal size.
        resize(m_normalSize);
        // fullscreen window will move (0,0). qt bug?
        move(m_fullScreenBeforePos);

#ifdef Q_OS_MACOS
        //setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        //show();
#endif
        if (m_skin) {
            updateStyleSheet(m_frameSize.height() > m_frameSize.width());
        }
        showToolForm(this->show_toolbar);
#ifdef Q_OS_WIN32
        ::SetThreadExecutionState(ES_CONTINUOUS);
#endif
    } else {
        // 横屏全屏铺满全屏，不保持宽高比
        if (m_widthHeightRatio > 1.0f) {
            ui->keepRatioWidget->setWidthHeightRatio(-1.0f);
        }

        // record current size before fullscreen, it will be used to rollback size after exit fullscreen.
        m_normalSize = size();
        m_filePanel->hide();

        m_fullScreenBeforePos = pos();
        // 这种临时增加标题栏再全屏的方案会导致收不到mousemove事件，导致setmousetrack失效
        // mac fullscreen must show title bar
#ifdef Q_OS_MACOS
        //setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
#endif
        showToolForm(false);
        if (m_skin) {
            layout()->setContentsMargins(0, 0, 0, 0);
        }
        showFullScreen();

        // 全屏状态禁止电脑休眠、息屏
#ifdef Q_OS_WIN32
        ::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
    }
}

bool VideoForm::isHost()
{
    if (!m_toolForm) {
        return false;
    }
    return m_toolForm->isHost();
}

void VideoForm::updateFPS(quint32 fps)
{
    if (!m_fpsLabel) {
        return;
    }
    m_fpsLabel->setText(QString("FPS:%1").arg(fps));
}

void VideoForm::grabCursor(bool grab)
{
    QRect rc = getGrabCursorRect();
    MouseTap::getInstance()->enableMouseEventTap(rc, grab);
}

void VideoForm::onFrame(int width, int height, uint8_t *dataY, uint8_t *dataU, uint8_t *dataV, int linesizeY, int linesizeU, int linesizeV)
{
    updateRender(width, height, dataY, dataU, dataV, linesizeY, linesizeU, linesizeV);
}

void VideoForm::onFrameMetal(void *cvPixelBuffer, int width, int height)
{
#ifdef Q_OS_MACOS
    if (!m_metalWidget || !cvPixelBuffer) {
        return;
    }

    if (m_metalFirstFrame) {
        m_metalFirstFrame = false;
        if (m_loadingWidget) {
            m_loadingWidget->close();
        }
        ui->keepRatioWidget->updateGeometry();
    }

    updateShowSize(QSize(width, height));
    m_metalWidget->renderFrame((CVPixelBufferRef)cvPixelBuffer, width, height);
#else
    Q_UNUSED(cvPixelBuffer);
    Q_UNUSED(width);
    Q_UNUSED(height);
#endif
}

void VideoForm::staysOnTop(bool top)
{
    const bool changed = windowFlags().testFlag(Qt::WindowStaysOnTopHint) != top;
    const bool needShow = isVisible();
    const bool needShowTool = m_toolForm && m_toolForm->isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, top);
    if (m_toolForm) {
        m_toolForm->setWindowFlag(Qt::WindowStaysOnTopHint, top);
        m_toolForm->setWindowOnTop(top);
    }
    if (needShow) {
        show();
    }
    if (needShowTool) {
        m_toolForm->show();
    }
    if (changed) {
        emit windowOnTopChanged(top);
    }
}

void VideoForm::mousePressEvent(QMouseEvent *event)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (event->button() == Qt::MiddleButton) {
        if (device && !device->isCurrentCustomKeymap()) {
            device->postGoHome();
            return;
        }
    }

    if (event->button() == Qt::RightButton) {
        if (device && !device->isCurrentCustomKeymap()) {
            device->postGoBack();
            return;
        }
    }

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = event->localPos();
        QPointF globalPos = event->globalPos();
#else
        QPointF localPos = event->position();
        QPointF globalPos = event->globalPosition();
#endif

    QWidget *vw = videoWidget();
    if (vw && vw->geometry().contains(event->pos())) {
        if (!device) {
            return;
        }
        QPointF mappedPos = vw->mapFrom(this, localPos.toPoint());
        QMouseEvent newEvent(event->type(), mappedPos, globalPos, event->button(), event->buttons(), event->modifiers());
        emit device->mouseEvent(&newEvent, m_frameSize, vw->size());

        // debug keymap pos
        if (event->button() == Qt::LeftButton) {
            qreal x = localPos.x() / vw->size().width();
            qreal y = localPos.y() / vw->size().height();
            QString posTip = QString(R"("pos": {"x": %1, "y": %2})").arg(x).arg(y);
            qInfo() << posTip.toStdString().c_str();
        }
    } else {
        if (event->button() == Qt::LeftButton) {
            m_dragPosition = globalPos.toPoint() - frameGeometry().topLeft();
            event->accept();
        }
    }
}

void VideoForm::mouseReleaseEvent(QMouseEvent *event)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (m_dragPosition.isNull()) {
        if (!device) {
            return;
        }
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = event->localPos();
        QPointF globalPos = event->globalPos();
#else
        QPointF localPos = event->position();
        QPointF globalPos = event->globalPosition();
#endif
        QWidget *vw = videoWidget();
        if (!vw) {
            return;
        }

        // local check
        QPointF local = vw->mapFrom(this, localPos.toPoint());
        if (local.x() < 0) {
            local.setX(0);
        }
        if (local.x() > vw->width()) {
            local.setX(vw->width());
        }
        if (local.y() < 0) {
            local.setY(0);
        }
        if (local.y() > vw->height()) {
            local.setY(vw->height());
        }
        QMouseEvent newEvent(event->type(), local, globalPos, event->button(), event->buttons(), event->modifiers());
        emit device->mouseEvent(&newEvent, m_frameSize, vw->size());
    } else {
        m_dragPosition = QPoint(0, 0);
    }
}

void VideoForm::mouseMoveEvent(QMouseEvent *event)
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = event->localPos();
        QPointF globalPos = event->globalPos();
#else
        QPointF localPos = event->position();
        QPointF globalPos = event->globalPosition();
#endif
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    QWidget *vw = videoWidget();
    if (vw && vw->geometry().contains(event->pos())) {
        if (!device) {
            return;
        }
        QPointF mappedPos = vw->mapFrom(this, localPos.toPoint());
        QMouseEvent newEvent(event->type(), mappedPos, globalPos, event->button(), event->buttons(), event->modifiers());
        emit device->mouseEvent(&newEvent, m_frameSize, vw->size());
    } else if (!m_dragPosition.isNull()) {
        if (event->buttons() & Qt::LeftButton) {
            move(globalPos.toPoint() - m_dragPosition);
            event->accept();
        }
    }
}

void VideoForm::mouseDoubleClickEvent(QMouseEvent *event)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    QWidget *vw = videoWidget();
    if (event->button() == Qt::LeftButton && vw && !vw->geometry().contains(event->pos())) {
        if (!isMaximized()) {
            removeBlackRect();
        }
    }

    if (event->button() == Qt::RightButton && device && !device->isCurrentCustomKeymap()) {
        emit device->postBackOrScreenOn(event->type() == QEvent::MouseButtonPress);
    }

    if (vw && vw->geometry().contains(event->pos())) {
        if (!device) {
            return;
        }
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF localPos = event->localPos();
        QPointF globalPos = event->globalPos();
#else
        QPointF localPos = event->position();
        QPointF globalPos = event->globalPosition();
#endif
        QPointF mappedPos = vw->mapFrom(this, localPos.toPoint());
        QMouseEvent newEvent(event->type(), mappedPos, globalPos, event->button(), event->buttons(), event->modifiers());
        emit device->mouseEvent(&newEvent, m_frameSize, vw->size());
    }
}

void VideoForm::wheelEvent(QWheelEvent *event)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    QWidget *vw = videoWidget();
    if (!vw) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (vw->geometry().contains(event->position().toPoint())) {
        if (!device) {
            return;
        }
        QPointF pos = vw->mapFrom(this, event->position().toPoint());
        QWheelEvent wheelEvent(
            pos, event->globalPosition(), event->pixelDelta(), event->angleDelta(), event->buttons(), event->modifiers(), event->phase(), event->inverted());
#else
    if (vw->geometry().contains(event->pos())) {
        if (!device) {
            return;
        }
        QPointF pos = vw->mapFrom(this, event->pos());

        QWheelEvent wheelEvent(
            pos, event->globalPosF(), event->pixelDelta(), event->angleDelta(), event->delta(), event->orientation(),
            event->buttons(), event->modifiers(), event->phase(), event->source(), event->inverted());
#endif
        emit device->wheelEvent(&wheelEvent, m_frameSize, vw->size());
    }
}

void VideoForm::keyPressEvent(QKeyEvent *event)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    if (Qt::Key_Escape == event->key() && !event->isAutoRepeat() && isFullScreen()) {
        switchFullScreen();
    }

    QWidget *vw = videoWidget();
    QSize widgetSize = vw ? vw->size() : m_frameSize;
    emit device->keyEvent(event, m_frameSize, widgetSize);
}

void VideoForm::keyReleaseEvent(QKeyEvent *event)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    QWidget *vw = videoWidget();
    QSize widgetSize = vw ? vw->size() : m_frameSize;
    emit device->keyEvent(event, m_frameSize, widgetSize);
}

void VideoForm::paintEvent(QPaintEvent *paint)
{
    Q_UNUSED(paint)
    QStyleOption opt;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    opt.init(this);
#else
    opt.initFrom(this);
#endif
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void VideoForm::showEvent(QShowEvent *event)
{
    Q_UNUSED(event)
    if (!isFullScreen() && this->show_toolbar) {
        QTimer::singleShot(500, this, [this](){
            showToolForm(this->show_toolbar);
        });
    }
}

void VideoForm::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event)
    if (m_flexDisplay) {
        m_pendingDisplaySize = ui->keepRatioWidget->size();
        if (!m_pendingDisplaySize.isEmpty()) {
            m_flexResizeTimer.start();
        }
        return;
    }

    QSize goodSize = ui->keepRatioWidget->goodSize();
    if (goodSize.isEmpty()) {
        return;
    }
    QSize curSize = size();
    const int panelWidth = filePanelWidth();
    // 限制VideoForm尺寸不能小于keepRatioWidget good size
    if (m_widthHeightRatio > 1.0f) {
        // hor
        if (curSize.height() <= goodSize.height()) {
            setMinimumHeight(goodSize.height());
        } else {
            setMinimumHeight(0);
        }
        setMinimumWidth(0);
    } else {
        // ver
        if (curSize.width() <= goodSize.width() + panelWidth) {
            setMinimumWidth(goodSize.width() + panelWidth);
        } else {
            setMinimumWidth(panelWidth);
        }
    }
}

void VideoForm::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    Config::getInstance().setRect(device->getSerial(), geometry());
    device->disconnectDevice();
}

void VideoForm::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void VideoForm::dragMoveEvent(QDragMoveEvent *event)
{
    Q_UNUSED(event)
}

void VideoForm::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event)
}

void VideoForm::dropEvent(QDropEvent *event)
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    const QMimeData *qm = event->mimeData();
    QList<QUrl> urls = qm->urls();

    for (const QUrl &url : urls) {
        QString file = url.toLocalFile();
        QFileInfo fileInfo(file);

        if (!fileInfo.exists()) {
            QMessageBox::warning(this, "QtScrcpy", tr("file does not exist"), QMessageBox::Ok);
            continue;
        }

        if (fileInfo.isFile() && fileInfo.suffix() == "apk") {
            emit device->installApkRequest(file);
            continue;
        }
        emit device->pushFileRequest(file, Config::getInstance().getPushFilePath() + fileInfo.fileName());
    }
}

void VideoForm::installApkRequest(const QString &apkFile)
{
    m_apkStatusTimer.stop();
    if (m_apkPending == 0) {
        m_apkSucceeded = 0;
        m_apkFailed = 0;
    }
    ++m_apkPending;
    const int completed = m_apkSucceeded + m_apkFailed;
    showApkStatus(m_apkPending + completed == 1
        ? tr("Installing APK: %1").arg(QFileInfo(apkFile).fileName())
        : tr("Installing APKs... %1/%2 completed").arg(completed).arg(completed + m_apkPending));
}

void VideoForm::showApkStatus(const QString &text)
{
    m_apkStatus->setText(text);
    const int availableWidth = qMax(1, ui->keepRatioWidget->width() - 24);
    m_apkStatus->setFixedWidth(qMin(360, availableWidth));
    m_apkStatus->adjustSize();
    m_apkStatus->move((ui->keepRatioWidget->width() - m_apkStatus->width()) / 2, 12);
    m_apkStatus->raise();
    m_apkStatus->show();
}
