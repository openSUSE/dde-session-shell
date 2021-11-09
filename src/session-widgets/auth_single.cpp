/*
* Copyright (C) 2021 ~ 2021 Uniontech Software Technology Co.,Ltd.
*
* Author:     Zhang Qipeng <zhangqipeng@uniontech.com>
*
* Maintainer: Zhang Qipeng <zhangqipeng@uniontech.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "auth_single.h"

#include "authcommon.h"
#include "dlineeditex.h"

#include <DHiDPIHelper>

#include <QKeyEvent>
#include <QTimer>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QApplication>
#include <QDesktopWidget>
#include <QDBusReply>
#include <QWindow>

#include <unistd.h>
#include <com_deepin_daemon_accounts_user.h>

#define Service "com.deepin.dialogs.ResetPassword"
#define Path "/com/deepin/dialogs/ResetPassword"
#define Interface "com.deepin.dialogs.ResetPassword"

using namespace AuthCommon;

AuthSingle::AuthSingle(QWidget *parent)
    : AuthModule(parent)
    , m_capsLock(new DLabel(this))
    , m_lineEdit(new DLineEditEx(this))
    , m_keyboardBtn(new DPushButton(this))
    , m_passwordHintBtn(new DIconButton(this))
    , m_resetPasswordMessageVisible(false)
    , m_resetPasswordFloatingMessage(nullptr)
{
    setObjectName(QStringLiteral("AuthSingle"));
    setAccessibleName(QStringLiteral("AuthSingle"));

    m_type = AuthTypeSingle;

    initUI();
    initConnections();

    m_lineEdit->installEventFilter(this);
    setFocusProxy(m_lineEdit);
}

/**
 * @brief 初始化界面
 */
void AuthSingle::initUI()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_lineEdit->setClearButtonEnabled(false);
    m_lineEdit->setEchoMode(QLineEdit::Password);
    m_lineEdit->setContextMenuPolicy(Qt::NoContextMenu);
    m_lineEdit->setFocusPolicy(Qt::StrongFocus);
    m_lineEdit->lineEdit()->setAlignment(Qt::AlignCenter);

    QHBoxLayout *passwordLayout = new QHBoxLayout(m_lineEdit->lineEdit());
    passwordLayout->setContentsMargins(0, 0, 10, 0);
    passwordLayout->setSpacing(0);
    /* 键盘布局按钮 */
    m_keyboardBtn->setAccessibleName(QStringLiteral("KeyboardButton"));
    m_keyboardBtn->setContentsMargins(0, 0, 0, 0);
    m_keyboardBtn->setFocusPolicy(Qt::NoFocus);
    m_keyboardBtn->setCursor(Qt::ArrowCursor);
    m_keyboardBtn->setFlat(true);
    passwordLayout->addWidget(m_keyboardBtn, 0, Qt::AlignLeft | Qt::AlignVCenter);
    /* 缩放因子 */
    passwordLayout->addStretch(1);
    /* 大小写状态 */
    QPixmap pixmap = DHiDPIHelper::loadNxPixmap(":/misc/images/caps_lock.svg");
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_capsLock->setAccessibleName(QStringLiteral("CapsStatusLabel"));
    m_capsLock->setPixmap(pixmap);
    passwordLayout->addWidget(m_capsLock, 0, Qt::AlignRight | Qt::AlignVCenter);
    /* 密码提示 */
    m_passwordHintBtn->setAccessibleName(QStringLiteral("PasswordHintButton"));
    m_passwordHintBtn->setContentsMargins(0, 0, 0, 0);
    m_passwordHintBtn->setFocusPolicy(Qt::NoFocus);
    m_passwordHintBtn->setCursor(Qt::ArrowCursor);
    m_passwordHintBtn->setFlat(true);
    m_passwordHintBtn->setIcon(QIcon(PASSWORD_HINT));
    m_passwordHintBtn->setIconSize(QSize(16, 16));
    passwordLayout->addWidget(m_passwordHintBtn, 0, Qt::AlignRight | Qt::AlignVCenter);

    mainLayout->addWidget(m_lineEdit);
}

/**
 * @brief 初始化信号连接
 */
void AuthSingle::initConnections()
{
    AuthModule::initConnections();
    /* 键盘布局按钮 */
    connect(m_keyboardBtn, &QPushButton::clicked, this, &AuthSingle::requestShowKeyboardList);
    /* 密码提示 */
    connect(m_passwordHintBtn, &DIconButton::clicked, this, &AuthSingle::showPasswordHint);
    /* 输入框 */
    connect(m_lineEdit, &DLineEditEx::focusChanged, this, [this](const bool focus) {
        if (!focus) {
            m_lineEdit->setAlert(false);
        }
        emit focusChanged(focus);
    });
    connect(m_lineEdit, &DLineEditEx::textChanged, this, [this](const QString &text) {
        m_lineEdit->setAlert(false);
        emit lineEditTextChanged(text);
    });
    connect(m_lineEdit, &DLineEditEx::returnPressed, this, [this] {
        if (!m_lineEdit->text().isEmpty()) {
            setAnimationStatus(true);
            m_lineEdit->clearFocus();
            m_lineEdit->setFocusPolicy(Qt::NoFocus);
            m_lineEdit->lineEdit()->setReadOnly(true);
            emit requestAuthenticate();
        }
    });
}

/**
 * @brief AuthSingle::reset
 */
void AuthSingle::reset()
{
    m_lineEdit->clear();
    m_lineEdit->setAlert(false);
    m_lineEdit->hideAlertMessage();
    setLineEditEnabled(true);
}

/**
 * @brief 设置认证状态
 *
 * @param status
 * @param result
 */
void AuthSingle::setAuthStatus(const int state, const QString &result)
{
    qDebug() << "AuthSingle::setAuthResult:" << state << result;
    m_status = state;
    switch (state) {
    case StatusCodeSuccess:
        setAnimationStatus(false);
        m_lineEdit->setAlert(false);
        m_lineEdit->clear();
        setLineEditEnabled(false);
        setLineEditInfo(result, PlaceHolderText);
        emit authFinished(StatusCodeSuccess);
        break;
    case StatusCodeFailure: {
        setAnimationStatus(false);
        m_lineEdit->clear();
        if (m_limitsInfo->locked) {
            m_lineEdit->setAlert(false);
            setLineEditEnabled(false);
            if (m_integerMinutes == 1) {
                setLineEditInfo(tr("Please try again 1 minute later"), PlaceHolderText);
            } else {
                setLineEditInfo(tr("Please try again %n minutes later", "", static_cast<int>(m_integerMinutes)), PlaceHolderText);
            }
            if (m_currentUid <= 9999 && isUserAccountBinded()) {
                setResetPasswordMessageVisible(true);
                updateResetPasswordUI();
            }
        } else {
            setLineEditEnabled(true);
            setLineEditInfo(result, AlertText);
        }
        break;
    }
    case StatusCodeCancel:
        setAnimationStatus(false);
        m_lineEdit->setAlert(false);
        m_lineEdit->hideAlertMessage();
        break;
    case StatusCodeTimeout:
        setAnimationStatus(false);
        setLineEditInfo(result, AlertText);
        break;
    case StatusCodeError:
        setAnimationStatus(false);
        setLineEditInfo(result, AlertText);
        break;
    case StatusCodeVerify:
        setAnimationStatus(true);
        break;
    case StatusCodeException:
        setAnimationStatus(false);
        setLineEditInfo(result, AlertText);
        break;
    case StatusCodePrompt:
        setAnimationStatus(false);
        m_lineEdit->clear();
        setLineEditEnabled(true);
        setLineEditInfo(result, PlaceHolderText);
        break;
    case StatusCodeStarted:
        break;
    case StatusCodeEnded:
        break;
    case StatusCodeLocked:
        setAnimationStatus(false);
        break;
    case StatusCodeRecover:
        setAnimationStatus(false);
        break;
    case StatusCodeUnlocked:
        break;
    default:
        setAnimationStatus(false);
        setLineEditInfo(result, AlertText);
        qWarning() << "Error! The status of Password Auth is wrong!" << state << result;
        break;
    }
    update();
}

/**
 * @brief 设置大小写图标状态
 *
 * @param on
 */
void AuthSingle::setCapsLockVisible(const bool on)
{
    m_capsLock->setVisible(on);
}

/**
 * @brief 设置认证动画状态
 *
 * @param start
 */
void AuthSingle::setAnimationStatus(const bool start)
{
    start ? m_lineEdit->startAnimation() : m_lineEdit->stopAnimation();
}

/**
 * @brief 更新认证受限信息
 *
 * @param info
 */
void AuthSingle::setLimitsInfo(const LimitsInfo &info)
{
    qDebug() << "AuthSingle::setLimitsInfo" << info.unlockTime;
    AuthModule::setLimitsInfo(info);
    setPasswordHintBtnVisible(info.numFailures > 0 && !m_passwordHint.isEmpty());
}

/**
 * @brief 设置 LineEdit 是否可输入
 *
 * @param enable
 */
void AuthSingle::setLineEditEnabled(const bool enable)
{
    // m_lineEdit->setEnabled(enable);
    if (enable) {
        m_lineEdit->setFocusPolicy(Qt::StrongFocus);
        m_lineEdit->setFocus();
        m_lineEdit->lineEdit()->setReadOnly(false);
    } else {
        m_lineEdit->setFocusPolicy(Qt::NoFocus);
        m_lineEdit->clearFocus();
        m_lineEdit->lineEdit()->setReadOnly(true);
    }
}

/**
 * @brief 设置输入框中的文案
 *
 * @param text
 * @param type
 */
void AuthSingle::setLineEditInfo(const QString &text, const TextType type)
{
    switch (type) {
    case AlertText:
        m_lineEdit->showAlertMessage(text, this, 3000);
        m_lineEdit->setAlert(true);
        break;
    case InputText: {
        const int cursorPos = m_lineEdit->lineEdit()->cursorPosition();
        m_lineEdit->setText(text);
        m_lineEdit->lineEdit()->setCursorPosition(cursorPos);
        break;
    }
    case PlaceHolderText:
        m_lineEdit->setPlaceholderText(text);
        break;
    }
}

/**
 * @brief 设置密码提示内容
 * @param hint
 */
void AuthSingle::setPasswordHint(const QString &hint)
{
    if (hint == m_passwordHint) {
        return;
    }
    m_passwordHint = hint;
}

void AuthSingle::setCurrentUid(uid_t uid)
{
    m_currentUid = uid;
}

/**
 * @brief 设置键盘布局按钮显示的文案
 *
 * @param text
 */
void AuthSingle::setKeyboardButtonInfo(const QString &text)
{
    QString currentText = text.split(";").first();
    /* special mark in Japanese */
    if (currentText.contains("/")) {
        currentText = currentText.split("/").last();
    }
    m_keyboardBtn->setText(currentText);
}

/**
 * @brief 设置键盘布局按钮的显示状态
 *
 * @param visible
 */
void AuthSingle::setKeyboardButtonVisible(const bool visible)
{
    m_keyboardBtn->setVisible(visible);
}

/**
 * @brief 设置重置密码消息框的显示状态数据
 *
 * @param visible
 */
void AuthSingle::setResetPasswordMessageVisible(const bool isVisible)
{
    m_resetPasswordMessageVisible = isVisible;
}

/**
 * @brief 获取输入框中的文字
 *
 * @return QString
 */
QString AuthSingle::lineEditText() const
{
    return m_lineEdit->text();
}

/**
 * @brief 更新认证锁定时的文案
 */
void AuthSingle::updateUnlockPrompt()
{
    qDebug() << "AuthSingle::updateUnlockPrompt:" << m_integerMinutes;
    if (m_integerMinutes == 1) {
        m_lineEdit->setPlaceholderText(tr("Please try again 1 minute later"));
    } else if (m_integerMinutes > 1) {
        m_lineEdit->setPlaceholderText(tr("Please try again %n minutes later", "", static_cast<int>(m_integerMinutes)));
    } else {
        QTimer::singleShot(1000, this, [this] {
            emit activeAuth(m_type);
        });
        qInfo() << "Waiting authentication service...";
    }
    update();
}

/**
 * @brief 显示密码提示
 */
void AuthSingle::showPasswordHint()
{
    m_lineEdit->showAlertMessage(m_passwordHint, this, 3000);
}

/**
 * @brief 设置密码提示按钮的可见性
 * @param visible
 */
void AuthSingle::setPasswordHintBtnVisible(const bool isVisible)
{
    m_passwordHintBtn->setVisible(isVisible);
}

/**
 * @brief 显示重置密码消息框
 */
void AuthSingle::showResetPasswordMessage()
{
    if (m_resetPasswordFloatingMessage && m_resetPasswordFloatingMessage->isVisible()) {
        return;
    }

    QWidget *userLoginWidget = parentWidget();
    if (!userLoginWidget) {
        return;
    }

    QWidget *centerFrame = userLoginWidget->parentWidget();
    if (!centerFrame) {
        return;
    }

    QPalette pa;
    pa.setColor(QPalette::Background, QColor(247, 247, 247, 51));
    m_resetPasswordFloatingMessage = new DFloatingMessage(DFloatingMessage::MessageType::ResidentType);
    m_resetPasswordFloatingMessage->setPalette(pa);
    m_resetPasswordFloatingMessage->setIcon(QIcon::fromTheme("dialog-warning"));
    DSuggestButton *suggestButton = new DSuggestButton(tr("Reset Password"));
    m_resetPasswordFloatingMessage->setWidget(suggestButton);
    m_resetPasswordFloatingMessage->setMessage(tr("Forgot password?"));
    connect(suggestButton, &QPushButton::clicked, this, [ this ]{
        if (window()->windowHandle() && window()->windowHandle()->setKeyboardGrabEnabled(false)) {
            qDebug() << "setKeyboardGrabEnabled(false) success！";
        }
        const QString AccountsService("com.deepin.daemon.Accounts");
        const QString path = QString("/com/deepin/daemon/Accounts/User%1").arg(m_currentUid);
        com::deepin::daemon::accounts::User user(AccountsService, path, QDBusConnection::systemBus());
        auto reply = user.SetPassword("");
        reply.waitForFinished();
        qWarning() << "reply setpassword:" << reply.error().message();
    });
    DMessageManager::instance()->sendMessage(centerFrame, m_resetPasswordFloatingMessage);
    connect(m_resetPasswordFloatingMessage, &DFloatingMessage::closeButtonClicked, this, [this](){
        if (m_resetPasswordFloatingMessage) {
            delete  m_resetPasswordFloatingMessage;
            m_resetPasswordFloatingMessage = nullptr;
        }
        emit resetPasswordMessageVisibleChanged(false);
    });
    emit resetPasswordMessageVisibleChanged(true);
}

/**
 * @brief 关闭重置密码消息框
 */
void AuthSingle::closeResetPasswordMessage()
{
    if (m_resetPasswordFloatingMessage) {
        m_resetPasswordFloatingMessage->close();
        delete  m_resetPasswordFloatingMessage;
        m_resetPasswordFloatingMessage = nullptr;
        emit resetPasswordMessageVisibleChanged(false);
    }
}

/**
 * @brief 当前账户是否绑定unionid
 */
bool AuthSingle::isUserAccountBinded()
{
    QDBusInterface syncHelperInter("com.deepin.sync.Helper",
                                   "/com/deepin/sync/Helper",
                                   "com.deepin.sync.Helper",
                                   QDBusConnection::systemBus());
    QDBusReply<QString> retUOSID = syncHelperInter.call("UOSID");
    if (!syncHelperInter.isValid()) {
        return false;
    }
    QString uosid;
    if (retUOSID.error().message().isEmpty()) {
        uosid = retUOSID.value();
    } else {
        qWarning() << retUOSID.error().message();
        return false;
    }

    QDBusInterface accountsInter("com.deepin.daemon.Accounts",
                                 QString("/com/deepin/daemon/Accounts/User%1").arg(m_currentUid),
                                 "com.deepin.daemon.Accounts.User",
                                 QDBusConnection::systemBus());
    QVariant retUUID = accountsInter.property("UUID");
    if (!accountsInter.isValid()) {
        return false;
    }
    QString uuid = retUUID.toString();

    QDBusReply<QString> retLocalBindCheck= syncHelperInter.call("LocalBindCheck", uosid, uuid);
    if (!syncHelperInter.isValid()) {
        return false;
    }
    QString ubid;
    if (retLocalBindCheck.error().message().isEmpty()) {
        ubid = retLocalBindCheck.value();
    } else {
        qWarning() << "UOSID:" << uosid << "uuid:" << uuid;
        qWarning() << retLocalBindCheck.error().message();
        return false;
    }
    if(!ubid.isEmpty()) {
        return true;
    } else {
        return false;
    }
}

/**
 * @brief 更新重置密码UI相关状态
 */
void AuthSingle::updateResetPasswordUI()
{
    if (m_currentUid > 9999) {
        return;
    }
    if (m_resetPasswordMessageVisible) {
        showResetPasswordMessage();
    } else {
        closeResetPasswordMessage();
    }
}

bool AuthSingle::eventFilter(QObject *watched, QEvent *event)
{
    if (qobject_cast<DLineEditEx *>(watched) == m_lineEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->matches(QKeySequence::Cut)
            || keyEvent->matches(QKeySequence::Copy)
            || keyEvent->matches(QKeySequence::Paste)) {
            return true;
        }
    }
    return false;
}