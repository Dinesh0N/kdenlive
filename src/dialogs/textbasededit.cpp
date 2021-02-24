/***************************************************************************
 *   Copyright (C) 2020 by Jean-Baptiste Mardelle                          *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3 or any later version accepted by the       *
 *   membership of KDE e.V. (or its successor approved  by the membership  *
 *   of KDE e.V.), which shall act as a proxy defined in Section 14 of     *
 *   version 3 of the license.                                             *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "textbasededit.h"
#include "monitor/monitor.h"
#include "bin/bin.h"
#include "bin/projectclip.h"
#include "bin/projectsubclip.h"
#include "bin/projectitemmodel.h"
#include "timeline2/view/timelinewidget.h"
#include "timeline2/view/timelinecontroller.h"
#include "core.h"
#include "mainwindow.h"
#include "kdenlivesettings.h"
#include "timecodedisplay.h"
#include <profiles/profilemodel.hpp>

#include "klocalizedstring.h"

#include <QEvent>
#include <QKeyEvent>
#include <QToolButton>
#include <KMessageBox>

VideoTextEdit::VideoTextEdit(QWidget *parent)
    : QTextEdit(parent)
    , clipOffset(0)
    , m_hoveredBlock(-1)
    , m_lastClickedBlock(-1)
{
    setMouseTracking(true);
    setReadOnly(true);
    //setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    lineNumberArea = new LineNumberArea(this);
    connect(this, &VideoTextEdit::cursorPositionChanged, [this]() {
        lineNumberArea->update();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, [this]() {
        lineNumberArea->update();
    });
    QRect rect =  this->contentsRect();
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
}

void VideoTextEdit::repaintLines()
{
    lineNumberArea->update();
}

void VideoTextEdit::cleanup()
{
    speechZones.clear();
    cutZones.clear();
    m_hoveredBlock = -1;
    clear();
}

void VideoTextEdit::rebuildZones()
{
    speechZones.clear();
    m_selectedBlocks.clear();
    QTextCursor curs = textCursor();
    curs.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
    for (int i = 0; i < document()->blockCount(); ++i) {
        int start = curs.position() + 1;
        curs.setPosition(start);
        curs.select(QTextCursor::WordUnderCursor);
        while (curs.selectedText().isEmpty() && start < document()->characterCount()) {
            start++;
            curs.setPosition(start);
            curs.select(QTextCursor::WordUnderCursor);
        }
        int selStart = curs.selectionStart();
        int selEnd = curs.selectionEnd();
        curs.setPosition(selStart + (selEnd - selStart) / 2);
        QString anchorStart = anchorAt(cursorRect(curs).center());
        //qDebug()<<"=== START ANCHOR: "<<anchorStart<<" AT POS: "<<curs.position();
        curs.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
        int end = curs.position() - 1;
        curs.setPosition(end);
        curs.select(QTextCursor::WordUnderCursor);
        while (curs.selectedText().isEmpty() && end > start) {
            end--;
            curs.setPosition(end);
            curs.select(QTextCursor::WordUnderCursor);
        }
        selStart = curs.selectionStart();
        selEnd = curs.selectionEnd();
        curs.setPosition(selStart + (selEnd - selStart) / 2);
        QString anchorEnd = anchorAt(cursorRect(curs).center());
        qDebug()<<"=== ANCHORAs FOR : "<<i<<", "<<anchorStart<<"-"<<anchorEnd<<" AT POS: "<<curs.position();
        if (!anchorStart.isEmpty() && !anchorEnd.isEmpty()) {
            double startMs = anchorStart.section(QLatin1Char('#'), 1).section(QLatin1Char(':'), 0, 0).toDouble() + clipOffset;
            double endMs = anchorEnd.section(QLatin1Char('#'), 1).section(QLatin1Char(':'), 1, 1).toDouble() + clipOffset;
            speechZones << QPair<double, double>(startMs, endMs);
        }
        curs.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor);
    }
    repaintLines();
}

int VideoTextEdit::lineNumberAreaWidth()
{
    int space = 3 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * 11;
    return space;
}

QVector<QPoint> VideoTextEdit::processedZones(QVector<QPoint> sourceZones)
{
    QVector<QPoint> resultZones;
    QVector<QPoint> zonesToRemove;
    qDebug()<<"=== SOURCE ZONEs: "<<sourceZones;
    qDebug()<<"=== CUT ZONEs: "<<cutZones;
    for (auto &zone : sourceZones) {
        bool cutted = false;
        QVector<QPoint> resultZone;
        for (auto &cut : cutZones) {
            if (!cutted) {
                if (cut.x() > zone.x()) {
                    if (cut.x() > zone.y()) {
                        // Cut is outside zone
                        continue;
                    }
                    // Cut is inside zone
                    cutted = true;
                    if (cut.y() > zone.y()) {
                        // Only keep the start of this zone
                        resultZone << QPoint(zone.x(), cut.x());
                    } else {
                        resultZone << QPoint(zone.x(), cut.x());
                        resultZone << QPoint(cut.y(), zone.y());
                    }
                    zonesToRemove << cut;
                } else if (cut.y() < zone.y()) {
                    // Only keep the end of this zone
                    resultZone << QPoint(cut.y(), zone.y());
                    zonesToRemove << cut;
                    cutted = true;
                }
            } else {
                // Check in already cutted zones
                for (auto &subCut : resultZone) {
                    if (cut.x() > subCut.x()) {
                        if (cut.x() > subCut.y()) {
                            // cut is outside
                            continue;
                        }
                        // Cut is inside zone
                        if (cut.y() > subCut.y()) {
                            // Only keep the start of this zone
                            resultZone << QPoint(subCut.x(), cut.x());
                        } else {
                            resultZone << QPoint(subCut.x(), cut.x());
                            resultZone << QPoint(cut.y(), subCut.y());
                        }
                        zonesToRemove << subCut;
                    } else if (cut.y() < subCut.y()) {
                        // Only keep the end of this zone
                        resultZone << QPoint(cut.y(), subCut.y());
                        zonesToRemove << subCut;
                    }
                }
            }
        }
        if (!cutted) {
            resultZones << zone;
        } else {
            resultZones << resultZone;
        }
    }
    for (auto &toRemove : zonesToRemove) {
        resultZones.removeAll(toRemove);
    }
    qDebug()<<"=== FINAL CUTS: "<<resultZones;
    return resultZones;
}

QVector<QPoint> VideoTextEdit::getInsertZones()
{
    if (m_selectedBlocks.isEmpty()) {
        // return text selection, not blocks
        QTextCursor cursor = textCursor();
        QString anchorStart;
        QString anchorEnd;
        if (!cursor.selectedText().isEmpty()) {
            qDebug()<<"=== EXPORTING SELECTION";
            int start = cursor.selectionStart();
            int end = cursor.selectionEnd() - 1;
            cursor.setPosition(start);
            cursor.select(QTextCursor::WordUnderCursor);
            while (cursor.selectedText().isEmpty() && start < end) {
                start++;
                cursor.setPosition(start);
                cursor.select(QTextCursor::WordUnderCursor);
            }
            int selStart = cursor.selectionStart();
            int selEnd = cursor.selectionEnd();
            cursor.setPosition(selStart + (selEnd - selStart) / 2);
            anchorStart = anchorAt(cursorRect(cursor).center());
            cursor.setPosition(end);
            cursor.select(QTextCursor::WordUnderCursor);
            while (cursor.selectedText().isEmpty() && end > start) {
                end--;
                cursor.setPosition(end);
                cursor.select(QTextCursor::WordUnderCursor);
            }
            selStart = cursor.selectionStart();
            selEnd = cursor.selectionEnd();
            cursor.setPosition(selStart + (selEnd - selStart) / 2);
            anchorEnd = anchorAt(cursorRect(cursor).center());
        } else {
            // Return full text
            cursor.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
            int end = cursor.position() - 1;
            cursor.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
            int start = cursor.position();
            cursor.select(QTextCursor::WordUnderCursor);
            while (cursor.selectedText().isEmpty() && start < end) {
                start++;
                cursor.setPosition(start);
                cursor.select(QTextCursor::WordUnderCursor);
            }
            int selStart = cursor.selectionStart();
            int selEnd = cursor.selectionEnd();
            cursor.setPosition(selStart + (selEnd - selStart) / 2);
            anchorStart = anchorAt(cursorRect(cursor).center());
            cursor.setPosition(end);
            cursor.select(QTextCursor::WordUnderCursor);
            while (cursor.selectedText().isEmpty() && end > start) {
                end--;
                cursor.setPosition(end);
                cursor.select(QTextCursor::WordUnderCursor);
            }
            selStart = cursor.selectionStart();
            selEnd = cursor.selectionEnd();
            cursor.setPosition(selStart + (selEnd - selStart) / 2);
            anchorEnd = anchorAt(cursorRect(cursor).center());
        }
        if (!anchorStart.isEmpty() && !anchorEnd.isEmpty()) {
            double startMs = anchorStart.section(QLatin1Char('#'), 1).section(QLatin1Char(':'), 0, 0).toDouble() + clipOffset;
            double endMs = anchorEnd.section(QLatin1Char('#'), 1).section(QLatin1Char(':'), 1, 1).toDouble() + clipOffset;
            qDebug()<<"=== GOT EXPORT MAIN ZONE: "<<GenTime(startMs).frames(pCore->getCurrentFps())<<" - "<<GenTime(endMs).frames(pCore->getCurrentFps());
            QPoint originalZone(QPoint(GenTime(startMs).frames(pCore->getCurrentFps()), GenTime(endMs).frames(pCore->getCurrentFps())));
            return processedZones({originalZone});
        }
        return {};
    }
    QVector<QPoint> zones;
    int zoneStart = -1;
    int zoneEnd = -1;
    int currentEnd = -1;
    int currentStart = -1;
    qDebug()<<"=== FROM BLOCKS: "<<m_selectedBlocks;
    for (auto &bk : m_selectedBlocks) {
        QPair<double, double> z = speechZones.at(bk);
        currentStart = GenTime(z.first).frames(pCore->getCurrentFps());
        currentEnd = GenTime(z.second).frames(pCore->getCurrentFps());
        if (zoneStart < 0) {
            zoneStart = currentStart;
        } else if (currentStart - zoneEnd > 1) {
            // Insert last zone
            zones << QPoint(zoneStart, zoneEnd);
            zoneStart = currentStart;
        }
        zoneEnd = currentEnd;
    }
    qDebug()<<"=== INSERT LAST: "<<currentStart<<"-"<<currentEnd;
    zones << QPoint(currentStart, currentEnd);

    qDebug()<<"=== GOT RESULTING ZONES: "<<zones;
    return processedZones(zones);
}

void VideoTextEdit::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
}

void VideoTextEdit::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void VideoTextEdit::keyPressEvent(QKeyEvent *e)
{
    QTextEdit::keyPressEvent(e);
}

void VideoTextEdit::checkHoverBlock(int yPos)
{
    QTextCursor curs = QTextCursor(this->document());
    curs.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
    
    m_hoveredBlock = -1;
    for (int i = 0; i < this->document()->blockCount(); ++i) {
        QTextBlock block = curs.block();
        QRect r2 = this->document()->documentLayout()->blockBoundingRect(block).translated(
                0, 0 - (
                    this->verticalScrollBar()->sliderPosition()
                    ) ).toRect();
        if (yPos < r2.x()) {
            break;
        }
        if (yPos > r2.x() && yPos < r2.bottom()) {
            m_hoveredBlock = i;
            break;
        }
        curs.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor);
    }
    setCursor(m_hoveredBlock == -1 ? Qt::ArrowCursor : Qt::PointingHandCursor);
    lineNumberArea->update();
}

void VideoTextEdit::blockClicked(Qt::KeyboardModifiers modifiers, bool play)
{
    if (m_hoveredBlock > -1 && m_hoveredBlock < speechZones.count()) {
        if (m_selectedBlocks.contains(m_hoveredBlock)) {
            if (modifiers & Qt::ControlModifier) {
                // remove from selection on ctrl+click an already selected block
                m_selectedBlocks.removeAll(m_hoveredBlock);
            } else {
                m_selectedBlocks = {m_hoveredBlock};
                lineNumberArea->update();
            }
        } else {
            // Add to selection
            if (modifiers & Qt::ControlModifier) {
                m_selectedBlocks << m_hoveredBlock;
            } else if (modifiers & Qt::ShiftModifier) {
                if (m_lastClickedBlock > -1) {
                    for (int i = qMin(m_lastClickedBlock, m_hoveredBlock); i <= qMax(m_lastClickedBlock, m_hoveredBlock); i++) {
                        if (!m_selectedBlocks.contains(i)) {
                            m_selectedBlocks << i;
                        }
                    }
                } else {
                    m_selectedBlocks = {m_hoveredBlock};
                }
            } else {
                m_selectedBlocks = {m_hoveredBlock};
            }
        }
        if (m_hoveredBlock >= 0) {
            m_lastClickedBlock = m_hoveredBlock;
        }
        QPair<double, double> zone = speechZones.at(m_hoveredBlock);
        double startMs = zone.first;
        double endMs = zone.second;
        pCore->getMonitor(Kdenlive::ClipMonitor)->requestSeek(GenTime(startMs).frames(pCore->getCurrentFps()));
        pCore->getMonitor(Kdenlive::ClipMonitor)->slotLoadClipZone(QPoint(GenTime(startMs).frames(pCore->getCurrentFps()), GenTime(endMs).frames(pCore->getCurrentFps())));
        QTextCursor cursor = textCursor();
        cursor.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, m_hoveredBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        setTextCursor(cursor);        
        if (play) {
            pCore->getMonitor(Kdenlive::ClipMonitor)->slotPlayZone();
        }
    }
}

int VideoTextEdit::getFirstVisibleBlockId()
{
// Detect the first block for which bounding rect - once
// translated in absolute coordinates - is contained
// by the editor's text area

// Costly way of doing but since 
// "blockBoundingGeometry(...)" doesn't exist 
// for "QTextEdit"...

    QTextCursor curs = QTextCursor(this->document());
    curs.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
    for(int i=0; i < this->document()->blockCount(); ++i)
    {
        QTextBlock block = curs.block();

        QRect r1 = this->viewport()->geometry();
        QRect r2 = this->document()->documentLayout()->blockBoundingRect(block).translated(
                r1.x(), r1.y() - (
                    this->verticalScrollBar()->sliderPosition()
                    ) ).toRect();

        if (r1.contains(r2, true)) { return i; }

        curs.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor);
    }
    return 0;
}

void VideoTextEdit::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    this->verticalScrollBar()->setSliderPosition(this->verticalScrollBar()->sliderPosition());

    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), palette().alternateBase().color());
    int blockNumber = this->getFirstVisibleBlockId();

    QTextBlock block = this->document()->findBlockByNumber(blockNumber);
    QTextBlock prev_block = (blockNumber > 0) ? this->document()->findBlockByNumber(blockNumber-1) : block;
    int translate_y = (blockNumber > 0) ? -this->verticalScrollBar()->sliderPosition() : 0;

    int top = this->viewport()->geometry().top();

    // Adjust text position according to the previous "non entirely visible" block 
    // if applicable. Also takes in consideration the document's margin offset.
    int additional_margin;
    if (blockNumber == 0)
        // Simply adjust to document's margin
        additional_margin = (int) this->document()->documentMargin() -1 - this->verticalScrollBar()->sliderPosition();
    else
        // Getting the height of the visible part of the previous "non entirely visible" block
        additional_margin = (int) this->document()->documentLayout()->blockBoundingRect(prev_block)
                .translated(0, translate_y).intersected(this->viewport()->geometry()).height();

    // Shift the starting point
    top += additional_margin;

    int bottom = top + (int) this->document()->documentLayout()->blockBoundingRect(block).height();

    QColor col_2 = palette().link().color();
    QColor col_1 = palette().highlightedText().color();
    QColor col_0 = palette().text().color();

    // Draw the numbers (displaying the current line number in green)
    while (block.isValid() && top <= event->rect().bottom()) {
        if (blockNumber >= speechZones.count()) {
            break;
        }
        if (block.isVisible() && bottom >= event->rect().top()) {
            if (m_selectedBlocks.contains(blockNumber)) {
                painter.fillRect(QRect(0, top, lineNumberArea->width(), bottom - top), palette().highlight().color());
            }
            QString number = pCore->timecode().getDisplayTimecode(GenTime(speechZones[blockNumber].first), false);
            painter.setPen(QColor(120, 120, 120));
            painter.setPen((this->textCursor().blockNumber() == blockNumber) ? col_2 : m_selectedBlocks.contains(blockNumber) ? col_1 : col_0);
            painter.drawText(-5, top,
                             lineNumberArea->width(), fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + (int) this->document()->documentLayout()->blockBoundingRect(block).height();
        ++blockNumber;
    }

}

void VideoTextEdit::mousePressEvent(QMouseEvent *e)
{
    QTextEdit::mousePressEvent(e);
    QTextCursor current = textCursor();
    QTextCursor cursor = cursorForPosition(e->pos());
    int pos = cursor.position();
    if (pos > current.selectionStart() && pos < current.selectionStart()) {
        // Clicked in selection
    } else {
        const QString link = anchorAt(e->pos());
        if (!link.isEmpty()) {
            // Clicked on a word
            cursor.setPosition(pos + 1, QTextCursor::KeepAnchor);
            double startMs = link.section(QLatin1Char('#'), 1).section(QLatin1Char(':'), 0, 0).toDouble() + clipOffset;
            pCore->getMonitor(Kdenlive::ClipMonitor)->requestSeek(GenTime(startMs).frames(pCore->getCurrentFps()));
        }
    }
    setTextCursor(cursor);
}

void VideoTextEdit::mouseReleaseEvent(QMouseEvent *e)
{
    QTextEdit::mouseReleaseEvent(e);
    QTextCursor cursor = textCursor();
    if (!cursor.selectedText().isEmpty()) {
        // We have a selection, ensure full word is selected
        int pos = cursor.position();
        int start = cursor.selectionStart();
        int end = cursor.selectionEnd();
        qDebug()<<"=== CHARACTER POS: "<<pos<<" - sel: "<<start<<" / "<<end;
        cursor.setPosition(start);
        cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::MoveAnchor);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        pos = cursor.position();
        if (!cursor.atBlockEnd() && document()->characterAt(pos - 1) != QLatin1Char(' ')) {
            // Remove trailing space
            cursor.setPosition(pos + 1, QTextCursor::KeepAnchor);
        }
        setTextCursor(cursor);
    }
    if (!m_selectedBlocks.isEmpty()) {
        m_selectedBlocks.clear();
        repaintLines();
    }
}

void VideoTextEdit::mouseMoveEvent(QMouseEvent *e)
{
    QTextEdit::mouseMoveEvent(e);
    if (e->buttons() & Qt::LeftButton) {
        /*QTextCursor cursor = textCursor();
        cursor.movePosition(QTextCursor::EndOfWord, QTextCursor::KeepAnchor);
        setTextCursor(cursor);*/
    } else {
        const QString link = anchorAt(e->pos());
        viewport()->setCursor(link.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    }
}

TextBasedEdit::TextBasedEdit(QWidget *parent)
    : QWidget(parent)
    , m_clipDuration(0.)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    setupUi(this);
    setFocusPolicy(Qt::StrongFocus);
    vosk_config->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    vosk_config->setToolTip(i18n("Configure speech recognition"));
    connect(vosk_config, &QToolButton::clicked, [this]() {
        pCore->window()->slotPreferences(8);
    });
    m_playlist.setFileTemplate(QDir::temp().absoluteFilePath(QStringLiteral("kdenlive-speech-XXXXXX.mlt")));
    qDebug()<<"======= EDITOR TXT COLOR: "<<palette().text().color().name()<<"\n==========";
    
    // Visual text editor
    QVBoxLayout *l = new QVBoxLayout;
    m_visualEditor = new VideoTextEdit(this);
    m_visualEditor->installEventFilter(this);
    l->addWidget(m_visualEditor);
    text_frame->setLayout(l);
    m_visualEditor->setDocument(&m_document);
    connect(&m_document, &QTextDocument::blockCountChanged, [this](int ct) {
        m_visualEditor->repaintLines();
        qDebug()<<"++++++++++++++++++++\n\nGOT BLOCKS: "<<ct<<"\n\n+++++++++++++++++++++";
    });
    
    connect(m_visualEditor, &VideoTextEdit::selectionChanged, [this]() {
        bool hasSelection = m_visualEditor->textCursor().selectedText().isEmpty() == false;
        button_insert->setEnabled(hasSelection);
        button_delete->setEnabled(hasSelection);
    });
    
    connect(button_start, &QPushButton::clicked, this, &TextBasedEdit::startRecognition);
    frame_progress->setVisible(false);
    button_abort->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    connect(button_abort, &QToolButton::clicked, [this]() {
        if (m_speechJob && m_speechJob->state() == QProcess::Running) {
            m_speechJob->kill();
        }
    });
    connect(pCore.get(), &Core::updateVoskAvailability, this, &TextBasedEdit::updateAvailability);
    connect(pCore.get(), &Core::voskModelUpdate, [&](QStringList models) {
        language_box->clear();
        language_box->addItems(models);
        updateAvailability();
        if (models.isEmpty()) {
            showMessage(i18n("Please install speech recognition models"), KMessageWidget::Information);
            vosk_config->setVisible(true);
        } else {
            if (KdenliveSettings::vosk_found()) {
                vosk_config->setVisible(false);
            }
            if (!KdenliveSettings::vosk_text_model().isEmpty() && models.contains(KdenliveSettings::vosk_text_model())) {
                int ix = language_box->findText(KdenliveSettings::vosk_text_model());
                if (ix > -1) {
                    language_box->setCurrentIndex(ix);
                }
            }
        }
    });
    connect(language_box, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), [this]() {
        KdenliveSettings::setVosk_text_model(language_box->currentText());
    });
    info_message->hide();
    
    m_logAction = new QAction(i18n("Show log"), this);
    connect(m_logAction, &QAction::triggered, [this]() {
        KMessageBox::sorry(this, m_errorString, i18n("Detailed log"));
    });

    speech_zone->setChecked(KdenliveSettings::speech_zone());
    connect(speech_zone, &QCheckBox::stateChanged, [this](int state) {
        KdenliveSettings::setSpeech_zone(state == Qt::Checked);
    });
    button_delete->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    button_delete->setToolTip(i18n("Delete selected text"));
    button_delete->setEnabled(false);
    connect(button_delete, &QToolButton::clicked, this, &TextBasedEdit::deleteItem);
    
    button_add->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    button_add->setToolTip(i18n("Play edited text"));
    button_add->setEnabled(false);
    connect(button_add, &QToolButton::clicked, this, &TextBasedEdit::previewPlaylist);
    
    button_insert->setIcon(QIcon::fromTheme(QStringLiteral("timeline-insert")));
    button_insert->setToolTip(i18n("Insert selected blocks in timeline"));
    connect(button_insert, &QToolButton::clicked, this, &TextBasedEdit::insertToTimeline);
    button_insert->setEnabled(false);
    
    // Message Timer
    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(5000);
    connect(&m_hideTimer, &QTimer::timeout, info_message, &KMessageWidget::animatedHide);

    // Search stuff
    search_frame->setVisible(false);
    button_search->setIcon(QIcon::fromTheme(QStringLiteral("edit-find")));
    search_prev->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    search_next->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    connect(button_search, &QToolButton::toggled, this, [&](bool toggled) {
        search_frame->setVisible(toggled);
        search_line->setFocus();
    });
    connect(search_line, &QLineEdit::textChanged, [this](const QString &searchText) {
        QPalette palette = this->palette();
        QColor col = palette.color(QPalette::Base);
        if (searchText.length() > 2) {
            bool found = m_visualEditor->find(searchText);
            if (found) {
                col.setGreen(qMin(255, static_cast<int>(col.green() * 1.5)));
                palette.setColor(QPalette::Base,col);
                QTextCursor cur = m_visualEditor->textCursor();
                cur.select(QTextCursor::WordUnderCursor);
                m_visualEditor->setTextCursor(cur);
            } else {
                // Loop over, abort
                col.setRed(qMin(255, static_cast<int>(col.red() * 1.5)));
                palette.setColor(QPalette::Base,col);
            }
        }
        search_line->setPalette(palette);   
    });
    connect(search_next, &QToolButton::clicked, [this]() {
        const QString searchText = search_line->text();
        QPalette palette = this->palette();
        QColor col = palette.color(QPalette::Base);
        if (searchText.length() > 2) {
            bool found = m_visualEditor->find(searchText);
            if (found) {
                col.setGreen(qMin(255, static_cast<int>(col.green() * 1.5)));
                palette.setColor(QPalette::Base,col);
                QTextCursor cur = m_visualEditor->textCursor();
                cur.select(QTextCursor::WordUnderCursor);
                m_visualEditor->setTextCursor(cur);
            } else {
                // Loop over, abort
                col.setRed(qMin(255, static_cast<int>(col.red() * 1.5)));
                palette.setColor(QPalette::Base,col);
            }
        }
        search_line->setPalette(palette);  
    });
    connect(search_prev, &QToolButton::clicked, [this]() {
        const QString searchText = search_line->text();
                QPalette palette = this->palette();
        QColor col = palette.color(QPalette::Base);
        if (searchText.length() > 2) {
            bool found = m_visualEditor->find(searchText, QTextDocument::FindBackward);
            if (found) {
                col.setGreen(qMin(255, static_cast<int>(col.green() * 1.5)));
                palette.setColor(QPalette::Base,col);
                QTextCursor cur = m_visualEditor->textCursor();
                cur.select(QTextCursor::WordUnderCursor);
                m_visualEditor->setTextCursor(cur);
            } else {
                // Loop over, abort
                col.setRed(qMin(255, static_cast<int>(col.red() * 1.5)));
                palette.setColor(QPalette::Base,col);
            }
        }
        search_line->setPalette(palette);  
    });
    parseVoskDictionaries();
}

bool TextBasedEdit::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        qDebug()<<"==== FOT TXTEDIT EVENT FILTER: "<<static_cast <QKeyEvent*> (event)->key();
    }
    /*if(obj == m_visualEditor && event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = static_cast <QKeyEvent*> (event);
        if (keyEvent->key() != Qt::Key_Left && keyEvent->key() != Qt::Key_Up && keyEvent->key() != Qt::Key_Right && keyEvent->key() != Qt::Key_Down) {
            parentWidget()->setFocus();
            return true;
        }
    }*/
    return QObject::eventFilter(obj, event);
}

void TextBasedEdit::startRecognition()
{
    button_add->setEnabled(true);
    if (m_speechJob && m_speechJob->state() != QProcess::NotRunning) {
        if (KMessageBox::questionYesNo(this, i18n("Another recognition job is running. Abort it ?")) !=  KMessageBox::Yes) {
            return;
        }
    }
    info_message->hide();
    m_errorString.clear();
    qDebug()<<"======= EDITOR TXT COLOR: "<<palette().text().color().name()<<"\n==========";
    m_document.setDefaultStyleSheet(QString("body {font-size:%2px;}\na { text-decoration:none;color:%1;font-size:%2px;}").arg(palette().text().color().name()).arg(QFontInfo(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont)).pixelSize()));
    m_visualEditor->cleanup();
    //m_visualEditor->insertHtml(QStringLiteral("<body>"));

    info_message->removeAction(m_logAction);
    QString pyExec = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (pyExec.isEmpty()) {
        showMessage(i18n("Cannot find python3, please install it on your system."), KMessageWidget::Warning);
        return;
    }
    // Start python script
    QString language = language_box->currentText();
    if (language.isEmpty()) {
        showMessage(i18n("Please install a language model."), KMessageWidget::Warning);
        return;
    }
    QString speechScript = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("scripts/speechtotext.py"));
    if (speechScript.isEmpty()) {
        showMessage(i18n("The speech script was not found, check your install."), KMessageWidget::Warning);
        return;
    }
    m_binId = pCore->getMonitor(Kdenlive::ClipMonitor)->activeClipId();
    std::shared_ptr<AbstractProjectItem> clip = pCore->projectItemModel()->getItemByBinId(m_binId);
    if (clip == nullptr) {
        showMessage(i18n("Select a clip in Project Bin."), KMessageWidget::Information);
        return;
    }

    m_speechJob.reset(new QProcess(this));
    showMessage(i18n("Starting speech recognition"), KMessageWidget::Information);
    qApp->processEvents();
    QString modelDirectory = KdenliveSettings::vosk_folder_path();
    if (modelDirectory.isEmpty()) {
        modelDirectory = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("speechmodels"), QStandardPaths::LocateDirectory);
    }
    qDebug()<<"==== ANALYSIS SPEECH: "<<modelDirectory<<" - "<<language;
    
    m_sourceUrl.clear();
    QString clipName;
    m_visualEditor->clipOffset = 0;
    m_lastPosition = 0;
    double endPos = 0;
    if (clip->itemType() == AbstractProjectItem::ClipItem) {
        std::shared_ptr<ProjectClip> clipItem = std::static_pointer_cast<ProjectClip>(clip);
        if (clipItem) {
            m_sourceUrl = clipItem->url();
            clipName = clipItem->clipName();
            if (speech_zone->isChecked()) {
                // Analyse clip zone only
                QPoint zone = clipItem->zone();
                m_lastPosition = zone.x();
                m_visualEditor->clipOffset = GenTime(zone.x(), pCore->getCurrentFps()).seconds();
                m_clipDuration = GenTime(zone.y() - zone.x(), pCore->getCurrentFps()).seconds();
                endPos = m_clipDuration;
            } else {
                m_clipDuration = clipItem->duration().seconds();
            }
        }
    } else if (clip->itemType() == AbstractProjectItem::SubClipItem) {
        std::shared_ptr<ProjectSubClip> clipItem = std::static_pointer_cast<ProjectSubClip>(clip);
        if (clipItem) {
            auto master = clipItem->getMasterClip();
            m_sourceUrl = master->url();
            clipName = master->clipName();
            QPoint zone = clipItem->zone();
            m_lastPosition = zone.x();
            m_visualEditor->clipOffset = GenTime(zone.x(), pCore->getCurrentFps()).seconds();
            m_clipDuration = GenTime(zone.y() - zone.x(), pCore->getCurrentFps()).seconds();
            endPos = m_clipDuration;
        }
    }
    if (m_sourceUrl.isEmpty()) {
        showMessage(i18n("Select a clip for speech recognition."), KMessageWidget::Information);
        return;
    }
    showMessage(i18n("Starting speech recognition on %1.", clipName), KMessageWidget::Information);
    qApp->processEvents();
    connect(m_speechJob.get(), &QProcess::readyReadStandardError, this, &TextBasedEdit::slotProcessSpeechError);
    connect(m_speechJob.get(), &QProcess::readyReadStandardOutput, this, &TextBasedEdit::slotProcessSpeech);
    connect(m_speechJob.get(), static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, &TextBasedEdit::slotProcessSpeechStatus);
    qDebug()<<"=== STARTING RECO: "<<speechScript<<" / "<<modelDirectory<<" / "<<language<<" / "<<m_sourceUrl<<", START: "<<m_visualEditor->clipOffset<<", DUR: "<<endPos;
    m_speechJob->start(pyExec, {speechScript, modelDirectory, language, m_sourceUrl, QString::number(m_visualEditor->clipOffset), QString::number(endPos)});
    speech_progress->setValue(0);
    frame_progress->setVisible(true);
}

void TextBasedEdit::updateAvailability()
{
    bool enabled = KdenliveSettings::vosk_found() && language_box->count() > 0;
    button_start->setEnabled(enabled);
    vosk_config->setVisible(!enabled);
}

void TextBasedEdit::slotProcessSpeechStatus(int, QProcess::ExitStatus status)
{
    if (status == QProcess::CrashExit) {
        if (!m_errorString.isEmpty()) {
            info_message->addAction(m_logAction);
        }
        showMessage(i18n("Speech recognition aborted."), KMessageWidget::Warning);
    } else if (m_visualEditor->toPlainText().isEmpty()) {
        if (!m_errorString.isEmpty()) {
            info_message->addAction(m_logAction);
        }
        showMessage(i18n("No speech detected."), KMessageWidget::Information);
    } else {
        button_add->setEnabled(true);
        showMessage(i18n("Speech recognition finished."), KMessageWidget::Positive);
    }
    QTextCursor cur = m_visualEditor->textCursor();
    cur.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
    m_visualEditor->setTextCursor(cur);
    frame_progress->setVisible(false);
}

void TextBasedEdit::slotProcessSpeechError()
{
    m_errorString.append(QString::fromUtf8(m_speechJob->readAllStandardError()));
}

void TextBasedEdit::slotProcessSpeech()
{
    QString saveData = QString::fromUtf8(m_speechJob->readAllStandardOutput());
    qDebug()<<"=== GOT DATA:\n"<<saveData;
    QJsonParseError error;
    auto loadDoc = QJsonDocument::fromJson(saveData.toUtf8(), &error);
    qDebug()<<"===JSON ERROR: "<<error.errorString();
    QTextCursor cursor = m_visualEditor->textCursor();
    if (loadDoc.isObject()) {
        QJsonObject obj = loadDoc.object();
        if (!obj.isEmpty()) {
            //QString itemText = obj["text"].toString();
            QString htmlLine;
            QPair <double, double>sentenceZone;
            if (obj["result"].isArray()) {
                QJsonArray obj2 = obj["result"].toArray();
                // Store words with their start/end time
                foreach (const QJsonValue & v, obj2) {
                    htmlLine.append(QString("<a href=\"%1#%2:%3\">%4</a> ").arg(m_binId).arg(v.toObject().value("start").toDouble()).arg(v.toObject().value("end").toDouble()).arg(v.toObject().value("word").toString()));
                }
                // Get start time for first word
                QJsonValue val = obj2.first();
                if (val.isObject() && val.toObject().keys().contains("start")) {
                    double ms = val.toObject().value("start").toDouble() + m_visualEditor->clipOffset;
                    GenTime startPos(ms);
                    sentenceZone.first = ms;
                    if (startPos.frames(pCore->getCurrentFps()) > m_lastPosition + 1) {
                        // Insert space
                        GenTime silenceStart(m_lastPosition, pCore->getCurrentFps());
                        m_visualEditor->moveCursor(QTextCursor::End);
                        QString htmlSpace = QString("<a href=\"#%1:%2\">%3</a>").arg(silenceStart.seconds()).arg(GenTime(startPos.frames(pCore->getCurrentFps()) - 1, pCore->getCurrentFps()).seconds()).arg(i18n("No speech"));
                        m_visualEditor->insertHtml(htmlSpace);
                        m_visualEditor->textCursor().insertBlock(cursor.blockFormat());
                        m_visualEditor->speechZones << QPair<double, double>(silenceStart.seconds(), GenTime(startPos.frames(pCore->getCurrentFps()) - 1, pCore->getCurrentFps()).seconds());
                    }
                    val = obj2.last();
                    if (val.isObject() && val.toObject().keys().contains("end")) {
                        double ms = val.toObject().value("end").toDouble();
                        sentenceZone.second = ms + m_visualEditor->clipOffset;
                        m_lastPosition = GenTime(ms + m_visualEditor->clipOffset).frames(pCore->getCurrentFps());
                        if (m_clipDuration > 0.) {
                            speech_progress->setValue(static_cast<int>(100 * ms / m_clipDuration));
                        }
                    }
                }
            } else {
                // Last empty object - no speech detected
                GenTime silenceStart(m_lastPosition + 1, pCore->getCurrentFps());
                m_visualEditor->moveCursor(QTextCursor::End);
                QString htmlSpace = QString("<a href=\"#%1:%2\">%3</a>").arg(silenceStart.seconds()).arg(GenTime(m_clipDuration).seconds()).arg(i18n("No speech"));
                m_visualEditor->insertHtml(htmlSpace);
                m_visualEditor->speechZones << QPair<double, double>(silenceStart.seconds(), GenTime(m_clipDuration).seconds());
            }
            if (!htmlLine.isEmpty()) {
                m_visualEditor->insertHtml(htmlLine.simplified());
                if (sentenceZone.second < m_visualEditor->clipOffset + m_clipDuration) {
                    m_visualEditor->textCursor().insertBlock(cursor.blockFormat());
                }
                m_visualEditor->speechZones << sentenceZone;
            }
        }
    } else if (loadDoc.isEmpty()) {
        qDebug()<<"==== EMPTY OBJEC DOC";
    }
    qDebug()<<"==== GOT BLOCKS: "<<m_document.blockCount();
    qDebug()<<"=== LINES: "<<m_document.firstBlock().lineCount();
    m_visualEditor->repaintLines();
}

void TextBasedEdit::parseVoskDictionaries()
{
    QString modelDirectory = KdenliveSettings::vosk_folder_path();
    QDir dir;
    if (modelDirectory.isEmpty()) {
        modelDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        dir = QDir(modelDirectory);
        if (!dir.cd(QStringLiteral("speechmodels"))) {
            qDebug()<<"=== /// CANNOT ACCESS SPEECH DICTIONARIES FOLDER";
            pCore->voskModelUpdate({});
            return;
        }
    } else {
        dir = QDir(modelDirectory);
    }
    QStringList dicts = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QStringList final;
    for (auto &d : dicts) {
        QDir sub(dir.absoluteFilePath(d));
        if (sub.exists(QStringLiteral("mfcc.conf")) || (sub.exists(QStringLiteral("conf/mfcc.conf")))) {
            final << d;
        }
    }
    pCore->voskModelUpdate(final);
}

void TextBasedEdit::deleteItem()
{
    QTextCursor cursor = m_visualEditor->textCursor();
    int start = cursor.selectionStart();
    int end = cursor.selectionEnd();
    qDebug()<<"=== CUTTONG: "<<start<<" - "<<end;
    if (end > start) {
        cursor.setPosition(start);
        cursor.select(QTextCursor::WordUnderCursor);
        while (cursor.selectedText().isEmpty() && start < end) {
            start++;
            cursor.setPosition(start);
            cursor.select(QTextCursor::WordUnderCursor);
        }
        qDebug()<<"=== FINAL START CUT: "<<start;
        int selStart = cursor.selectionStart();
        int selEnd = cursor.selectionEnd();
        cursor.setPosition(selStart + (selEnd - selStart) / 2);
        QString anchorStart = m_visualEditor->anchorAt(m_visualEditor->cursorRect(cursor).center());
        qDebug()<<"=== GOT START ANCHOR: "<<cursor.selectedText()<<" = "<<anchorStart ;
        cursor.setPosition(end);
        cursor.select(QTextCursor::WordUnderCursor);
        while (cursor.selectedText().isEmpty() && end > start) {
            end--;
            cursor.setPosition(end);
            cursor.select(QTextCursor::WordUnderCursor);
        }
        selStart = cursor.selectionStart();
        selEnd = cursor.selectionEnd();
        cursor.setPosition(selStart + (selEnd - selStart) / 2);
        QString anchorEnd = m_visualEditor->anchorAt(m_visualEditor->cursorRect(cursor).center());
        qDebug()<<"=== FINAL END CUT: "<<end;
        qDebug()<<"=== GOT END ANCHOR: "<<cursor.selectedText()<<" = "<<anchorEnd;
        if (!anchorEnd.isEmpty() && !anchorEnd.isEmpty()) {
            double startMs = anchorStart.section(QLatin1Char('#'), 1).section(QLatin1Char(':'), 0, 0).toDouble() + m_visualEditor->clipOffset;
            double endMs = anchorEnd.section(QLatin1Char('#'), 1).section(QLatin1Char(':'), 1, 1).toDouble() + m_visualEditor->clipOffset;
            if (startMs < endMs) {
                qDebug()<<"=== GOT CUT ZONE: "<<GenTime(startMs).frames(pCore->getCurrentFps())<<" - "<<GenTime(endMs).frames(pCore->getCurrentFps());
                m_visualEditor->cutZones << QPoint(GenTime(startMs).frames(pCore->getCurrentFps()), GenTime(endMs).frames(pCore->getCurrentFps()));
                cursor = m_visualEditor->textCursor();
                cursor.removeSelectedText();
            }
        }
    } else {
        QTextCursor curs = m_visualEditor->textCursor();
        curs.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
        for (int i = 0; i < m_document.blockCount(); ++i) {
            int blockStart = curs.position();
            curs.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
            int blockEnd = curs.position();
            if (blockStart == blockEnd) {
                // Empty block, delete
                curs.select(QTextCursor::BlockUnderCursor);
                curs.removeSelectedText();
                curs.deleteChar();
            }
            curs.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor);
        }
    }
    // Reset selection and rebuild line numbers
    m_visualEditor->rebuildZones();
}

void TextBasedEdit::insertToTimeline()
{
    QVector<QPoint> zones = m_visualEditor->getInsertZones();
    if (zones.isEmpty()) {
        return;
    }
    for (auto &zone : zones) {
        pCore->window()->getMainTimeline()->controller()->insertZone(m_binId, zone, false);
    }
}

void TextBasedEdit::previewPlaylist()
{
    QVector<QPoint> zones = m_visualEditor->getInsertZones();
    if (!m_playlist.open()) {
        // Something went wrong
        showMessage(i18n("Cannot open temporary playlist"), KMessageWidget::Information);
        return;
    }
    m_playlist.close();
    if (zones.isEmpty()) {
        showMessage(i18n("No text to export"), KMessageWidget::Information);
        return;
    }
    QMap<QString, QString> properties;
    properties.insert("kdenlive:speech", m_visualEditor->toHtml());
    std::shared_ptr<AbstractProjectItem> clip = pCore->projectItemModel()->getItemByBinId(m_binId);
    std::shared_ptr<ProjectClip> clipItem = std::static_pointer_cast<ProjectClip>(clip);
    /*QString sourcePath = clipItem->url();
    int ix = 1;
    QString playlistPath = QString("%1-cut%2.mlt").arg(sourcePath).arg(ix);
    while (QFile::exists(playlistPath)) {
        ix++;
        playlistPath = QString("%1-cut%2.mlt").arg(sourcePath).arg(ix);
    }*/
    pCore->bin()->savePlaylist(m_binId, m_playlist.fileName(), zones, properties);
    emit previewClip(m_playlist.fileName(), i18n("Speech cut"));
    //slotItemDropped({QUrl::fromLocalFile(playlistPath)}, m_proxyModel->mapToSource(m_proxyModel->selectionModel()->currentIndex()));
}

void TextBasedEdit::showMessage(const QString &text, KMessageWidget::MessageType type)
{
    if (info_message->isVisible()) {
        m_hideTimer.stop();
    }
    info_message->setMessageType(type);
    info_message->setText(text);
    info_message->animatedShow();
    if (type != KMessageWidget::Error) {
        m_hideTimer.start();
    }
}
