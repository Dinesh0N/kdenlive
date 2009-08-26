/***************************************************************************
                          titlewidget.cpp  -  description
                             -------------------
    begin                : Feb 28 2008
    copyright            : (C) 2008 by Marco Gittler
    email                : g.marco@freenet.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "titlewidget.h"
#include "kdenlivesettings.h"

#include <cmath>

#include <KDebug>
#include <KGlobalSettings>
#include <KFileDialog>
#include <KStandardDirs>
#include <KMessageBox>
#include <kio/netaccess.h>

#include <QDomDocument>
#include <QGraphicsItem>
#include <QGraphicsSvgItem>
#include <QTimer>
#include <QToolBar>
#include <QMenu>
#include <QSignalMapper>
#include <QTextBlockFormat>
#include <QTextCursor>

int settingUp = false;

const int IMAGEITEM = 7;
const int RECTITEM = 3;
const int TEXTITEM = 8;
static bool insertingValues = false;

TitleWidget::TitleWidget(KUrl url, Timecode tc, QString projectTitlePath, Render *render, QWidget *parent) :
        QDialog(parent),
        Ui::TitleWidget_UI(),
        m_startViewport(NULL),
        m_endViewport(NULL),
        m_render(render),
        m_count(0),
        m_unicodeDialog(new UnicodeDialog(UnicodeDialog::InputHex)),
        m_projectTitlePath(projectTitlePath),
        m_tc(tc)
{
    setupUi(this);
    setFont(KGlobalSettings::toolBarFont());
    //toolBox->setFont(KGlobalSettings::toolBarFont());
    frame_properties->setEnabled(false);
    rect_properties->setFixedHeight(frame_properties->height() + 4);
    no_properties->setFixedHeight(frame_properties->height() + 4);
    image_properties->setFixedHeight(frame_properties->height() + 4);
    text_properties->setFixedHeight(frame_properties->height() + 4);
    frame_properties->setFixedHeight(frame_toolbar->height());
    m_frameWidth = render->renderWidth();
    m_frameHeight = render->renderHeight();
    showToolbars(TITLE_NONE);

    //TODO: get default title duration instead of hardcoded one
    title_duration->setText(m_tc.getTimecode(GenTime(5000 / 1000.0)));

    connect(kcolorbutton, SIGNAL(clicked()), this, SLOT(slotChangeBackground())) ;
    connect(horizontalSlider, SIGNAL(valueChanged(int)), this, SLOT(slotChangeBackground())) ;

    connect(fontColorButton, SIGNAL(clicked()), this, SLOT(slotUpdateText())) ;
    connect(font_family, SIGNAL(currentFontChanged(const QFont &)), this, SLOT(slotUpdateText())) ;
    connect(font_size, SIGNAL(valueChanged(int)), this, SLOT(slotUpdateText())) ;
    connect(textAlpha, SIGNAL(valueChanged(int)), this, SLOT(slotUpdateText()));
    connect(font_weight_box, SIGNAL(currentIndexChanged(int)), this, SLOT(slotUpdateText()));

    connect(rectFAlpha, SIGNAL(valueChanged(int)), this, SLOT(rectChanged()));
    connect(rectBAlpha, SIGNAL(valueChanged(int)), this, SLOT(rectChanged()));
    connect(rectFColor, SIGNAL(clicked()), this, SLOT(rectChanged()));
    connect(rectBColor, SIGNAL(clicked()), this, SLOT(rectChanged()));
    connect(rectLineWidth, SIGNAL(valueChanged(int)), this, SLOT(rectChanged()));

    connect(startViewportX, SIGNAL(valueChanged(int)), this, SLOT(setupViewports()));
    connect(startViewportY, SIGNAL(valueChanged(int)), this, SLOT(setupViewports()));
    connect(startViewportSize, SIGNAL(valueChanged(int)), this, SLOT(setupViewports()));
    connect(endViewportX, SIGNAL(valueChanged(int)), this, SLOT(setupViewports()));
    connect(endViewportY, SIGNAL(valueChanged(int)), this, SLOT(setupViewports()));
    connect(endViewportSize, SIGNAL(valueChanged(int)), this, SLOT(setupViewports()));

    connect(zValue, SIGNAL(valueChanged(int)), this, SLOT(zIndexChanged(int)));
    connect(itemzoom, SIGNAL(valueChanged(int)), this, SLOT(itemScaled(int)));
    connect(itemrotate, SIGNAL(valueChanged(int)), this, SLOT(itemRotate(int)));
    connect(itemhcenter, SIGNAL(clicked()), this, SLOT(itemHCenter()));
    connect(itemvcenter, SIGNAL(clicked()), this, SLOT(itemVCenter()));

    connect(origin_x_left, SIGNAL(clicked()), this, SLOT(slotOriginXClicked()));
    connect(origin_y_top, SIGNAL(clicked()), this, SLOT(slotOriginYClicked()));

    m_signalMapper = new QSignalMapper(this);
    m_signalMapper->setMapping(value_w, ValueWidth);
    m_signalMapper->setMapping(value_h, ValueHeight);
    connect(value_w, SIGNAL(valueChanged(int)), m_signalMapper, SLOT(map()));
    connect(value_h, SIGNAL(valueChanged(int)), m_signalMapper, SLOT(map()));
    connect(m_signalMapper, SIGNAL(mapped(int)), this, SLOT(slotValueChanged(int)));

    connect(value_x, SIGNAL(valueChanged(int)), this, SLOT(slotAdjustSelectedItem()));
    connect(value_y, SIGNAL(valueChanged(int)), this, SLOT(slotAdjustSelectedItem()));
    connect(value_w, SIGNAL(valueChanged(int)), this, SLOT(slotAdjustSelectedItem()));
    connect(value_h, SIGNAL(valueChanged(int)), this, SLOT(slotAdjustSelectedItem()));
    connect(buttonFitZoom, SIGNAL(clicked()), this, SLOT(slotAdjustZoom()));
    connect(buttonRealSize, SIGNAL(clicked()), this, SLOT(slotZoomOneToOne()));
    connect(buttonItalic, SIGNAL(clicked()), this, SLOT(slotUpdateText()));
    connect(buttonUnder, SIGNAL(clicked()), this, SLOT(slotUpdateText()));
    connect(buttonAlignLeft, SIGNAL(clicked()), this, SLOT(slotUpdateText()));
    connect(buttonAlignRight, SIGNAL(clicked()), this, SLOT(slotUpdateText()));
    connect(buttonAlignCenter, SIGNAL(clicked()), this, SLOT(slotUpdateText()));
    connect(buttonAlignNone, SIGNAL(clicked()), this, SLOT(slotUpdateText()));
    //connect(buttonInsertUnicode, SIGNAL(clicked()), this, SLOT(slotInsertUnicode()));
    connect(displayBg, SIGNAL(stateChanged(int)), this, SLOT(displayBackgroundFrame()));

    connect(m_unicodeDialog, SIGNAL(charSelected(QString)), this, SLOT(slotInsertUnicodeString(QString)));

    // mbd
    connect(this, SIGNAL(accepted()), this, SLOT(slotAccepted()));

    font_weight_box->blockSignals(true);
    font_weight_box->addItem(i18nc("Font style", "Light"), QFont::Light);
    font_weight_box->addItem(i18nc("Font style", "Normal"), QFont::Normal);
    font_weight_box->addItem(i18nc("Font style", "Demi-Bold"), QFont::DemiBold);
    font_weight_box->addItem(i18nc("Font style", "Bold"), QFont::Bold);
    font_weight_box->addItem(i18nc("Font style", "Black"), QFont::Black);
    font_weight_box->setToolTip(i18n("Font weight"));
    font_weight_box->setCurrentIndex(1);
    font_weight_box->blockSignals(false);

    buttonFitZoom->setIcon(KIcon("zoom-fit-best"));
    buttonRealSize->setIcon(KIcon("zoom-original"));
    buttonItalic->setIcon(KIcon("format-text-italic"));
    buttonUnder->setIcon(KIcon("format-text-underline"));
    buttonAlignCenter->setIcon(KIcon("format-justify-center"));
    buttonAlignLeft->setIcon(KIcon("format-justify-left"));
    buttonAlignRight->setIcon(KIcon("format-justify-right"));
    buttonAlignNone->setIcon(KIcon("kdenlive-align-none"));

    buttonAlignNone->setToolTip(i18n("No alignment"));
    buttonAlignRight->setToolTip(i18n("Align right"));
    buttonAlignLeft->setToolTip(i18n("Align left"));
    buttonAlignCenter->setToolTip(i18n("Align center"));

    m_unicodeAction = new QAction(KIcon("kdenlive-insert-unicode"), QString(), this);
    m_unicodeAction->setShortcut(Qt::SHIFT + Qt::CTRL + Qt::Key_U);
    m_unicodeAction->setToolTip(i18n("Insert Unicode character") + ' ' + m_unicodeAction->shortcut().toString());
    connect(m_unicodeAction, SIGNAL(triggered()), this, SLOT(slotInsertUnicode()));
    buttonInsertUnicode->setDefaultAction(m_unicodeAction);

    origin_x_left->setToolTip(i18n("Invert x axis and change 0 point"));
    origin_y_top->setToolTip(i18n("Invert y axis and change 0 point"));
    rectBColor->setToolTip(i18n("Select fill color"));
    rectFColor->setToolTip(i18n("Select border color"));
    rectBAlpha->setToolTip(i18n("Fill transparency"));
    rectFAlpha->setToolTip(i18n("Border transparency"));
    zoom_slider->setToolTip(i18n("Zoom"));
    buttonRealSize->setToolTip(i18n("Original size (1:1)"));
    buttonFitZoom->setToolTip(i18n("Fit zoom"));
    kcolorbutton->setToolTip(i18n("Select background color"));
    horizontalSlider->setToolTip(i18n("Background Transparency"));

    itemhcenter->setIcon(KIcon("kdenlive-align-hor"));
    itemhcenter->setToolTip(i18n("Align item horizontally"));
    itemvcenter->setIcon(KIcon("kdenlive-align-vert"));
    itemvcenter->setToolTip(i18n("Align item vertically"));

    QHBoxLayout *layout = new QHBoxLayout;
    frame_toolbar->setLayout(layout);
    layout->setContentsMargins(2, 2, 2, 2);
    QToolBar *m_toolbar = new QToolBar("titleToolBar", this);

    m_buttonCursor = m_toolbar->addAction(KIcon("transform-move"), QString());
    m_buttonCursor->setCheckable(true);
    m_buttonCursor->setShortcut(Qt::ALT + Qt::Key_S);
    m_buttonCursor->setToolTip(i18n("Selection Tool") + ' ' + m_buttonCursor->shortcut().toString());
    connect(m_buttonCursor, SIGNAL(triggered()), this, SLOT(slotSelectTool()));

    m_buttonText = m_toolbar->addAction(KIcon("insert-text"), QString());
    m_buttonText->setCheckable(true);
    m_buttonText->setShortcut(Qt::ALT + Qt::Key_T);
    m_buttonText->setToolTip(i18n("Add Text") + ' ' + m_buttonText->shortcut().toString());
    connect(m_buttonText, SIGNAL(triggered()), this, SLOT(slotTextTool()));

    m_buttonRect = m_toolbar->addAction(KIcon("kdenlive-insert-rect"), QString());
    m_buttonRect->setCheckable(true);
    m_buttonRect->setShortcut(Qt::ALT + Qt::Key_R);
    m_buttonRect->setToolTip(i18n("Add Rectangle") + ' ' + m_buttonRect->shortcut().toString());
    connect(m_buttonRect, SIGNAL(triggered()), this, SLOT(slotRectTool()));

    m_buttonImage = m_toolbar->addAction(KIcon("insert-image"), QString());
    m_buttonImage->setCheckable(false);
    m_buttonImage->setShortcut(Qt::ALT + Qt::Key_I);
    m_buttonImage->setToolTip(i18n("Add Image") + ' ' + m_buttonImage->shortcut().toString());
    connect(m_buttonImage, SIGNAL(triggered()), this, SLOT(slotImageTool()));

    m_toolbar->addSeparator();

    m_buttonLoad = m_toolbar->addAction(KIcon("document-open"), i18n("Open Document"));
    m_buttonLoad->setCheckable(false);
    m_buttonLoad->setShortcut(Qt::CTRL + Qt::Key_O);
    connect(m_buttonLoad, SIGNAL(triggered()), this, SLOT(loadTitle()));

    m_buttonSave = m_toolbar->addAction(KIcon("document-save-as"), i18n("Save As"));
    m_buttonSave->setCheckable(false);
    m_buttonSave->setShortcut(Qt::CTRL + Qt::Key_S);
    connect(m_buttonSave, SIGNAL(triggered()), this, SLOT(saveTitle()));

    layout->addWidget(m_toolbar);

    // initialize graphic scene
    m_scene = new GraphicsSceneRectMove(this);
    graphicsView->setScene(m_scene);
    m_titledocument.setScene(m_scene, m_frameWidth, m_frameHeight);
    connect(m_scene, SIGNAL(changed(QList<QRectF>)), this, SLOT(slotChanged()));

    // a gradient background
    /*QRadialGradient *gradient = new QRadialGradient(0, 0, 10);
    gradient->setSpread(QGradient::ReflectSpread);
    scene->setBackgroundBrush(*gradient);*/

    m_frameImage = new QGraphicsPixmapItem();
    QTransform qtrans;
    qtrans.scale(2.0, 2.0);
    m_frameImage->setTransform(qtrans);
    m_frameImage->setZValue(-1200);
    m_frameImage->setFlags(QGraphicsItem::ItemClipsToShape);
    displayBackgroundFrame();
    graphicsView->scene()->addItem(m_frameImage);

    connect(m_scene, SIGNAL(selectionChanged()), this , SLOT(selectionChanged()));
    connect(m_scene, SIGNAL(itemMoved()), this , SLOT(selectionChanged()));
    connect(m_scene, SIGNAL(sceneZoom(bool)), this , SLOT(slotZoom(bool)));
    connect(m_scene, SIGNAL(actionFinished()), this , SLOT(slotSelectTool()));
    //connect(m_scene, SIGNAL(actionFinished()), this , SLOT(selectionChanged()));
    connect(m_scene, SIGNAL(newRect(QGraphicsRectItem *)), this , SLOT(slotNewRect(QGraphicsRectItem *)));
    connect(m_scene, SIGNAL(newText(QGraphicsTextItem *)), this , SLOT(slotNewText(QGraphicsTextItem *)));
    connect(zoom_slider, SIGNAL(valueChanged(int)), this , SLOT(slotUpdateZoom(int)));

    QPen framepen(Qt::DotLine);
    framepen.setColor(Qt::red);

    m_frameBorder = new QGraphicsRectItem(QRectF(0, 0, m_frameWidth, m_frameHeight));
    m_frameBorder->setPen(framepen);
    m_frameBorder->setZValue(-1100);
    m_frameBorder->setBrush(Qt::transparent);
    m_frameBorder->setFlags(QGraphicsItem::ItemClipsToShape);
    graphicsView->scene()->addItem(m_frameBorder);

    // mbd: load saved settings
    readChoices();

    initViewports();
    graphicsView->show();
    //graphicsView->setRenderHint(QPainter::Antialiasing);
    graphicsView->setInteractive(true);
    //graphicsView->resize(400, 300);
    kDebug() << "// TITLE WIDGWT: " << graphicsView->viewport()->width() << "x" << graphicsView->viewport()->height();
    //toolBox->setItemEnabled(2, false);
    if (!url.isEmpty()) loadTitle(url);
    else {
        slotTextTool();
        QTimer::singleShot(200, this, SLOT(slotAdjustZoom()));
    }
}

TitleWidget::~TitleWidget()
{
    delete m_buttonRect;
    delete m_buttonText;
    delete m_buttonImage;
    delete m_buttonCursor;
    delete m_buttonSave;
    delete m_buttonLoad;
    delete m_unicodeAction;

    delete m_unicodeDialog;
    delete m_frameBorder;
    delete m_frameImage;
    delete m_startViewport;
    delete m_endViewport;
    delete m_scene;
    delete m_signalMapper;
}

//static
QStringList TitleWidget::getFreeTitleInfo(const KUrl &projectUrl, bool isClone)
{
    QStringList result;
    QString titlePath = projectUrl.path(KUrl::AddTrailingSlash) + "titles/";
    KStandardDirs::makeDir(titlePath);
    titlePath.append((isClone == false) ? "title" : "clone");
    int counter = 0;
    QString path;
    while (path.isEmpty() || QFile::exists(path)) {
        counter++;
        path = titlePath + QString::number(counter).rightJustified(3, '0', false) + ".png";
    }
    result.append(((isClone == false) ? i18n("Title") : i18n("Clone")) + ' ' + QString::number(counter).rightJustified(3, '0', false));
    result.append(path);
    return result;
}

QString TitleWidget::getTitleResourceFromName(const KUrl &projectUrl, const QString &titleName)
{
    QStringList result;
    QString titlePath = projectUrl.path(KUrl::AddTrailingSlash) + "titles/";
    KStandardDirs::makeDir(titlePath);
    return titlePath + titleName + ".png";
}

//virtual
void TitleWidget::resizeEvent(QResizeEvent * /*event*/)
{
    //slotAdjustZoom();
}

void TitleWidget::slotTextTool()
{
    m_scene->setTool(TITLE_TEXT);
    showToolbars(TITLE_TEXT);
    checkButton(TITLE_TEXT);
}

void TitleWidget::slotRectTool()
{
    m_scene->setTool(TITLE_RECTANGLE);
    showToolbars(TITLE_RECTANGLE);
    checkButton(TITLE_RECTANGLE);
}

void TitleWidget::slotSelectTool()
{
    m_scene->setTool(TITLE_SELECT);

    // Find out which toolbars need to be shown, depending on selected item
    TITLETOOL t = TITLE_SELECT;
    QList<QGraphicsItem *> l = graphicsView->scene()->selectedItems();
    if (l.size() > 0) {
        switch (l.at(0)->type()) {
        case TEXTITEM:
            t = TITLE_TEXT;
            break;
        case RECTITEM:
            t = TITLE_RECTANGLE;
            break;
        case IMAGEITEM:
            t = TITLE_IMAGE;
            break;
        }
    }
    enableToolbars(t);
    showToolbars(t);

    if (l.size() > 0) {
        updateCoordinates(l.at(0));
        updateDimension(l.at(0));
        updateRotZoom(l.at(0));
    }

    checkButton(TITLE_SELECT);
}

void TitleWidget::slotImageTool()
{
    KUrl url = KFileDialog::getOpenUrl(KUrl(), "*.svg *.png *.jpg *.jpeg *.gif *.raw", this, i18n("Load Image"));
    if (!url.isEmpty()) {
        if (url.path().endsWith(".svg")) {
            QGraphicsSvgItem *svg = new QGraphicsSvgItem(url.toLocalFile());
            svg->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
            svg->setZValue(m_count++);
            svg->setData(Qt::UserRole, url.path());
            graphicsView->scene()->addItem(svg);
        } else {
            QPixmap pix(url.path());
            QGraphicsPixmapItem *image = new QGraphicsPixmapItem(pix);
            image->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
            image->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
            image->setData(Qt::UserRole, url.path());
            image->setZValue(m_count++);
            graphicsView->scene()->addItem(image);
        }
    }
    m_scene->setTool(TITLE_SELECT);
    showToolbars(TITLE_SELECT);
    checkButton(TITLE_NONE);
}

void TitleWidget::showToolbars(TITLETOOL toolType)
{
    switch (toolType) {
    case TITLE_TEXT:
        rect_properties->setHidden(true);
        image_properties->setHidden(true);
        no_properties->setHidden(true);
        text_properties->setHidden(false);
        break;
    case TITLE_RECTANGLE:
        image_properties->setHidden(true);
        no_properties->setHidden(true);
        text_properties->setHidden(true);
        rect_properties->setHidden(false);
        break;
    case TITLE_IMAGE:
        no_properties->setHidden(true);
        text_properties->setHidden(true);
        rect_properties->setHidden(true);
        image_properties->setHidden(false);
        break;
    default:
        text_properties->setHidden(true);
        rect_properties->setHidden(true);
        image_properties->setHidden(true);
        no_properties->setHidden(false);
        break;
    }
}

void TitleWidget::enableToolbars(TITLETOOL toolType)
{
    // TITLETOOL is defined in graphicsscenerectmove.h
    bool bFrame = false;
    bool bText = false;
    bool bRect = false;
    bool bImage = false;
    bool bValue_w = false;
    bool bValue_h = false;

    switch (toolType) {
    case TITLE_SELECT:
        break;
    case TITLE_TEXT:
        bFrame = true;
        bText = true;
        break;
    case TITLE_RECTANGLE:
        bFrame = true;
        bRect = true;
        bValue_w = true;
        bValue_h = true;
        break;
    case TITLE_IMAGE:
        bFrame = true;
        bValue_w = true;
        bValue_h = true;
        bImage = true;
        break;
    default:
        break;
    }
    frame_properties->setEnabled(bFrame);
    text_properties->setEnabled(bText);
    rect_properties->setEnabled(bRect);
    image_properties->setEnabled(bImage);
    value_w->setEnabled(bValue_w);
    value_h->setEnabled(bValue_h);
}

void TitleWidget::checkButton(TITLETOOL toolType)
{
    bool bSelect = false;
    bool bText = false;
    bool bRect = false;
    bool bImage = false;

    switch (toolType) {
    case TITLE_SELECT:
        bSelect = true;
        break;
    case TITLE_TEXT:
        bText = true;
        break;
    case TITLE_RECTANGLE:
        bRect = true;
        break;
    case TITLE_IMAGE:
        bImage = true;
        break;
    case TITLE_NONE:
        break;
    }

    m_buttonCursor->setChecked(bSelect);
    m_buttonText->setChecked(bText);
    m_buttonRect->setChecked(bRect);
    m_buttonImage->setChecked(bImage);
}

void TitleWidget::displayBackgroundFrame()
{
    if (!displayBg->isChecked()) {
        QPixmap bg(m_frameWidth / 2, m_frameHeight / 2);
        QPixmap pattern(20, 20);
        pattern.fill();
        QColor bgcolor(210, 210, 210);
        QPainter p;
        p.begin(&pattern);
        p.fillRect(QRect(0, 0, 10, 10), bgcolor);
        p.fillRect(QRect(10, 10, 20, 20), bgcolor);
        p.end();
        QBrush br(pattern);

        p.begin(&bg);
        p.fillRect(bg.rect(), br);
        p.end();
        m_frameImage->setPixmap(bg);
    } else {
        m_frameImage->setPixmap(m_render->extractFrame((int) m_render->seekPosition().frames(m_render->fps()), m_frameWidth / 2, m_frameHeight / 2));
    }
}

void TitleWidget::initViewports()
{
    m_startViewport = new QGraphicsPolygonItem(QPolygonF(QRectF(0, 0, 0, 0)));
    m_endViewport = new QGraphicsPolygonItem(QPolygonF(QRectF(0, 0, 0, 0)));

    QPen startpen(Qt::DotLine);
    QPen endpen(Qt::DashDotLine);
    startpen.setColor(QColor(100, 200, 100, 140));
    endpen.setColor(QColor(200, 100, 100, 140));

    m_startViewport->setPen(startpen);
    m_endViewport->setPen(endpen);

    startViewportSize->setValue(100);
    endViewportSize->setValue(100);

    m_startViewport->setZValue(-1000);
    m_endViewport->setZValue(-1000);

    m_startViewport->setFlags(/*QGraphicsItem::ItemIsMovable|*/QGraphicsItem::ItemIsSelectable);
    m_endViewport->setFlags(/*QGraphicsItem::ItemIsMovable|*/QGraphicsItem::ItemIsSelectable);

    graphicsView->scene()->addItem(m_startViewport);
    graphicsView->scene()->addItem(m_endViewport);
}

void TitleWidget::slotUpdateZoom(int pos)
{
    m_scene->setZoom((double) pos / 100);
    zoom_label->setText(QString::number(pos) + '%');
}

void TitleWidget::slotZoom(bool up)
{
    int pos = zoom_slider->value();
    if (up) pos++;
    else pos--;
    zoom_slider->setValue(pos);
}

void TitleWidget::slotAdjustZoom()
{
    /*double scalex = graphicsView->width() / (double)(m_frameWidth * 1.2);
    double scaley = graphicsView->height() / (double)(m_frameHeight * 1.2);
    if (scalex > scaley) scalex = scaley;
    int zoompos = (int)(scalex * 7 + 0.5);*/
    graphicsView->fitInView(m_frameBorder, Qt::KeepAspectRatio);
    int zoompos = graphicsView->matrix().m11() * 100;
    zoom_slider->setValue(zoompos);
    graphicsView->centerOn(m_frameBorder);
}

void TitleWidget::slotZoomOneToOne()
{
    zoom_slider->setValue(100);
    graphicsView->centerOn(m_frameBorder);
}

void TitleWidget::slotNewRect(QGraphicsRectItem * rect)
{
    updateAxisButtons(rect); // back to default

    QColor f = rectFColor->color();
    f.setAlpha(rectFAlpha->value());
    QPen penf(f);
    penf.setWidth(rectLineWidth->value());
    rect->setPen(penf);
    QColor b = rectBColor->color();
    b.setAlpha(rectBAlpha->value());
    rect->setBrush(QBrush(b));
    rect->setZValue(m_count++);
    //setCurrentItem(rect);
    //graphicsView->setFocus();
}

void TitleWidget::slotNewText(QGraphicsTextItem *tt)
{
    updateAxisButtons(tt); // back to default

    QFont font = font_family->currentFont();
    font.setPixelSize(font_size->value());
    // mbd: issue 551:
    font.setWeight(font_weight_box->itemData(font_weight_box->currentIndex()).toInt());
    font.setItalic(buttonItalic->isChecked());
    font.setUnderline(buttonUnder->isChecked());

    tt->setFont(font);
    QColor color = fontColorButton->color();
    color.setAlpha(textAlpha->value());
    tt->setDefaultTextColor(color);
    tt->setZValue(m_count++);
    setCurrentItem(tt);
}

void TitleWidget::setFontBoxWeight(int weight)
{
    int index = font_weight_box->findData(weight);
    if (index < 0) {
        index = font_weight_box->findData(QFont::Normal);
    }
    font_weight_box->setCurrentIndex(index);
}

void TitleWidget::setCurrentItem(QGraphicsItem *item)
{
    m_scene->setSelectedItem(item);
}

void TitleWidget::zIndexChanged(int v)
{
    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() >= 1) {
        l[0]->setZValue(v);
    }
}

void TitleWidget::selectionChanged()
{
    if (m_scene->tool() != TITLE_SELECT) return;
    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    //toolBox->setItemEnabled(2, false);
    //toolBox->setItemEnabled(3, false);
    value_x->blockSignals(true);
    value_y->blockSignals(true);
    value_w->blockSignals(true);
    value_h->blockSignals(true);
    itemzoom->blockSignals(true);
    itemrotate->blockSignals(true);
    if (l.size() == 0) {
        bool blockX = !origin_x_left->signalsBlocked();
        bool blockY = !origin_y_top->signalsBlocked();
        if (blockX) origin_x_left->blockSignals(true);
        if (blockY) origin_y_top->blockSignals(true);
        origin_x_left->setChecked(false);
        origin_y_top->setChecked(false);
        updateTextOriginX();
        updateTextOriginY();
        enableToolbars(TITLE_NONE);
        if (blockX) origin_x_left->blockSignals(false);
        if (blockY) origin_y_top->blockSignals(false);
    } else if (l.size() == 1) {
        if (l.at(0)->type() == TEXTITEM) {
            showToolbars(TITLE_TEXT);
            QGraphicsTextItem* i = static_cast <QGraphicsTextItem *>(l.at(0));
            //if (l[0]->hasFocus())
            toolBox->setCurrentIndex(0);
            //toolBox->setItemEnabled(2, true);
            font_size->blockSignals(true);
            font_family->blockSignals(true);
            font_weight_box->blockSignals(true);
            buttonItalic->blockSignals(true);
            buttonUnder->blockSignals(true);
            fontColorButton->blockSignals(true);
            textAlpha->blockSignals(true);
            buttonAlignLeft->blockSignals(true);
            buttonAlignRight->blockSignals(true);
            buttonAlignNone->blockSignals(true);
            buttonAlignCenter->blockSignals(true);

            QFont font = i->font();
            font_family->setCurrentFont(font);
            font_size->setValue(font.pixelSize());
            buttonItalic->setChecked(font.italic());
            buttonUnder->setChecked(font.underline());
            setFontBoxWeight(font.weight());

            QColor color = i->defaultTextColor();
            fontColorButton->setColor(color);
            textAlpha->setValue(color.alpha());

            QTextCursor cur = i->textCursor();
            QTextBlockFormat format = cur.blockFormat();
            if (i->textWidth() == -1) buttonAlignNone->setChecked(true);
            else if (format.alignment() == Qt::AlignHCenter) buttonAlignCenter->setChecked(true);
            else if (format.alignment() == Qt::AlignRight) buttonAlignRight->setChecked(true);
            else if (format.alignment() == Qt::AlignLeft) buttonAlignLeft->setChecked(true);

            font_size->blockSignals(false);
            font_family->blockSignals(false);
            font_weight_box->blockSignals(false);
            buttonItalic->blockSignals(false);
            buttonUnder->blockSignals(false);
            fontColorButton->blockSignals(false);
            textAlpha->blockSignals(false);
            buttonAlignLeft->blockSignals(false);
            buttonAlignRight->blockSignals(false);
            buttonAlignNone->blockSignals(false);
            buttonAlignCenter->blockSignals(false);

            updateAxisButtons(i);
            updateCoordinates(i);
            updateDimension(i);
            enableToolbars(TITLE_TEXT);

        } else if ((l.at(0))->type() == RECTITEM) {
            showToolbars(TITLE_RECTANGLE);
            settingUp = true;
            QGraphicsRectItem *rec = static_cast <QGraphicsRectItem *>(l.at(0));
            toolBox->setCurrentIndex(0);
            //toolBox->setItemEnabled(3, true);
            rectFAlpha->setValue(rec->pen().color().alpha());
            rectBAlpha->setValue(rec->brush().color().alpha());
            //kDebug() << rec->brush().color().alpha();
            QColor fcol = rec->pen().color();
            QColor bcol = rec->brush().color();
            //fcol.setAlpha(255);
            //bcol.setAlpha(255);
            rectFColor->setColor(fcol);
            rectBColor->setColor(bcol);
            settingUp = false;
            rectLineWidth->setValue(rec->pen().width());

            updateAxisButtons(l.at(0));
            updateCoordinates(rec);
            updateDimension(rec);
            enableToolbars(TITLE_RECTANGLE);

        } else if (l.at(0)->type() == IMAGEITEM) {
            showToolbars(TITLE_IMAGE);

            updateCoordinates(l.at(0));
            updateDimension(l.at(0));

            enableToolbars(TITLE_IMAGE);

        } else {
            //toolBox->setCurrentIndex(0);
            showToolbars(TITLE_NONE);
            enableToolbars(TITLE_NONE);
            /*frame_properties->setEnabled(false);
            text_properties->setEnabled(false);
            rect_properties->setEnabled(false);*/
        }
        zValue->setValue((int)l.at(0)->zValue());
        itemzoom->setValue((int)(m_transformations.value(l.at(0)).scalex * 100.0 + 0.5));
        itemrotate->setValue((int)(m_transformations.value(l.at(0)).rotate));
        value_x->blockSignals(false);
        value_y->blockSignals(false);
        value_w->blockSignals(false);
        value_h->blockSignals(false);
        itemzoom->blockSignals(false);
        itemrotate->blockSignals(false);
    }
}

void TitleWidget::slotValueChanged(int type)
{
    QList<QGraphicsItem *> l = graphicsView->scene()->selectedItems();
    if (l.size() > 0 && l.at(0)->type() == IMAGEITEM) {

        int val = 0;
        switch (type) {
        case ValueWidth:
            val = value_w->value();
            break;
        case ValueHeight:
            val = value_h->value();
            break;
        }

        QGraphicsItem *i = l.at(0);
        Transform t = m_transformations.value(i);

        // Ratio width:height
        double phi = (double) i->boundingRect().width() / i->boundingRect().height();
        double alpha = (double) t.rotate / 180.0 * M_PI;

        // New length
        double length = val;

        // Scaling factor
        double scale = 1;

        switch (type) {
        case ValueWidth:
            // Add 0.5 because otherwise incrementing by 1 might have no effect
            length = val / (cos(alpha) + 1 / phi * sin(alpha)) + 0.5;
            scale = length / i->boundingRect().width();
            break;
        case ValueHeight:
            length = val / (phi * sin(alpha) + cos(alpha)) + 0.5;
            scale = length / i->boundingRect().height();
            break;
        }

        t.scalex = scale;
        t.scaley = scale;
        QTransform qtrans;
        qtrans.scale(scale, scale);
        qtrans.rotate(t.rotate);
        i->setTransform(qtrans);
        m_transformations[i] = t;

        updateDimension(i);
        updateRotZoom(i);
    }
}

/** \brief Updates position/size of the selected item when a value
 * of an item (coordinates, size) has changed */
void TitleWidget::slotAdjustSelectedItem()
{
    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() >= 1) {
        if (l.at(0)->type() == RECTITEM) {
            //rect item
            QGraphicsRectItem *rec = static_cast <QGraphicsRectItem *>(l.at(0));
            updatePosition(rec);
            rec->setRect(QRect(0, 0, value_w->value(), value_h->value()));
        } else if (l.at(0)->type() == TEXTITEM) {
            //text item
            updatePosition(l.at(0));
        } else if (l.at(0)->type() == IMAGEITEM) {
            //image item
            updatePosition(l.at(0));
        }
    }
}

/** \brief Updates width/height int the text fields, regarding transformation matrix */
void TitleWidget::updateDimension(QGraphicsItem *i)
{
    value_w->blockSignals(true);
    value_h->blockSignals(true);


    if (i->type() == IMAGEITEM) {
        // Get multipliers for rotation/scaling

        /*Transform t = m_transformations.value(i);
        QRectF r = i->boundingRect();
        int width = (int) ( abs(r.width()*t.scalex * cos(t.rotate/180.0*M_PI))
                    + abs(r.height()*t.scaley * sin(t.rotate/180.0*M_PI)) );
        int height = (int) ( abs(r.height()*t.scaley * cos(t.rotate/180*M_PI))
                    + abs(r.width()*t.scalex * sin(t.rotate/180*M_PI)) );*/

        value_w->setValue(i->sceneBoundingRect().width());
        value_h->setValue(i->sceneBoundingRect().height());
    } else if (i->type() == RECTITEM) {
        QGraphicsRectItem *r = static_cast <QGraphicsRectItem *>(i);
        value_w->setValue((int) r->rect().width());
        value_h->setValue((int) r->rect().height());
    } else if (i->type() == TEXTITEM) {
        QGraphicsTextItem *t = static_cast <QGraphicsTextItem *>(i);
        value_w->setValue((int) t->boundingRect().width());
        value_h->setValue((int) t->boundingRect().height());
    }

    value_w->blockSignals(false);
    value_h->blockSignals(false);
}

/** \brief Updates the coordinates in the text fields from the item */
void TitleWidget::updateCoordinates(QGraphicsItem *i)
{
    // Block signals emitted by this method
    value_x->blockSignals(true);
    value_y->blockSignals(true);

    if (i->type() == TEXTITEM) {

        QGraphicsTextItem *rec = static_cast <QGraphicsTextItem *>(i);

        // Set the correct x coordinate value
        if (origin_x_left->isChecked()) {
            // Origin (0 point) is at m_frameWidth, coordinate axis is inverted
            value_x->setValue((int)(m_frameWidth - rec->pos().x() - rec->boundingRect().width()));
        } else {
            // Origin is at 0 (default)
            value_x->setValue((int) rec->pos().x());
        }

        // Same for y
        if (origin_y_top->isChecked()) {
            value_y->setValue((int)(m_frameHeight - rec->pos().y() - rec->boundingRect().height()));
        } else {
            value_y->setValue((int) rec->pos().y());
        }

    } else if (i->type() == RECTITEM) {

        QGraphicsRectItem *rec = static_cast <QGraphicsRectItem *>(i);

        if (origin_x_left->isChecked()) {
            // Origin (0 point) is at m_frameWidth
            value_x->setValue((int)(m_frameWidth - rec->pos().x() - rec->rect().width()));
        } else {
            // Origin is at 0 (default)
            value_x->setValue((int) rec->pos().x());
        }

        if (origin_y_top->isChecked()) {
            value_y->setValue((int)(m_frameHeight - rec->pos().y() - rec->rect().height()));
        } else {
            value_y->setValue((int) rec->pos().y());
        }

    } else if (i->type() == IMAGEITEM) {

        if (origin_x_left->isChecked()) {
            value_x->setValue((int)(m_frameWidth - i->pos().x() - i->sceneBoundingRect().width()));
        } else {
            value_x->setValue((int) i->pos().x());
        }

        if (origin_y_top->isChecked()) {
            value_y->setValue((int)(m_frameHeight - i->pos().y() - i->sceneBoundingRect().height()));
        } else {
            value_y->setValue((int) i->pos().y());
        }

    }

    // Stop blocking signals now
    value_x->blockSignals(false);
    value_y->blockSignals(false);
}

void TitleWidget::updateRotZoom(QGraphicsItem *i)
{
    itemzoom->blockSignals(true);
    itemrotate->blockSignals(false);

    Transform t = m_transformations.value(i);
    itemzoom->setValue((int)(t.scalex * 100.0 + 0.5));
    itemrotate->setValue((int)(t.rotate));

    itemzoom->blockSignals(false);
    itemrotate->blockSignals(false);
}

/** \brief Updates the position of an item by reading coordinates from the text fields */
void TitleWidget::updatePosition(QGraphicsItem *i)
{
    if (i->type() == TEXTITEM) {
        QGraphicsTextItem *rec = static_cast <QGraphicsTextItem *>(i);

        int posX;
        if (origin_x_left->isChecked()) {
            /* Origin of the x axis is at m_frameWidth,
             * and distance from right border of the item to the right
             * border of the frame is taken.
             * See comment to slotOriginXClicked().
             */
            posX = m_frameWidth - value_x->value() - rec->boundingRect().width();
        } else {
            posX = value_x->value();
        }

        int posY;
        if (origin_y_top->isChecked()) {
            /* Same for y axis */
            posY = m_frameHeight - value_y->value() - rec->boundingRect().height();
        } else {
            posY = value_y->value();
        }

        rec->setPos(posX, posY);

    } else if (i->type() == RECTITEM) {

        QGraphicsRectItem *rec = static_cast <QGraphicsRectItem *>(i);

        int posX;
        if (origin_x_left->isChecked()) {
            posX = m_frameWidth - value_x->value() - rec->rect().width();
        } else {
            posX = value_x->value();
        }

        int posY;
        if (origin_y_top->isChecked()) {
            posY = m_frameHeight - value_y->value() - rec->rect().height();
        } else {
            posY = value_y->value();
        }

        rec->setPos(posX, posY);

    } else if (i->type() == IMAGEITEM) {
        int posX;
        if (origin_x_left->isChecked()) {
            // Use the sceneBoundingRect because this also regards transformations like zoom
            posX = m_frameWidth - value_x->value() - i->sceneBoundingRect().width();
        } else {
            posX = value_x->value();
        }

        int posY;
        if (origin_y_top->isChecked()) {
            posY = m_frameHeight - value_y->value() - i->sceneBoundingRect().height();
        } else {
            posY = value_y->value();
        }

        i->setPos(posX, posY);

    }

}

void TitleWidget::updateTextOriginX()
{
    if (origin_x_left->isChecked()) {
        origin_x_left->setText(i18n("\u2212X"));
    } else {
        origin_x_left->setText(i18n("+X"));
    }
}

void TitleWidget::slotOriginXClicked()
{
    // Update the text displayed on the button.
    updateTextOriginX();

    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() >= 1) {
        updateCoordinates(l.at(0));

        // Remember x axis setting
        l.at(0)->setData(TitleDocument::OriginXLeft, origin_x_left->isChecked() ?
                         TitleDocument::AxisInverted : TitleDocument::AxisDefault);
    }
    graphicsView->setFocus();
}

void TitleWidget::updateTextOriginY()
{
    if (origin_y_top->isChecked()) {
        origin_y_top->setText(i18n("\u2212Y"));
    } else {
        origin_y_top->setText(i18n("+Y"));
    }
}

void TitleWidget::slotOriginYClicked()
{
    // Update the text displayed on the button.
    updateTextOriginY();

    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() >= 1) {
        updateCoordinates(l.at(0));

        l.at(0)->setData(TitleDocument::OriginYTop, origin_y_top->isChecked() ?
                         TitleDocument::AxisInverted : TitleDocument::AxisDefault);

    }
    graphicsView->setFocus();
}

void TitleWidget::updateAxisButtons(QGraphicsItem *i)
{
    int xAxis = i->data(TitleDocument::OriginXLeft).toInt();
    int yAxis = i->data(TitleDocument::OriginYTop).toInt();
    origin_x_left->blockSignals(true);
    origin_y_top->blockSignals(true);

    if (xAxis == TitleDocument::AxisInverted) {
        origin_x_left->setChecked(true);
    } else {
        origin_x_left->setChecked(false);
    }
    updateTextOriginX();

    if (yAxis == TitleDocument::AxisInverted) {
        origin_y_top->setChecked(true);
    } else {
        origin_y_top->setChecked(false);
    }
    updateTextOriginY();

    origin_x_left->blockSignals(false);
    origin_y_top->blockSignals(false);
}

void TitleWidget::slotChangeBackground()
{
    QColor color = kcolorbutton->color();
    color.setAlpha(horizontalSlider->value());
    m_frameBorder->setBrush(QBrush(color));
}

/**
 * Something (yeah) has changed in our QGraphicsScene.
 */
void TitleWidget::slotChanged()
{
    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() >= 1 && l.at(0)->type() == TEXTITEM) {
        textChanged(static_cast <QGraphicsTextItem *>(l.at(0)));
    }
}

/**
 * If the user has set origin_x_left (everything also for y),
 * we need to look whether a text element has been selected. If yes,
 * we need to ensure that the right border of the text field
 * remains fixed also when some text has been entered.
 *
 * This is also known as right-justified, with the difference that
 * it is not valid for text but for its boundingRect. Text may still
 * be left-justified.
 */
void TitleWidget::textChanged(QGraphicsTextItem *i)
{

    updateDimension(i);

    if (origin_x_left->isChecked() || origin_y_top->isChecked()) {

        if (!i->toPlainText().isEmpty()) {
            updatePosition(i);
        } else {
            /*
             * Don't do anything if the string is empty. If the position
             * would be updated here, a newly created text field would
             * be set to the position of the last selected text field.
             */
        }
    }
}

void TitleWidget::slotInsertUnicode()
{
    m_unicodeDialog->exec();
}

void TitleWidget::slotInsertUnicodeString(QString text)
{
    QList<QGraphicsItem *> l = graphicsView->scene()->selectedItems();
    if (l.size() > 0) {
        if (l.at(0)->type() == TEXTITEM) {
            QGraphicsTextItem *t = static_cast <QGraphicsTextItem *>(l.at(0));
            t->textCursor().insertText(text);
        }
    }
}

void TitleWidget::slotUpdateText()
{
    QFont font = font_family->currentFont();
    font.setPixelSize(font_size->value());
    font.setItalic(buttonItalic->isChecked());
    font.setUnderline(buttonUnder->isChecked());
    font.setWeight(font_weight_box->itemData(font_weight_box->currentIndex()).toInt());
    QColor color = fontColorButton->color();
    color.setAlpha(textAlpha->value());

    QGraphicsTextItem* item = NULL;
    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() == 1 && l.at(0)->type() == TEXTITEM) {
        item = static_cast <QGraphicsTextItem *>(l.at(0));
    }
    if (!item) return;
    //if (item->textCursor().selection ().isEmpty())
    QTextCursor cur = item->textCursor();
    QTextBlockFormat format = cur.blockFormat();
    if (buttonAlignLeft->isChecked() || buttonAlignCenter->isChecked() || buttonAlignRight->isChecked()) {
        item->setTextWidth(item->boundingRect().width());
        if (buttonAlignCenter->isChecked()) format.setAlignment(Qt::AlignHCenter);
        else if (buttonAlignRight->isChecked()) format.setAlignment(Qt::AlignRight);
        else if (buttonAlignLeft->isChecked()) format.setAlignment(Qt::AlignLeft);
    } else {
        format.setAlignment(Qt::AlignLeft);
        item->setTextWidth(-1);
    }

    {
        item->setFont(font);
        item->setDefaultTextColor(color);
        cur.select(QTextCursor::Document);
        cur.setBlockFormat(format);
        item->setTextCursor(cur);
        cur.clearSelection();
        item->setTextCursor(cur);

    }
}

void TitleWidget::rectChanged()
{
    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() == 1 && l.at(0)->type() == RECTITEM && !settingUp) {
        QGraphicsRectItem *rec = static_cast<QGraphicsRectItem *>(l.at(0));
        QColor f = rectFColor->color();
        f.setAlpha(rectFAlpha->value());
        QPen penf(f);
        penf.setWidth(rectLineWidth->value());
        rec->setPen(penf);
        QColor b = rectBColor->color();
        b.setAlpha(rectBAlpha->value());
        rec->setBrush(QBrush(b));
    }
}

void TitleWidget::itemScaled(int val)
{
    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() == 1) {
        Transform x = m_transformations.value(l.at(0));
        x.scalex = (double)val / 100.0;
        x.scaley = (double)val / 100.0;
        QTransform qtrans;
        qtrans.scale(x.scalex, x.scaley);
        qtrans.rotate(x.rotate);
        l[0]->setTransform(qtrans);
        m_transformations[l.at(0)] = x;
        updateDimension(l.at(0));
    }
}

void TitleWidget::itemRotate(int val)
{
    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() == 1) {
        Transform x = m_transformations[l.at(0)];
        x.rotate = (double)val;
        QTransform qtrans;
        qtrans.scale(x.scalex, x.scaley);
        qtrans.rotate(x.rotate);
        l[0]->setTransform(qtrans);
        m_transformations[l.at(0)] = x;
        updateDimension(l.at(0));
    }
}

void TitleWidget::itemHCenter()
{
    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() == 1) {
        QGraphicsItem *item = l.at(0);
        QRectF br;
        br = item->sceneBoundingRect();
        int width = (int)br.width();
        int newPos = (int)((m_frameWidth - width) / 2);
        newPos += item->pos().x() - br.left(); // Check item transformation
        item->setPos(newPos, item->pos().y());
        updateCoordinates(item);
    }
}

void TitleWidget::itemVCenter()
{
    QList<QGraphicsItem*> l = graphicsView->scene()->selectedItems();
    if (l.size() == 1) {
        QGraphicsItem *item = l.at(0);
        QRectF br;
        br = item->sceneBoundingRect();
        int height = (int)br.height();
        int newPos = (int)((m_frameHeight - height) / 2);
        newPos += item->pos().y() - br.top(); // Check item transformation
        item->setPos(item->pos().x(), newPos);
        updateCoordinates(item);
    }
}

void TitleWidget::setupViewports()
{
    //double aspect_ratio = 4.0 / 3.0;//read from project
    //better zoom centered, but render uses only the created rect, so no problem to change the zoom function
    QRectF sp(0, 0, startViewportSize->value() * m_frameWidth / 100.0 , startViewportSize->value()* m_frameHeight / 100.0);
    QRectF ep(0, 0, endViewportSize->value() * m_frameWidth / 100.0, endViewportSize->value() * m_frameHeight / 100.0);
    // use a polygon thiat uses 16:9 and 4:3 rects forpreview the size in all aspect ratios ?
    QPolygonF spoly(sp);
    QPolygonF epoly(ep);
    spoly.translate(startViewportX->value(), startViewportY->value());
    epoly.translate(endViewportX->value(), endViewportY->value());
    m_startViewport->setPolygon(spoly);
    m_endViewport->setPolygon(epoly);
    if (! insertingValues) {
        m_startViewport->setData(0, startViewportX->value());
        m_startViewport->setData(1, startViewportY->value());
        m_startViewport->setData(2, startViewportSize->value());

        m_endViewport->setData(0, endViewportX->value());
        m_endViewport->setData(1, endViewportY->value());
        m_endViewport->setData(2, endViewportSize->value());
    }
}

void TitleWidget::loadTitle(KUrl url)
{
    if (url.isEmpty()) url = KFileDialog::getOpenUrl(KUrl(m_projectTitlePath), "application/x-kdenlivetitle", this, i18n("Load Title"));
    if (!url.isEmpty()) {
        QList<QGraphicsItem *> items = m_scene->items();
        for (int i = 0; i < items.size(); i++) {
            if (items.at(i)->zValue() > -1000) delete items.at(i);
        }
        m_scene->clearTextSelection();
        QDomDocument doc;
        QString tmpfile;

        if (KIO::NetAccess::download(url, tmpfile, 0)) {
            QFile file(tmpfile);
            if (file.open(QIODevice::ReadOnly)) {
                doc.setContent(&file, false);
                file.close();
            } else return;
            KIO::NetAccess::removeTempFile(tmpfile);
        }
        setXml(doc);
        /*int out;
        m_count = m_titledocument.loadDocument(url, m_startViewport, m_endViewport, &out) + 1;
        adjustFrameSize();
        title_duration->setText(m_tc.getTimecode(GenTime(out, m_render->fps())));
        insertingValues = true;
        startViewportX->setValue(m_startViewport->data(0).toInt());
        startViewportY->setValue(m_startViewport->data(1).toInt());
        startViewportSize->setValue(m_startViewport->data(2).toInt());
        endViewportX->setValue(m_endViewport->data(0).toInt());
        endViewportY->setValue(m_endViewport->data(1).toInt());
        endViewportSize->setValue(m_endViewport->data(2).toInt());

        insertingValues = false;
        slotSelectTool();
        slotAdjustZoom();*/
    }
}

void TitleWidget::saveTitle(KUrl url)
{
    if (url.isEmpty()) url = KFileDialog::getSaveUrl(KUrl(m_projectTitlePath), "application/x-kdenlivetitle", this, i18n("Save Title"));
    if (!url.isEmpty()) {
        if (m_titledocument.saveDocument(url, m_startViewport, m_endViewport, m_tc.getFrameCount(title_duration->text())) == false)
            KMessageBox::error(this, i18n("Cannot write to file %1", url.path()));
    }
}

QDomDocument TitleWidget::xml()
{
    QDomDocument doc = m_titledocument.xml(m_startViewport, m_endViewport);
    doc.documentElement().setAttribute("out", m_tc.getFrameCount(title_duration->text()));
    if (cropImage->isChecked()) {
        doc.documentElement().setAttribute("crop", 1);
    }
    return doc;
}

int TitleWidget::duration() const
{
    return m_tc.getFrameCount(title_duration->text());
}

void TitleWidget::setXml(QDomDocument doc)
{
    int out;
    m_count = m_titledocument.loadFromXml(doc, m_startViewport, m_endViewport, &out);
    adjustFrameSize();
    title_duration->setText(m_tc.getTimecode(GenTime(out, m_render->fps())));
    /*if (doc.documentElement().hasAttribute("out")) {
    GenTime duration = GenTime(doc.documentElement().attribute("out").toDouble() / 1000.0);
    title_duration->setText(m_tc.getTimecode(duration));
    }
    else title_duration->setText(m_tc.getTimecode(GenTime(5000)));*/

    QDomElement e = doc.documentElement();
    cropImage->setChecked(e.hasAttribute("crop"));
    m_transformations.clear();
    QList <QGraphicsItem *> items = graphicsView->scene()->items();
    const double PI = 4.0 * atan(1.0);
    for (int i = 0; i < items.count(); i++) {
        QTransform t = items.at(i)->transform();
        Transform x;
        x.scalex = t.m11();
        x.scaley = t.m22();
        x.rotate = 180. / PI * atan2(-t.m21(), t.m11());
        m_transformations[items.at(i)] = x;
    }
    // mbd: Update the GUI color selectors to match the stuff from the loaded document
    QColor background_color = m_titledocument.getBackgroundColor();
    horizontalSlider->blockSignals(true);
    kcolorbutton->blockSignals(true);
    horizontalSlider->setValue(background_color.alpha());
    background_color.setAlpha(255);
    kcolorbutton->setColor(background_color);
    horizontalSlider->blockSignals(false);
    kcolorbutton->blockSignals(false);

    startViewportX->setValue(m_startViewport->data(0).toInt());
    startViewportY->setValue(m_startViewport->data(1).toInt());
    startViewportSize->setValue(m_startViewport->data(2).toInt());
    endViewportX->setValue(m_endViewport->data(0).toInt());
    endViewportY->setValue(m_endViewport->data(1).toInt());
    endViewportSize->setValue(m_endViewport->data(2).toInt());

    QTimer::singleShot(200, this, SLOT(slotAdjustZoom()));
    slotSelectTool();
}

const QRect TitleWidget::renderedRect()
{
    int minX = 0;
    int minY = 0;
    int maxX = m_frameWidth;
    int maxY = m_frameHeight;
    if (!cropImage->isChecked()) {
        m_scene->removeItem(m_startViewport);
        m_scene->removeItem(m_endViewport);
        QRect boundingRect = m_scene->itemsBoundingRect().toRect();
        if (boundingRect.left() < 0) minX = boundingRect.left();
        if (boundingRect.top() < 0) minY = boundingRect.top();
        if (boundingRect.right() > maxX) maxX = boundingRect.right();
        if (boundingRect.bottom() > maxY) maxY = boundingRect.bottom();
        if (minX < 0) {
            maxX = maxX - minX;
        }
        if (minY < 0) {
            maxY = maxY - minY;
        }
    }
    QRect rect(minX, minY, maxX, maxY);
    return rect;
}

QImage TitleWidget::renderedPixmap()
{
    QRect rect = renderedRect();
    QImage pix(rect.width(), rect.height(), QImage::Format_ARGB32);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::HighQualityAntialiasing);
    m_scene->clearTextSelection();
    QPen framepen = m_frameBorder->pen();
    m_frameBorder->setPen(Qt::NoPen);
    m_startViewport->setVisible(false);
    m_endViewport->setVisible(false);
    m_frameImage->setVisible(false);

    m_scene->render(&painter, QRectF(), rect); //QRectF(minX, minY, maxX - minX, maxY - minY));
    painter.end();
    m_frameBorder->setPen(framepen);
    m_startViewport->setVisible(true);
    m_endViewport->setVisible(true);
    m_frameImage->setVisible(true);
    return pix;
}

/** \brief Connected to the accepted signal - calls writeChoices */
void TitleWidget::slotAccepted()
{
    writeChoices();
}

/** \brief Store the current choices of font, background and rect values */
void TitleWidget::writeChoices()
{
    // Get a pointer to a shared configuration instance, then get the TitleWidget group.
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup titleConfig(config, "TitleWidget");
    // Write the entries
    titleConfig.writeEntry("font_family", font_family->currentFont());
    //titleConfig.writeEntry("font_size", font_size->value());
    titleConfig.writeEntry("font_pixel_size", font_size->value());
    titleConfig.writeEntry("font_color", fontColorButton->color());
    titleConfig.writeEntry("font_alpha", textAlpha->value());
    titleConfig.writeEntry("font_weight", font_weight_box->itemData(font_weight_box->currentIndex()).toInt());
    titleConfig.writeEntry("font_italic", buttonItalic->isChecked());
    titleConfig.writeEntry("font_underlined", buttonUnder->isChecked());

    titleConfig.writeEntry("rect_foreground_color", rectFColor->color());
    titleConfig.writeEntry("rect_foreground_alpha", rectFAlpha->value());
    titleConfig.writeEntry("rect_background_color", rectBColor->color());
    titleConfig.writeEntry("rect_background_alpha", rectBAlpha->value());
    titleConfig.writeEntry("rect_line_width", rectLineWidth->value());

    titleConfig.writeEntry("background_color", kcolorbutton->color());
    titleConfig.writeEntry("background_alpha", horizontalSlider->value());

    titleConfig.writeEntry("crop_image", cropImage->isChecked());

    //! \todo Not sure if I should sync - it is probably safe to do it
    config->sync();

}

/** \brief Read the last stored choices into the dialog */
void TitleWidget::readChoices()
{
    // Get a pointer to a shared configuration instance, then get the TitleWidget group.
    KSharedConfigPtr config = KGlobal::config();
    KConfigGroup titleConfig(config, "TitleWidget");
    // read the entries
    font_family->setCurrentFont(titleConfig.readEntry("font_family", font_family->currentFont()));
    font_size->setValue(titleConfig.readEntry("font_pixel_size", font_size->value()));
    fontColorButton->setColor(titleConfig.readEntry("font_color", fontColorButton->color()));
    textAlpha->setValue(titleConfig.readEntry("font_alpha", textAlpha->value()));
    int weight;
    if (titleConfig.readEntry("font_bold", false)) weight = QFont::Bold;
    else weight = titleConfig.readEntry("font_weight", font_weight_box->itemData(font_weight_box->currentIndex()).toInt());
    setFontBoxWeight(weight);
    buttonItalic->setChecked(titleConfig.readEntry("font_italic", buttonItalic->isChecked()));
    buttonUnder->setChecked(titleConfig.readEntry("font_underlined", buttonUnder->isChecked()));

    rectFColor->setColor(titleConfig.readEntry("rect_foreground_color", rectFColor->color()));
    rectFAlpha->setValue(titleConfig.readEntry("rect_foreground_alpha", rectFAlpha->value()));
    rectBColor->setColor(titleConfig.readEntry("rect_background_color", rectBColor->color()));
    rectBAlpha->setValue(titleConfig.readEntry("rect_background_alpha", rectBAlpha->value()));
    rectLineWidth->setValue(titleConfig.readEntry("rect_line_width", rectLineWidth->value()));

    kcolorbutton->setColor(titleConfig.readEntry("background_color", kcolorbutton->color()));
    horizontalSlider->setValue(titleConfig.readEntry("background_alpha", horizontalSlider->value()));

    cropImage->setChecked(titleConfig.readEntry("crop_image", cropImage->isChecked()));
}

void TitleWidget::adjustFrameSize()
{
    m_frameWidth = m_titledocument.frameWidth();
    m_frameHeight = m_titledocument.frameHeight();
    m_frameBorder->setRect(0, 0, m_frameWidth, m_frameHeight);
    displayBackgroundFrame();
}




