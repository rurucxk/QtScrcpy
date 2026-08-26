#ifndef VIDEOFORM_H
#define VIDEOFORM_H

#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include "../QtScrcpyCore/include/QtScrcpyCore.h"

namespace Ui
{
    class videoForm;
}

class ToolForm;
class FileHandler;
class QYUVOpenGLWidget;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class MetalVideoWidget;
class QPushButton;
class QSplitter;
namespace qsc { class AdbProcess; }
class VideoForm : public QWidget, public qsc::DeviceObserver
{
    Q_OBJECT
public:
    explicit VideoForm(bool framelessWindow = false, bool skin = true, bool showToolBar = true, int decodeMode = 0, QWidget *parent = 0);
    ~VideoForm();

    void staysOnTop(bool top = true);
    void updateShowSize(const QSize &newSize);
    void updateRender(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV);
    void setSerial(const QString& serial);
    QRect getGrabCursorRect();
    const QSize &frameSize();
    void resizeSquare();
    void removeBlackRect();
    void showFPS(bool show);
    void switchFullScreen();
    void toggleFilePanel();
    bool isHost();

signals:
    void windowOnTopChanged(bool top);

private:
    void onFrame(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
                 int linesizeY, int linesizeU, int linesizeV) override;
    // VideoToolbox Metal 路径帧回调（仅 macOS arm64）
    void onFrameMetal(void* cvPixelBuffer, int width, int height) override;
    void updateFPS(quint32 fps) override;
    void onVideoSessionChanged(const QSize &size, bool clientResized) override;
    void grabCursor(bool grab) override;

    void updateStyleSheet(bool vertical);
    QMargins getMargins(bool vertical);
    void initUI();
    void initFilePanel();
    void setFilePanelVisible(bool visible, bool persist = true);
    void loadFilePath(const QString &path);
    void refreshFileList();
    void uploadFile();
    void downloadFile();
    void openFile(QListWidgetItem *item);
    void createDirectory();
    void removeFile();
    void sortFileList();
    void updateFileButtons();
    void setFileBusy(bool busy, const QString &status = QString());
    void onFileAdbResult(int processResult);
    QString normalizeRemotePath(const QString &path) const;
    QString remoteChildPath(const QString &name) const;
    bool isValidChildName(const QString &name) const;
    int filePanelWidth() const;

    void showToolForm(bool show = true);
    void moveCenter();
    void installShortcut();
    QRect getScreenRect();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    // 获取当前视频渲染 widget（OpenGL 或 Metal 容器）
    QWidget* videoWidget() const;
    // 是否使用 Metal 渲染路径
    bool isMetalMode() const;

    // ui
    Ui::videoForm *ui;
    QPointer<ToolForm> m_toolForm;
    QPointer<QWidget> m_loadingWidget;
    QPointer<QYUVOpenGLWidget> m_videoWidget;

    // Metal 渲染路径（仅 macOS arm64）
    QPointer<MetalVideoWidget> m_metalWidget;

    QPointer<QLabel> m_fpsLabel;
    QPointer<QSplitter> m_splitter;
    QPointer<QWidget> m_filePanel;
    QPointer<QLineEdit> m_filePathEdit;
    QPointer<QListWidget> m_fileList;
    QPointer<QLabel> m_fileStatus;
    QPointer<QLabel> m_fileToolTip;
    QPointer<QPushButton> m_fileUpBtn;
    QPointer<QPushButton> m_fileRefreshBtn;
    QPointer<QPushButton> m_fileSortBtn;
    QPointer<QPushButton> m_fileUploadBtn;
    QPointer<QPushButton> m_fileDownloadBtn;
    QPointer<QPushButton> m_fileMkdirBtn;
    QPointer<QPushButton> m_fileRemoveBtn;
    QPointer<qsc::AdbProcess> m_fileAdb;

    //inside member
    QSize m_frameSize;
    QSize m_normalSize;
    QPoint m_dragPosition;
    float m_widthHeightRatio = 0.5f;
    bool m_skin = true;
    QPoint m_fullScreenBeforePos;
    QString m_serial;
    int m_decodeMode = 0;
    bool m_metalFirstFrame = true;  // Metal 首次帧标记
    bool m_flexDisplay = false;
    bool m_preventAutoResize = false;
    QTimer m_flexResizeTimer;
    QSize m_pendingDisplaySize;
    enum FileOperation { FO_NONE, FO_LIST, FO_PUSH, FO_PULL, FO_OPEN, FO_MKDIR, FO_REMOVE };
    FileOperation m_fileOperation = FO_NONE;
    QString m_currentFilePath = "/sdcard";
    QString m_pendingRemotePath;
    QString m_pendingLocalPath;
    QTemporaryDir m_openTempDir;
    bool m_showFilePanel = false;

    //Whether to display the toolbar when connecting a device.
    bool show_toolbar = true;
};

#endif // VIDEOFORM_H
