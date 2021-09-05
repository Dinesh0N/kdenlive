/*
 *   SPDX-FileCopyrightText: 2006 Peter Penz *
 *                 2012    Simon A. Eugster <simon.eu@gmail.com>           *
 *   peter.penz@gmx.at                                                     *
 *   Code borrowed from Dolphin, adapted (2008) to Kdenlive by             *
 *   Jean-Baptiste Mardelle, jb@kdenlive.org                               *
 *                                                                         *
SPDX-License-Identifier: LicenseRef-KDE-Accepted-GPL
 ***************************************************************************/

#include "statusbarmessagelabel.h"
#include "kdenlivesettings.h"
#include "core.h"
#include "mainwindow.h"

#include <KNotification>
#include <kcolorscheme.h>
#include <kiconloader.h>
#include <klocalizedstring.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStyle>
#include <QTextEdit>

FlashLabel::FlashLabel(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
}

FlashLabel::~FlashLabel() = default;

void FlashLabel::setColor(const QColor &col)
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, col);
    setPalette(pal);
    update();
}

QColor FlashLabel::color() const
{
    return palette().window().color();
}

StatusBarMessageLabel::StatusBarMessageLabel(QWidget *parent)
    : QWidget(parent)
    , m_minTextHeight(-1)
    , m_queueSemaphore(1)
{
    setMinimumHeight(KIconLoader::SizeSmall);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    m_container = new FlashLabel(this);
    auto *lay = new QHBoxLayout(this);
    auto *lay2 = new QHBoxLayout(m_container);
    m_pixmap = new QLabel(this);
    m_pixmap->setAlignment(Qt::AlignCenter);
    m_label = new QLabel(this);
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_label->setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    m_keyMap = new QLabel(this);
    m_keyMap->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_keyMap->setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    m_progress = new QProgressBar(this);
    lay2->addWidget(m_pixmap);
    lay2->addWidget(m_label);
    lay2->addWidget(m_progress);
    lay->addWidget(m_keyMap);

    auto *spacer = new QSpacerItem(1, 1, QSizePolicy::MinimumExpanding, QSizePolicy::Maximum);
    lay->addItem(spacer);
    lay->addWidget(m_container);
    setLayout(lay);
    m_progress->setVisible(false);
    lay->setContentsMargins(BorderGap, 0, 2 * BorderGap, 0);
    m_queueTimer.setSingleShot(true);
    connect(&m_queueTimer, &QTimer::timeout, this, &StatusBarMessageLabel::slotMessageTimeout);
    connect(m_label, &QLabel::linkActivated, this, &StatusBarMessageLabel::slotShowJobLog);
}

StatusBarMessageLabel::~StatusBarMessageLabel() = default;

void StatusBarMessageLabel::mousePressEvent(QMouseEvent *event)
{
    QWidget::mousePressEvent(event);
    if (m_pixmap->rect().contains(event->localPos().toPoint()) && m_currentMessage.type == MltError) {
        confirmErrorMessage();
    }
}

void StatusBarMessageLabel::setKeyMap(const QString &text)
{
    m_keyMap->setText(text);
    m_keymapText = text;
}

void StatusBarMessageLabel::setTmpKeyMap(const QString &text)
{
    if (text.isEmpty()) {
        m_keyMap->setText(m_keymapText);
    } else {
        m_keyMap->setText(text);
    }
}

void StatusBarMessageLabel::setProgressMessage(const QString &text, MessageType type, int progress)
{
    if (type == ProcessingJobMessage) {
        m_progress->setValue(progress);
        m_progress->setVisible(progress < 100);
    } else if (m_currentMessage.type != ProcessingJobMessage || type == OperationCompletedMessage) {
        m_progress->setVisible(progress < 100);
    }
    if (text == m_currentMessage.text) {
        return;
    }
    setMessage(text, type, 0);
}

void StatusBarMessageLabel::setMessage(const QString &text, MessageType type, int timeoutMS)
{
    StatusBarMessageItem item(text, type, timeoutMS);
    if (type == OperationCompletedMessage) {
        m_progress->setVisible(false);
    }
    if (item.type == ErrorMessage || item.type == MltError) {
        KNotification::event(QStringLiteral("ErrorMessage"), item.text);
    }

    m_queueSemaphore.acquire();
    if (!m_messageQueue.contains(item)) {
        if (item.type == ErrorMessage || item.type == MltError || item.type == ProcessingJobMessage || item.type == OperationCompletedMessage || item.type == DirectMessage) {
            qCDebug(KDENLIVE_LOG) << item.text;

            // Put the new error message at first place and immediately show it
            if (item.timeoutMillis < 3000) {
                item.timeoutMillis = 3000;
            }
            if (item.type == ProcessingJobMessage) {
                // This is a job progress info, discard previous ones
                QList<StatusBarMessageItem> cleanList;
                for (const StatusBarMessageItem &msg : qAsConst(m_messageQueue)) {
                    if (msg.type != ProcessingJobMessage) {
                        cleanList << msg;
                    }
                }
                m_messageQueue = cleanList;
            } else {
                // Important error message, delete previous queue so they don't appear afterwards out of context
                m_messageQueue.clear();
            }

            m_messageQueue.push_front(item);

            // In case we are already displaying an error message, add a little delay
            int delay = 800 * static_cast<int>(m_currentMessage.type == ErrorMessage || m_currentMessage.type == MltError);
            m_queueTimer.start(delay);
        } else {
            // Message with normal priority
            m_messageQueue.push_back(item);
            if (!m_queueTimer.isValid() || m_queueTimer.elapsed() >= m_currentMessage.timeoutMillis) {
                m_queueTimer.start(0);
            }
        }
    }
    m_queueSemaphore.release();
}

bool StatusBarMessageLabel::slotMessageTimeout()
{
    m_queueSemaphore.acquire();

    bool newMessage = false;

    // Get the next message from the queue, unless the current one needs to be confirmed
    if (m_currentMessage.type == ProcessingJobMessage) {
        // Check if we have a job completed message to cancel this one
        StatusBarMessageItem item;
        while (!m_messageQueue.isEmpty()) {
            item = m_messageQueue.at(0);
            m_messageQueue.removeFirst();
            if (item.type == OperationCompletedMessage || item.type == ErrorMessage || item.type == MltError || item.type == ProcessingJobMessage) {
                m_currentMessage = item;
                m_label->setText(m_currentMessage.text);
                newMessage = true;
                break;
            }
        }
    } else if (!m_messageQueue.isEmpty()) {
        if (!m_currentMessage.needsConfirmation()) {
            m_currentMessage = m_messageQueue.at(0);
            m_label->setText(m_currentMessage.text);
            m_messageQueue.removeFirst();
            newMessage = true;
        }
    }

    // If the queue is empty, add a default (empty) message
    if (m_messageQueue.isEmpty() && m_currentMessage.type != DefaultMessage) {
        m_messageQueue.push_back(StatusBarMessageItem());
    }

    // Start a new timer, unless the current message still needs to be confirmed
    if (!m_messageQueue.isEmpty()) {

        if (!m_currentMessage.needsConfirmation()) {
            // If we only have the default message left to show in the queue,
            // keep the current one for a little longer.
            m_queueTimer.start(m_currentMessage.timeoutMillis + 4000 * static_cast<int>(m_messageQueue.at(0).type == DefaultMessage));
        }
    }

    QColor bgColor = KStatefulBrush(KColorScheme::Window, KColorScheme::NegativeBackground).brush(m_container->palette()).color();
    const char *iconName = nullptr;
    m_container->setColor(m_container->palette().window().color());
    switch (m_currentMessage.type) {
    case ProcessingJobMessage:
        iconName = "chronometer";
        m_pixmap->setCursor(Qt::ArrowCursor);
        break;
    case OperationCompletedMessage:
        iconName = "dialog-ok";
        m_pixmap->setCursor(Qt::ArrowCursor);
        break;

    case InformationMessage: {
        iconName = "dialog-information";
        m_pixmap->setCursor(Qt::ArrowCursor);
        QPropertyAnimation *anim = new QPropertyAnimation(m_container, "color", this);
        anim->setDuration(3000);
        anim->setEasingCurve(QEasingCurve::InOutQuad);
        anim->setKeyValueAt(0.2, m_container->palette().highlight().color());
        anim->setEndValue(m_container->palette().window().color());
        anim->start(QPropertyAnimation::DeleteWhenStopped);
        break;
    }

    case ErrorMessage: {
        iconName = "dialog-warning";
        m_pixmap->setCursor(Qt::ArrowCursor);
        QPropertyAnimation *anim = new QPropertyAnimation(m_container, "color", this);
        anim->setStartValue(bgColor);
        anim->setKeyValueAt(0.8, bgColor);
        anim->setEndValue(m_container->palette().window().color());
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setDuration(4000);
        anim->start(QPropertyAnimation::DeleteWhenStopped);
        break;
    }
    case MltError: {
        iconName = "dialog-close";
        m_pixmap->setCursor(Qt::PointingHandCursor);
        QPropertyAnimation *anim = new QPropertyAnimation(m_container, "color", this);
        anim->setStartValue(bgColor);
        anim->setEndValue(bgColor);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setDuration(3000);
        anim->start(QPropertyAnimation::DeleteWhenStopped);
        break;
    }
    case DefaultMessage:
        m_pixmap->setCursor(Qt::ArrowCursor);
    default:
        break;
    }

    if (iconName == nullptr) {
        m_pixmap->setVisible(false);
    } else {
        m_pixmap->setPixmap(QIcon::fromTheme(iconName).pixmap(style()->pixelMetric(QStyle::PM_SmallIconSize)));
        m_pixmap->setVisible(true);
    }
    m_queueSemaphore.release();

    return newMessage;
}

void StatusBarMessageLabel::confirmErrorMessage()
{
    m_currentMessage.confirmed = true;
    m_queueTimer.start(0);
}

void StatusBarMessageLabel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void StatusBarMessageLabel::slotShowJobLog(const QString &text)
{
    // Special actions
    if (text.startsWith(QLatin1Char('#'))) {
        if (text == QLatin1String("#projectmonitor")) {
            // Raise project monitor
            pCore->window()->raiseMonitor(false);
            return;
        } else if (text == QLatin1String("#clipmonitor")) {
            // Raise project monitor
            pCore->window()->raiseMonitor(true);
            return;
        }
    }
    QDialog d(this);
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    QWidget *mainWidget = new QWidget(this);
    auto *l = new QVBoxLayout;
    QTextEdit t(&d);
    t.insertPlainText(QUrl::fromPercentEncoding(text.toUtf8()));
    t.setReadOnly(true);
    l->addWidget(&t);
    mainWidget->setLayout(l);
    auto *mainLayout = new QVBoxLayout;
    d.setLayout(mainLayout);
    mainLayout->addWidget(mainWidget);
    mainLayout->addWidget(buttonBox);
    d.connect(buttonBox, &QDialogButtonBox::rejected, &d, &QDialog::accept);
    d.exec();
    confirmErrorMessage();
}
