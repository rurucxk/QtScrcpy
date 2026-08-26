#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHideEvent>
#include <QHash>
#include <QListWidget>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "iconhelper.h"
#include "toolform.h"
#include "ui_toolform.h"
#include "videoform.h"
#include "../groupcontroller/groupcontroller.h"
#include "../util/config.h"

ToolForm::ToolForm(QWidget *adsorbWidget, AdsorbPositions adsorbPos) : MagneticWidget(adsorbWidget, adsorbPos), ui(new Ui::ToolForm)
{
    ui->setupUi(this);
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    //setWindowFlags(windowFlags() & ~Qt::WindowMinMaxButtonsHint);

    updateGroupControl();

    initStyle();
    for (QPushButton *button : toolbarButtons()) {
        m_defaultToolbarOrder.append(button->objectName());
    }
    const UserBootConfig config = Config::getInstance().getUserBootConfig();
    applyToolbarOrder(config.toolbarOrder, config.hiddenToolbarButtons);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &ToolForm::showToolbarContextMenu);
    for (QPushButton *button : toolbarButtons()) {
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QWidget::customContextMenuRequested, this, &ToolForm::showToolbarContextMenu);
    }
}

ToolForm::~ToolForm()
{
    delete ui;
}

void ToolForm::setSerial(const QString &serial)
{
    m_serial = serial;
    updateCameraMode();
}

void ToolForm::setFilePanelVisible(bool visible)
{
    ui->filePanelBtn->setChecked(visible);
}

void ToolForm::setWindowOnTop(bool top)
{
    ui->windowOnTopBtn->setChecked(top);
    ui->windowOnTopBtn->setStyleSheet(top ? "color: #0078d4" : "");
}

bool ToolForm::isHost()
{
    return m_isHost;
}

QList<QPushButton *> ToolForm::toolbarButtons() const
{
    QList<QPushButton *> buttons;
    for (int i = 0; i < ui->verticalLayout->count(); ++i) {
        if (auto *button = qobject_cast<QPushButton *>(ui->verticalLayout->itemAt(i)->widget())) {
            buttons.append(button);
        }
    }
    return buttons;
}

void ToolForm::applyToolbarOrder(const QStringList &order, const QStringList &hiddenButtons)
{
    const QList<QPushButton *> buttons = toolbarButtons();
    QHash<QString, QPushButton *> buttonsByName;
    for (QPushButton *button : buttons) {
        buttonsByName.insert(button->objectName(), button);
    }

    QList<QPushButton *> orderedButtons;
    QSet<QString> usedNames;
    for (const QString &name : order) {
        if (buttonsByName.contains(name) && !usedNames.contains(name)) {
            orderedButtons.append(buttonsByName.value(name));
            usedNames.insert(name);
        }
    }
    for (QPushButton *button : buttons) {
        if (!usedNames.contains(button->objectName())) {
            orderedButtons.append(button);
        }
        ui->verticalLayout->removeWidget(button);
    }

    m_hiddenToolbarButtons.clear();
    for (const QString &name : hiddenButtons) {
        if (buttonsByName.contains(name) && !m_hiddenToolbarButtons.contains(name)) {
            m_hiddenToolbarButtons.append(name);
        }
    }

    for (QPushButton *button : orderedButtons) {
        ui->verticalLayout->addWidget(button);
    }

    updateCameraMode();
}

void ToolForm::showToolbarContextMenu(const QPoint &pos)
{
    auto *source = qobject_cast<QWidget *>(sender());
    QMenu menu(this);
    QAction *editAction = menu.addAction(tr("adjust toolbar order"));
    if (menu.exec((source ? source : this)->mapToGlobal(pos)) == editAction) {
        editToolbarOrder();
    }
}

void ToolForm::editToolbarOrder()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("adjust toolbar order"));
    dialog.resize(420, 680);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("toolbar buttons"), &dialog));
    auto *visibleList = new QListWidget(&dialog);
    visibleList->setDragDropMode(QAbstractItemView::InternalMove);
    visibleList->setDragEnabled(true);
    visibleList->viewport()->setAcceptDrops(true);
    visibleList->setDropIndicatorShown(true);
    visibleList->setDragDropOverwriteMode(false);
    visibleList->setDefaultDropAction(Qt::MoveAction);
    auto *hiddenList = new QListWidget(&dialog);
    QHash<QString, QPushButton *> buttonsByName;
    auto addButtonItem = [](QListWidget *list, QPushButton *button) {
        auto *item = new QListWidgetItem(button->toolTip());
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
        item->setData(Qt::UserRole, button->objectName());
        list->addItem(item);
    };
    for (QPushButton *button : toolbarButtons()) {
        buttonsByName.insert(button->objectName(), button);
        addButtonItem(m_hiddenToolbarButtons.contains(button->objectName()) ? hiddenList : visibleList, button);
    }
    layout->addWidget(visibleList, 2);

    auto *moveButtonsLayout = new QHBoxLayout;
    auto *deleteButton = new QPushButton(tr("delete selected button"), &dialog);
    auto *restoreButton = new QPushButton(tr("restore selected button"), &dialog);
    deleteButton->setEnabled(false);
    restoreButton->setEnabled(false);
    moveButtonsLayout->addWidget(deleteButton);
    moveButtonsLayout->addWidget(restoreButton);
    layout->addLayout(moveButtonsLayout);

    layout->addWidget(new QLabel(tr("deleted buttons"), &dialog));
    layout->addWidget(hiddenList, 1);

    connect(visibleList, &QListWidget::currentRowChanged, deleteButton, [deleteButton](int row) {
        deleteButton->setEnabled(row >= 0);
    });
    connect(hiddenList, &QListWidget::currentRowChanged, restoreButton, [restoreButton](int row) {
        restoreButton->setEnabled(row >= 0);
    });
    connect(deleteButton, &QPushButton::clicked, &dialog, [visibleList, hiddenList]() {
        if (visibleList->currentRow() >= 0) {
            hiddenList->addItem(visibleList->takeItem(visibleList->currentRow()));
        }
    });
    connect(restoreButton, &QPushButton::clicked, &dialog, [visibleList, hiddenList]() {
        if (hiddenList->currentRow() >= 0) {
            visibleList->addItem(hiddenList->takeItem(hiddenList->currentRow()));
        }
    });

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *resetButton = buttonBox->addButton(tr("restore defaults"), QDialogButtonBox::ResetRole);
    buttonBox->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(resetButton, &QPushButton::clicked, &dialog,
            [this, visibleList, hiddenList, buttonsByName, addButtonItem]() {
        visibleList->clear();
        hiddenList->clear();
        for (const QString &name : m_defaultToolbarOrder) {
            if (buttonsByName.contains(name)) {
                addButtonItem(visibleList, buttonsByName.value(name));
            }
        }
    });
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QStringList order;
    QStringList hiddenButtons;
    for (int i = 0; i < visibleList->count(); ++i) {
        order.append(visibleList->item(i)->data(Qt::UserRole).toString());
    }
    for (int i = 0; i < hiddenList->count(); ++i) {
        const QString name = hiddenList->item(i)->data(Qt::UserRole).toString();
        order.append(name);
        hiddenButtons.append(name);
    }

    UserBootConfig config = Config::getInstance().getUserBootConfig();
    config.toolbarOrder = order;
    config.hiddenToolbarButtons = hiddenButtons;
    Config::getInstance().setUserBootConfig(config);

    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (auto *toolForm = qobject_cast<ToolForm *>(widget)) {
            toolForm->applyToolbarOrder(order, hiddenButtons);
        }
    }
}

void ToolForm::updateCameraMode()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    const bool camera = device && device->isCameraMode();

    auto setButtonVisible = [this](QPushButton *button, bool modeVisible) {
        button->setVisible(modeVisible && !m_hiddenToolbarButtons.contains(button->objectName()));
    };
    for (QPushButton *button : toolbarButtons()) {
        setButtonVisible(button, true);
    }
    setButtonVisible(ui->groupControlBtn, !camera);
    setButtonVisible(ui->expandNotifyBtn, !camera);
    setButtonVisible(ui->expandSettingsBtn, !camera);
    setButtonVisible(ui->rotateBtn, !camera);
    setButtonVisible(ui->touchBtn, !camera);
    setButtonVisible(ui->openScreenBtn, !camera);
    setButtonVisible(ui->closeScreenBtn, !camera);
    setButtonVisible(ui->powerBtn, !camera);
    setButtonVisible(ui->volumeUpBtn, !camera);
    setButtonVisible(ui->volumeDownBtn, !camera);
    setButtonVisible(ui->appSwitchBtn, !camera);
    setButtonVisible(ui->menuBtn, !camera);
    setButtonVisible(ui->homeBtn, !camera);
    setButtonVisible(ui->returnBtn, !camera);
    setButtonVisible(ui->clipboardBtn, !camera);
    setButtonVisible(ui->cameraTorchBtn, camera);
    setButtonVisible(ui->cameraZoomOutBtn, camera);
    setButtonVisible(ui->cameraZoomInBtn, camera);
    ui->verticalLayout->activate();
    resize(width(), sizeHint().height());
}

void ToolForm::initStyle()
{
    IconHelper::Instance()->SetIcon(ui->fullScreenBtn, QChar(0xf0b2), 15);
    IconHelper::Instance()->SetIcon(ui->windowOnTopBtn, QChar(0xf08d), 15);
    IconHelper::Instance()->SetIcon(ui->filePanelBtn, QChar(0xf07b), 15);
    IconHelper::Instance()->SetIcon(ui->menuBtn, QChar(0xf096), 15);
    IconHelper::Instance()->SetIcon(ui->homeBtn, QChar(0xf1db), 15);
    //IconHelper::Instance()->SetIcon(ui->returnBtn, QChar(0xf104), 15);
    IconHelper::Instance()->SetIcon(ui->returnBtn, QChar(0xf053), 15);
    IconHelper::Instance()->SetIcon(ui->appSwitchBtn, QChar(0xf24d), 15);
    IconHelper::Instance()->SetIcon(ui->volumeUpBtn, QChar(0xf028), 15);
    IconHelper::Instance()->SetIcon(ui->volumeDownBtn, QChar(0xf027), 15);
    IconHelper::Instance()->SetIcon(ui->openScreenBtn, QChar(0xf06e), 15);
    IconHelper::Instance()->SetIcon(ui->closeScreenBtn, QChar(0xf070), 15);
    IconHelper::Instance()->SetIcon(ui->powerBtn, QChar(0xf011), 15);
    IconHelper::Instance()->SetIcon(ui->expandNotifyBtn, QChar(0xf103), 15);
    IconHelper::Instance()->SetIcon(ui->expandSettingsBtn, QChar(0xf013), 15);
    IconHelper::Instance()->SetIcon(ui->rotateBtn, QChar(0xf021), 15);
    IconHelper::Instance()->SetIcon(ui->screenShotBtn, QChar(0xf0c4), 15);
    IconHelper::Instance()->SetIcon(ui->touchBtn, QChar(0xf111), 15);
    IconHelper::Instance()->SetIcon(ui->groupControlBtn, QChar(0xf0c0), 15);
    IconHelper::Instance()->SetIcon(ui->clipboardBtn, QChar(0xf0c5), 15);
    IconHelper::Instance()->SetIcon(ui->cameraTorchBtn, QChar(0xf0eb), 15);
    IconHelper::Instance()->SetIcon(ui->cameraZoomOutBtn, QChar(0xf010), 15);
    IconHelper::Instance()->SetIcon(ui->cameraZoomInBtn, QChar(0xf00e), 15);
}

void ToolForm::updateGroupControl()
{
    if (m_isHost) {
        ui->groupControlBtn->setStyleSheet("color: red");
    } else {
        ui->groupControlBtn->setStyleSheet("color: green");
    }

    GroupController::instance().updateDeviceState(m_serial);
}

void ToolForm::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
#else
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
#endif
        event->accept();
    }
}

void ToolForm::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
}

void ToolForm::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        move(event->globalPos() - m_dragPosition);
#else
        move(event->globalPosition().toPoint() - m_dragPosition);
#endif
        event->accept();
    }
}

void ToolForm::showEvent(QShowEvent *event)
{
    Q_UNUSED(event)
    qDebug() << "show event";
}

void ToolForm::hideEvent(QHideEvent *event)
{
    Q_UNUSED(event)
    qDebug() << "hide event";
}

void ToolForm::on_fullScreenBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }

    dynamic_cast<VideoForm*>(parent())->switchFullScreen();
}

void ToolForm::on_windowOnTopBtn_clicked(bool checked)
{
    auto videoForm = dynamic_cast<VideoForm*>(parent());
    if (videoForm) {
        videoForm->staysOnTop(checked);
    }
}

void ToolForm::on_filePanelBtn_clicked()
{
    auto videoForm = dynamic_cast<VideoForm*>(parent());
    if (videoForm) {
        videoForm->toggleFilePanel();
    }
}

void ToolForm::on_returnBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postGoBack();
}

void ToolForm::on_homeBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postGoHome();
}

void ToolForm::on_menuBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postGoMenu();
}

void ToolForm::on_appSwitchBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postAppSwitch();
}

void ToolForm::on_powerBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postPower();
}

void ToolForm::on_screenShotBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->screenshot();
}

void ToolForm::on_volumeUpBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postVolumeUp();
}

void ToolForm::on_volumeDownBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->postVolumeDown();
}

void ToolForm::on_closeScreenBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->setDisplayPower(false);
}

void ToolForm::on_expandNotifyBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->expandNotificationPanel();
}

void ToolForm::on_expandSettingsBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (device) {
        device->expandSettingsPanel();
    }
}

void ToolForm::on_rotateBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (device) {
        device->rotateDevice();
    }
}

void ToolForm::on_touchBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }

    m_showTouch = !m_showTouch;
    device->showTouch(m_showTouch);
}

void ToolForm::on_cameraTorchBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device || !device->isCameraMode()) {
        return;
    }
    m_cameraTorch = !m_cameraTorch;
    device->setCameraTorch(m_cameraTorch);
    ui->cameraTorchBtn->setStyleSheet(m_cameraTorch ? "color: #f0c419" : "");
}

void ToolForm::on_cameraZoomOutBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (device && device->isCameraMode()) {
        device->cameraZoomOut();
    }
}

void ToolForm::on_cameraZoomInBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (device && device->isCameraMode()) {
        device->cameraZoomIn();
    }
}

void ToolForm::on_groupControlBtn_clicked()
{
    m_isHost = !m_isHost;
    updateGroupControl();
}

void ToolForm::on_openScreenBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->setDisplayPower(true);
}

void ToolForm::on_clipboardBtn_clicked()
{
    auto device = qsc::IDeviceManage::getInstance().getDevice(m_serial);
    if (!device) {
        return;
    }
    device->requestDeviceClipboard();
}
