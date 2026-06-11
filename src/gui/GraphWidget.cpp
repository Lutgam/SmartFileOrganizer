#include "GraphWidget.h"
#include "../core/TagManager.h"
#include "../core/DrawerCategoryLut.h"
#include <QGraphicsScene>
#include <QPainter>
#include <QTimer>
#include <QDebug>
#include <qmath.h>
#include <QWheelEvent>
#include <QStyleOptionGraphicsItem>
#include <QDir>
#include <QFileInfo>
#include <QLineF>
#include <QListWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QSpinBox>
#include <QSettings>
#include <QRandomGenerator>
#include <QTransform>
#include <functional>
#include <algorithm>

#include "LanguageManager.h"

static QString translateVirtualTagForDisplay(const QString &tag) {
    // Keep the underlying tag untouched; only translate for UI display.
    auto &lm = LanguageManager::instance();
    QString t = tag.trimmed();
    if (t.isEmpty()) return t;

    if (TagManager::hasAiPrefix(t)) {
        t = TagManager::stripAiPrefix(t);
        if (t.isEmpty()) return tag.trimmed();
    }

    // Emoji prefixes can be multiple QChars (surrogates + variation selectors).
    // Parse a "non-letter/number" prefix (excluding spaces) and translate the remainder.
    QString prefix;
    int i = 0;
    while (i < t.size()) {
        const QChar c = t.at(i);
        if (c.isSpace()) break;
        if (c.isLetterOrNumber()) break;
        prefix.append(c);
        ++i;
    }
    const QString rest = t.mid(i).trimmed();
    if (!prefix.isEmpty() && !rest.isEmpty()) {
        const QString translatedRest = lm.getText(rest);
        if (translatedRest != rest) return QStringLiteral("%1 %2").arg(prefix, translatedRest).trimmed();
    }

    return lm.getText(t);
}

namespace {

static const QString kTagImage = QStringLiteral("🖼️ 圖片");
static const QString kTagVideo = QStringLiteral("🎬 影片");
static const QString kTagDoc = QStringLiteral("📄 文件");
static const QString kTagAudio = QStringLiteral("🎵 音檔");
static const QString kTagDb = QStringLiteral("🗃️ 資料庫");

static bool sfAbsolutePathUnderWorkspaceRoot(const QString &absPathRaw, const QString &workspaceRootRaw)
{
    const QString ws = QDir::cleanPath(workspaceRootRaw);
    const QString p = QDir::cleanPath(absPathRaw);
    if (ws.isEmpty() || p.isEmpty())
        return false;
    if (p == ws)
        return true;
    return p.startsWith(ws + QLatin1Char('/'));
}

static QString normalizeDisplayTag(const QString &t)
{
    const QString s = t.trimmed();
    if (s == QStringLiteral("圖片"))
        return kTagImage;
    if (s == QStringLiteral("影片"))
        return kTagVideo;
    if (s == QStringLiteral("文件"))
        return kTagDoc;
    if (s == QStringLiteral("音檔") || s == QStringLiteral("音訊"))
        return kTagAudio;
    if (s == QStringLiteral("資料庫"))
        return kTagDb;
    if (s == QStringLiteral("🖼️圖片") || s == kTagImage)
        return kTagImage;
    if (s == QStringLiteral("🎬影片") || s == kTagVideo)
        return kTagVideo;
    if (s == QStringLiteral("📄文件") || s == kTagDoc)
        return kTagDoc;
    if (s == QStringLiteral("🎧音訊") || s == QStringLiteral("🎵音檔") || s == kTagAudio)
        return kTagAudio;
    if (s == QStringLiteral("🗃️資料庫") || s == QStringLiteral("🗄️資料庫") || s == kTagDb)
        return kTagDb;
    return s;
}

static QStringList orderedSystemTagWhitelistCanons()
{
    return {kTagImage,
            kTagVideo,
            kTagAudio,
            kTagDoc,
            kTagDb,
            QStringLiteral("📦壓縮檔"),
            QStringLiteral("🧩設定"),
            QStringLiteral("🧩 程式碼"),
            QStringLiteral("💻安裝檔"),
            QStringLiteral("💻應用程式"),
            QStringLiteral("📦備份檔"),
            QStringLiteral("🗓️會議"),
            QStringLiteral("🧑‍💼履歷"),
            QStringLiteral("🎒學校作業"),
            QStringLiteral("💰財務"),
            QStringLiteral("🎨設計")};
}

static QString mapLooseSystemTagToWhitelistCanon(const QString &rawIn)
{
    const QString n0 = normalizeDisplayTag(rawIn.trimmed());
    if (TagManager::hasAiPrefix(n0))
        return QString();

    for (const QString &w : orderedSystemTagWhitelistCanons()) {
        if (QString::compare(n0, w, Qt::CaseInsensitive) == 0)
            return w;
    }

    const QString low = n0.toLower();
    if (low.contains(QStringLiteral("system shortcut")) || low.contains(QStringLiteral("系統捷徑")))
        return QStringLiteral("🧩設定");
    if (n0.contains(QStringLiteral("安裝檔")))
        return QStringLiteral("💻安裝檔");
    if (low == QStringLiteral("application") || low == QStringLiteral("applications")
        || low.contains(QStringLiteral("[應用程式]")) || n0.contains(QStringLiteral("應用程式")))
        return QStringLiteral("💻應用程式");
    if (low.contains(QStringLiteral("source code")) || low == QStringLiteral("code") || low == QStringLiteral("script")
        || n0.contains(QStringLiteral("程式碼")))
        return QStringLiteral("🧩 程式碼");
    if (n0.contains(QStringLiteral("壓縮檔")))
        return QStringLiteral("📦壓縮檔");
    if (n0.contains(QStringLiteral("備份檔")))
        return QStringLiteral("📦備份檔");
    if (n0.contains(QStringLiteral("會議")))
        return QStringLiteral("🗓️會議");
    if (n0.contains(QStringLiteral("履歷")))
        return QStringLiteral("🧑‍💼履歷");
    if (n0.contains(QStringLiteral("學校作業")))
        return QStringLiteral("🎒學校作業");
    if (n0.contains(QStringLiteral("財務")))
        return QStringLiteral("💰財務");
    if (n0.contains(QStringLiteral("設計")))
        return QStringLiteral("🎨設計");
    if (n0.contains(QStringLiteral("設定")))
        return QStringLiteral("🧩設定");
    return QString();
}

static QString systemTagBaseZh(const QString &canon)
{
    if (canon == kTagImage)
        return QStringLiteral("圖片");
    if (canon == kTagVideo)
        return QStringLiteral("影片");
    if (canon == kTagDoc)
        return QStringLiteral("文件");
    if (canon == kTagAudio)
        return QStringLiteral("音檔");
    if (canon == kTagDb)
        return QStringLiteral("資料庫");
    if (canon.contains(QStringLiteral("壓縮檔")))
        return QStringLiteral("壓縮檔");
    if (canon.contains(QStringLiteral("程式碼")))
        return QStringLiteral("程式碼");
    if (canon.contains(QStringLiteral("安裝檔")))
        return QStringLiteral("安裝檔");
    if (canon.contains(QStringLiteral("備份檔")))
        return QStringLiteral("備份檔");
    if (canon.contains(QStringLiteral("設定")))
        return QStringLiteral("設定");
    if (canon.contains(QStringLiteral("設計")))
        return QStringLiteral("設計");
    if (canon.contains(QStringLiteral("資料庫")))
        return QStringLiteral("資料庫");
    if (canon.contains(QStringLiteral("學校作業")))
        return QStringLiteral("學校作業");
    if (canon.contains(QStringLiteral("應用程式")))
        return QStringLiteral("應用程式");
    if (canon.contains(QStringLiteral("履歷")))
        return QStringLiteral("履歷");
    return QString();
}

static QString systemTagEmojiPrefix(const QString &canon)
{
    QString out;
    for (int i = 0; i < canon.size(); ++i) {
        const QChar c = canon.at(i);
        if (c.isSpace()) {
            if (!out.isEmpty())
                break;
            continue;
        }
        const ushort u = c.unicode();
        const bool isCjk = (u >= 0x4E00 && u <= 0x9FFF);
        if (c.isLetterOrNumber() || isCjk)
            break;
        out.append(c);
        if (out.size() >= 6)
            break;
    }
    return out.trimmed();
}

static QString tagLibraryLabelStripAiBadge(const QString &displayName)
{
    QString d = displayName.trimmed();
    d = TagManager::stripAiPrefix(d).trimmed();
    return d.trimmed();
}

static QString tagChipDisplayStripLeadingEmoji(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
        return text;

    bool hadAi = false;
    if (text.startsWith(QStringLiteral("[AI]"), Qt::CaseInsensitive)) {
        hadAi = true;
        text = text.mid(4).trimmed();
    }

    int i = 0;
    while (i < text.size()) {
        const QChar c = text.at(i);
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (c.isLetterOrNumber())
            break;
        ++i;
    }
    while (i < text.size() && text.at(i).isSpace())
        ++i;

    QString rest = text.mid(i).trimmed();
    if (hadAi)
        return QStringLiteral("[AI] %1").arg(rest);
    return rest;
}

static QString aiTagLabelForTreeDisplay(const QString &raw)
{
    const QString stripped = tagLibraryLabelStripAiBadge(raw).trimmed();
    auto &lm = LanguageManager::instance();
    const QString drawerLocalized = lm.localizedDrawerLabel(stripped);
    if (drawerLocalized != stripped)
        return drawerLocalized;
    return tagChipDisplayStripLeadingEmoji(stripped);
}

static QStringList sfFixedAiClusterDrawerKeys()
{
    return sfActiveDrawerCategoryLut().drawerKeys();
}

static QString sfNormalizePersistedDrawerValue(const QString &vIn)
{
    return sfActiveDrawerCategoryLut().normalizeDrawerKey(vIn);
}

static bool sfIsSyntheticAiDrawerFolderTag(const QString &t)
{
    return sfActiveDrawerCategoryLut().isSyntheticDrawerFolderTag(t);
}

} // namespace

// --- Edge Implementation ---
Edge::Edge(Node *sourceNode, Node *destNode, const QColor &lineColor)
    : source(sourceNode), dest(destNode), m_lineColor(lineColor)
{
    setAcceptedMouseButtons(Qt::NoButton);
    source->addEdge(this);
    dest->addEdge(this);
    adjust();
}

void Edge::adjust()
{
    if (!source || !dest) return;

    QLineF line(mapFromItem(source, 0, 0), mapFromItem(dest, 0, 0));
    qreal length = line.length();

    prepareGeometryChange();

    if (length > qreal(20.)) {
        QPointF edgeOffset((line.dx() * 10) / length, (line.dy() * 10) / length);
        sourcePoint = line.p1() + edgeOffset;
        destPoint = line.p2() - edgeOffset;
    } else {
        sourcePoint = destPoint = line.p1();
    }
}

QRectF Edge::boundingRect() const
{
    if (!source || !dest) return QRectF();

    qreal penWidth = 1;
    qreal extra = (penWidth + arrowSize) / 2.0;

    return QRectF(sourcePoint, QSizeF(destPoint.x() - sourcePoint.x(),
                                      destPoint.y() - sourcePoint.y()))
        .normalized()
        .adjusted(-extra, -extra, extra, extra);
}

void Edge::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (!source || !dest) return;

    QLineF line(sourcePoint, destPoint);
    if (qFuzzyCompare(line.length(), qreal(0.))) return;

    painter->setPen(QPen(m_lineColor, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawLine(line);
}

// --- Node Implementation ---
Node::Node(GraphWidget *graphWidget, NodeType type, const QString &text)
    : graph(graphWidget), m_type(type), m_text(text)
{
    updateDimensions();
    setFlag(ItemIsMovable);
    setFlag(ItemSendsGeometryChanges);
    setCacheMode(DeviceCoordinateCache);
    setZValue(1);
}

void Node::updateDimensions() const
{
    QFont font;
    font.setBold(true);
    const QFontMetrics fm(font);
    constexpr int padding = 20;
    m_width = qreal(fm.horizontalAdvance(m_text)) + padding * 2;
    m_height = 40.0;
}

void Node::addEdge(Edge *edge)
{
    edgeList << edge;
    edge->adjust();
}

QList<Edge *> Node::edges() const
{
    return edgeList;
}

void Node::calculateForces()
{
    // Force-directed layout disabled — positions come from spiral placement only.
    newPos = pos();
    return;
}

bool Node::advancePosition()
{
    if (newPos == pos())
        return false;

    setPos(newPos);
    return true;
}

QRectF Node::contentRect() const
{
    updateDimensions();
    return QRectF(-m_width / 2.0, -m_height / 2.0, m_width, m_height);
}

QRectF Node::boundingRect() const
{
    constexpr qreal adjust = 2.0;
    return contentRect().adjusted(-adjust, -adjust, adjust, adjust);
}

QPainterPath Node::shape() const
{
    QPainterPath path;
    const QRectF rect = contentRect();
    path.addRoundedRect(rect, 5, 5);
    return path;
}

void Node::beginAppearPop()
{
    m_appearPopStep = 0;
    m_visualScale = 0.28;
    update();
}

void Node::tickAppearPop()
{
    if (m_appearPopStep >= 11) {
        m_visualScale = 1.0;
        return;
    }
    ++m_appearPopStep;
    const qreal t = qreal(m_appearPopStep) / 11.0;
    m_visualScale = 0.28 + (0.72 * t);
    update();
}

void Node::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    QFont font;
    font.setBold(true);
    painter->setFont(font);

    const QRectF rect = contentRect();
    if (!qFuzzyCompare(m_visualScale, 1.0)) {
        painter->translate(rect.center());
        painter->scale(m_visualScale, m_visualScale);
        painter->translate(-rect.center());
    }
    const qreal width = rect.width();

    // Shadow
    painter->setPen(Qt::NoPen);
    painter->setBrush(Qt::darkGray);
    painter->drawRoundedRect(rect.translated(3, 3), 5, 5);

    // Body
    QColor color;
    if (m_type == File) {
        color = QColor(100, 200, 100); // Green
    } else {
        color = QColor(100, 150, 255); // Blue
    }

    const bool glow = m_pressedGlow || (option->state & QStyle::State_Sunken);
    if (glow) {
        QPen glowPen(color.lighter(160), 3);
        glowPen.setCosmetic(true);
        painter->setPen(glowPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(rect.adjusted(-3, -3, 3, 3), 7, 7);
    }

    QRadialGradient gradient(-3, -3, width / 2.0);
    if (glow) {
        gradient.setCenter(3, 3);
        gradient.setFocalPoint(3, 3);
        gradient.setColorAt(1, color.lighter(140));
        gradient.setColorAt(0, color.lighter(110));
    } else {
        gradient.setColorAt(0, color.lighter(120));
        gradient.setColorAt(1, color.darker(120));
    }

    painter->setBrush(gradient);
    painter->setPen(QPen(Qt::black, 0));
    painter->drawRoundedRect(rect, 5, 5);

    // Text
    painter->setPen(Qt::black);
    painter->drawText(rect, Qt::AlignCenter, m_text);
}

QVariant Node::itemChange(GraphicsItemChange change, const QVariant &value)
{
    switch (change) {
    case ItemPositionHasChanged:
        for (Edge *edge : edgeList)
            edge->adjust();
        graph->itemMoved();
        break;
    default:
        break;
    };

    return QGraphicsItem::itemChange(change, value);
}

void Node::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    m_pressedGlow = true;
    update();
    QGraphicsItem::mousePressEvent(event);
}

void Node::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    m_pressedGlow = false;
    update();
    QGraphicsItem::mouseReleaseEvent(event);
}

// --- GraphWidget Implementation ---
GraphWidget::GraphWidget(TagManager* tagMgr, QWidget *parent)
    : QGraphicsView(parent), timerId(0), tagManager(tagMgr)
{
    QGraphicsScene *scene = new QGraphicsScene(this);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    scene->setSceneRect(-kGraphSceneHalfExtent,
                        -kGraphSceneHalfExtent,
                        kGraphSceneHalfExtent * 2.0,
                        kGraphSceneHalfExtent * 2.0);
    setScene(scene);
    
    setCacheMode(CacheBackground);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setDragMode(QGraphicsView::NoDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(0, 0);

    // Initial build
    // buildGraph(); 

    loadMaxGraphNodesSetting();
    ensureToolbar();
    rebuildTagFilterOptions();
}

void GraphWidget::loadMaxGraphNodesSetting()
{
    QSettings s;
    m_maxGraphNodes = qBound(kMinMaxGraphNodes,
                             s.value(QStringLiteral("graph/max_nodes"), kDefaultMaxGraphNodes).toInt(),
                             kMaxMaxGraphNodes);
    if (m_maxNodesSpin) {
        m_maxNodesSpin->blockSignals(true);
        m_maxNodesSpin->setValue(m_maxGraphNodes);
        m_maxNodesSpin->blockSignals(false);
    }
}

void GraphWidget::saveMaxGraphNodesSetting()
{
    QSettings s;
    s.setValue(QStringLiteral("graph/max_nodes"), m_maxGraphNodes);
}

int GraphWidget::countGraphNodesInScene() const
{
    if (!scene())
        return 0;
    int n = 0;
    for (QGraphicsItem *item : scene()->items()) {
        if (qgraphicsitem_cast<const Node *>(item))
            ++n;
    }
    return n;
}

void GraphWidget::itemMoved()
{
    Q_UNUSED(timerId);
    // Physics timer disabled — nodes stay at spiral-placed positions.
}

void GraphWidget::timerEvent(QTimerEvent *event)
{
    Q_UNUSED(event);
    if (timerId) {
        killTimer(timerId);
        timerId = 0;
    }
}

void GraphWidget::wheelEvent(QWheelEvent *event)
{
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    constexpr double scaleFactor = 1.15;
    if (event->angleDelta().y() > 0)
        scale(scaleFactor, scaleFactor);
    else
        scale(1.0 / scaleFactor, 1.0 / scaleFactor);
    event->accept();
}

namespace {

bool graphItemBlocksCanvasPan(QGraphicsItem *item)
{
    if (!item)
        return false;
    if (qgraphicsitem_cast<Node *>(item) || qgraphicsitem_cast<Edge *>(item))
        return true;
    if (QGraphicsItem *parent = item->parentItem()) {
        if (qgraphicsitem_cast<Node *>(parent) || qgraphicsitem_cast<Edge *>(parent))
            return true;
    }
    return false;
}

} // namespace

void GraphWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !graphItemBlocksCanvasPan(itemAt(event->pos()))) {
        m_canvasPanActive = true;
        m_canvasPanLastPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void GraphWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_canvasPanActive) {
        const QPoint delta = event->pos() - m_canvasPanLastPos;
        m_canvasPanLastPos = event->pos();
        if (QScrollBar *h = horizontalScrollBar())
            h->setValue(h->value() - delta.x());
        if (QScrollBar *v = verticalScrollBar())
            v->setValue(v->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void GraphWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_canvasPanActive && event->button() == Qt::LeftButton) {
        m_canvasPanActive = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void GraphWidget::drawBackground(QPainter *painter, const QRectF &rect)
{
    Q_UNUSED(rect);

    QRectF sceneRect = this->sceneRect();

    // Dark background as requested in screenshot
    painter->fillRect(rect.intersected(sceneRect), QColor(20, 20, 20));
    painter->setPen(Qt::NoPen);
    painter->drawRect(sceneRect);
}

void GraphWidget::scaleView(qreal scaleFactor)
{
    qreal factor = transform().scale(scaleFactor, scaleFactor).mapRect(QRectF(0, 0, 1, 1)).width();
    if (factor < 0.07 || factor > 100)
        return;

    scale(scaleFactor, scaleFactor);
}

void GraphWidget::setFilterPanelExpanded(bool expanded)
{
    m_filterPanelExpanded = expanded;
    if (m_filterPanel)
        m_filterPanel->setVisible(expanded);
    if (m_filterExpandBtn)
        m_filterExpandBtn->setVisible(!expanded);
    updateFilterPanelLayout();
}

void GraphWidget::updateFilterPanelLayout()
{
    const int margin = 10;
    if (m_filterExpandBtn && !m_filterPanelExpanded) {
        m_filterExpandBtn->setGeometry(margin, margin + 8, 76, 36);
        m_filterExpandBtn->raise();
    }
    if (m_filterPanel && m_filterPanelExpanded) {
        const int panelWidth = qMin(280, qMax(200, width() / 4));
        m_filterPanel->setGeometry(margin, margin + 8, panelWidth, height() - margin * 2 - 8);
        m_filterPanel->raise();
    }
    if (m_toolbar) {
        const QSize s = m_toolbar->sizeHint();
        m_toolbar->setGeometry(width() - s.width() - margin, margin, s.width(), s.height());
        m_toolbar->raise();
    }
}

void GraphWidget::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    updateFilterPanelLayout();
    fitAllNodes();
}

void GraphWidget::ensureToolbar() {
    if (m_toolbar) return;

    m_filterExpandBtn = new QPushButton(QStringLiteral("展開設定"), this);
    m_filterExpandBtn->setObjectName(QStringLiteral("graphFilterExpandTab"));
    m_filterExpandBtn->setCursor(Qt::PointingHandCursor);
    m_filterExpandBtn->setStyleSheet(QStringLiteral(
        "QPushButton#graphFilterExpandTab {"
        "  background: rgba(20,20,20,230);"
        "  color: #e2e8f0;"
        "  border: 1px solid rgba(255,255,255,40);"
        "  border-radius: 8px;"
        "  padding: 6px 8px;"
        "  font-weight: bold;"
        "}"
        "QPushButton#graphFilterExpandTab:hover { background: rgba(45,55,72,240); }"));
    m_filterExpandBtn->hide();
    connect(m_filterExpandBtn, &QPushButton::clicked, this, [this]() { setFilterPanelExpanded(true); });

    m_filterPanel = new QWidget(this);
    m_filterPanel->setObjectName(QStringLiteral("graphFilterPanel"));
    m_filterPanel->setStyleSheet(QStringLiteral(
        "QWidget#graphFilterPanel { background: rgba(20,20,20,220); border: 1px solid rgba(255,255,255,35); border-radius: 8px; }"
        "QLabel { color: white; }"
        "QTabWidget::pane { border: none; background: transparent; }"
        "QTabBar::tab { color: #cbd5e1; padding: 4px 8px; }"
        "QTabBar::tab:selected { color: white; font-weight: bold; }"
        "QListWidget, QTreeWidget { background: rgba(0,0,0,40); color: #e2e8f0; border: none; }"
        "QListWidget::item, QTreeWidget::item { padding: 4px 2px; }"));
    auto *filterLayout = new QVBoxLayout(m_filterPanel);
    filterLayout->setContentsMargins(8, 8, 8, 8);
    filterLayout->setSpacing(6);

    {
        auto *headerRow = new QHBoxLayout();
        m_filterLabel = new QLabel(QStringLiteral("標籤過濾"), m_filterPanel);
        headerRow->addWidget(m_filterLabel, 1);
        m_btnCollapseFilter = new QPushButton(QStringLiteral("收起"), m_filterPanel);
        m_btnCollapseFilter->setCursor(Qt::PointingHandCursor);
        m_btnCollapseFilter->setFlat(true);
        m_btnCollapseFilter->setStyleSheet(QStringLiteral("QPushButton { color: #94a3b8; }"
                                                         "QPushButton:hover { color: white; }"));
        connect(m_btnCollapseFilter, &QPushButton::clicked, this, [this]() { setFilterPanelExpanded(false); });
        headerRow->addWidget(m_btnCollapseFilter);
        filterLayout->addLayout(headerRow);
    }

    m_tagFilterTabWidget = new QTabWidget(m_filterPanel);
    m_systemFilterList = new QListWidget(m_filterPanel);
    m_systemFilterList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_aiFilterList = new QListWidget(m_filterPanel);
    m_aiFilterList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    auto &lm = LanguageManager::instance();
    m_tagFilterTabWidget->addTab(m_systemFilterList, lm.getText(QStringLiteral("副檔名分類")));
    m_tagFilterTabWidget->addTab(m_aiFilterList, lm.getText(QStringLiteral("預設標籤分類 (18大類)")));
    filterLayout->addWidget(m_tagFilterTabWidget, 1);

    {
        auto *limitRow = new QHBoxLayout();
        m_maxNodesSpinLabel = new QLabel(QStringLiteral("顯示節點上限"), m_filterPanel);
        limitRow->addWidget(m_maxNodesSpinLabel);
        m_maxNodesSpin = new QSpinBox(m_filterPanel);
        m_maxNodesSpin->setRange(kMinMaxGraphNodes, kMaxMaxGraphNodes);
        m_maxNodesSpin->setValue(m_maxGraphNodes);
        limitRow->addWidget(m_maxNodesSpin);
        filterLayout->addLayout(limitRow);

        connect(m_maxNodesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
            m_maxGraphNodes = qBound(kMinMaxGraphNodes, value, kMaxMaxGraphNodes);
            saveMaxGraphNodesSetting();
            buildGraph();
        });
    }

    auto scheduleBuildGraph = [this]() {
        QTimer::singleShot(0, this, [this]() { buildGraph(); });
    };
    connect(m_systemFilterList, &QListWidget::itemChanged, this, [scheduleBuildGraph](QListWidgetItem *) {
        scheduleBuildGraph();
    });
    connect(m_aiFilterList, &QListWidget::itemChanged, this, [scheduleBuildGraph](QListWidgetItem *) {
        scheduleBuildGraph();
    });

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("graphToolbar"));
    m_toolbar->setStyleSheet(QStringLiteral(
        "QWidget#graphToolbar { background: rgba(30,30,30,200); border: 1px solid rgba(255,255,255,40); border-radius: 8px; }"
        "QLabel { color: white; }"
        "QComboBox { padding: 2px 6px; }"
        "QPushButton { color: white; padding: 4px 10px; }"));

    auto *row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(10, 8, 10, 8);
    row->setSpacing(8);

    m_btnResetView = new QPushButton(QStringLiteral("重置視圖"), m_toolbar);
    m_btnZoomOut = new QPushButton(QStringLiteral("−"), m_toolbar);
    m_btnZoomIn = new QPushButton(QStringLiteral("+"), m_toolbar);
    m_btnZoomOut->setFixedWidth(36);
    m_btnZoomIn->setFixedWidth(36);
    connect(m_btnResetView, &QPushButton::clicked, this, &GraphWidget::resetView);
    connect(m_btnZoomIn, &QPushButton::clicked, this, &GraphWidget::zoomIn);
    connect(m_btnZoomOut, &QPushButton::clicked, this, &GraphWidget::zoomOut);
    row->addWidget(m_btnResetView);
    row->addWidget(m_btnZoomOut);
    row->addWidget(m_btnZoomIn);

    m_maxNodesHint = new QLabel(QStringLiteral("滾輪縮放 · 拖曳畫布平移"), m_toolbar);
    row->addWidget(m_maxNodesHint);

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        auto &lm = LanguageManager::instance();
        if (m_filterLabel)
            m_filterLabel->setText(lm.getText(QStringLiteral("標籤過濾")));
        if (m_tagFilterTabWidget && m_systemFilterList && m_aiFilterList) {
            m_tagFilterTabWidget->setTabText(m_tagFilterTabWidget->indexOf(m_systemFilterList),
                                             lm.getText(QStringLiteral("副檔名分類")));
            m_tagFilterTabWidget->setTabText(m_tagFilterTabWidget->indexOf(m_aiFilterList),
                                             lm.getText(QStringLiteral("預設標籤分類 (18大類)")));
        }
        if (m_maxNodesSpinLabel) {
            m_maxNodesSpinLabel->setText(
                lm.language() == LanguageManager::Language::EN_US ? QStringLiteral("Max nodes shown")
                                                                : QStringLiteral("顯示節點上限"));
        }
        if (m_btnResetView) {
            m_btnResetView->setText(lm.language() == LanguageManager::Language::EN_US ? QStringLiteral("Reset view")
                                                                                      : QStringLiteral("重置視圖"));
        }
        if (m_filterExpandBtn) {
            m_filterExpandBtn->setText(lm.language() == LanguageManager::Language::EN_US ? QStringLiteral("Expand")
                                                                                         : QStringLiteral("展開設定"));
        }
        if (m_btnCollapseFilter) {
            m_btnCollapseFilter->setText(lm.language() == LanguageManager::Language::EN_US ? QStringLiteral("Collapse")
                                                                                           : QStringLiteral("收起"));
        }
        if (m_maxNodesHint) {
            m_maxNodesHint->setText(lm.language() == LanguageManager::Language::EN_US
                                        ? QStringLiteral("Wheel zoom · drag canvas to pan")
                                        : QStringLiteral("滾輪縮放 · 拖曳畫布平移"));
        }
        rebuildTagFilterOptions();
    });

    m_toolbar->show();
    m_filterPanel->show();
    m_filterExpandBtn->show();
    setFilterPanelExpanded(m_filterPanelExpanded);
}

void GraphWidget::setFilterContext(const QString &workspaceRoot, const QHash<QString, QString> &aiTagToDrawerKey)
{
    m_workspaceRoot = QDir::cleanPath(workspaceRoot);
    m_aiTagToDrawerKey = aiTagToDrawerKey;
    rebuildTagFilterOptions();
}

QStringList GraphWidget::selectedFilterTags() const
{
    QStringList out;
    if (m_systemFilterList) {
        for (int i = 0; i < m_systemFilterList->count(); ++i) {
            QListWidgetItem *item = m_systemFilterList->item(i);
            if (!item || item->checkState() != Qt::Checked)
                continue;
            const QString tag = item->data(Qt::UserRole).toString().trimmed();
            if (!tag.isEmpty())
                out << tag;
        }
    }

    for (const QString &dk : selectedAiDrawerKeys())
        out << drawerTagNodeKey(dk);

    out.removeDuplicates();
    return out;
}

QSet<QString> GraphWidget::selectedAiDrawerKeys() const
{
    QSet<QString> out;
    if (!m_aiFilterList)
        return out;

    for (int i = 0; i < m_aiFilterList->count(); ++i) {
        QListWidgetItem *item = m_aiFilterList->item(i);
        if (!item || item->checkState() != Qt::Checked)
            continue;
        const QString role = item->data(Qt::UserRole).toString().trimmed();
        if (!role.startsWith(QStringLiteral("SF_DRAWER:")))
            continue;
        const QString dk = role.mid(QStringLiteral("SF_DRAWER:").size());
        if (!dk.isEmpty())
            out.insert(dk);
    }
    return out;
}

QString GraphWidget::aiDrawerKeyForLeaf(const QString &leafTag) const
{
    QString dk = sfNormalizePersistedDrawerValue(m_aiTagToDrawerKey.value(leafTag.trimmed()));
    static const QString kFallbackDrawer = QStringLiteral("📦 雜項");
    if (dk.isEmpty() || !sfFixedAiClusterDrawerKeys().contains(dk))
        dk = kFallbackDrawer;
    return dk;
}

QString GraphWidget::drawerTagNodeKey(const QString &drawerKey)
{
    return QStringLiteral("SF_DRAWER:%1").arg(drawerKey);
}

QColor GraphWidget::edgeColorForTag(const QString &tag, int paletteIndex) const
{
    Q_UNUSED(tag);
    static const QColor palette[] = {
        QColor(248, 113, 113),
        QColor(96, 165, 250),
        QColor(74, 222, 128),
        QColor(251, 191, 36),
        QColor(192, 132, 252),
        QColor(45, 212, 191),
        QColor(244, 114, 182),
        QColor(148, 163, 184),
    };
    return palette[paletteIndex % (sizeof(palette) / sizeof(palette[0]))];
}

void GraphWidget::rebuildTagFilterOptions() {
    ensureToolbar();
    if (!m_systemFilterList || !m_aiFilterList || !tagManager)
        return;

    if (m_filterLabel)
        m_filterLabel->setText(LanguageManager::instance().getText(QStringLiteral("標籤過濾")));

    const QStringList prevChecked = selectedFilterTags();
    const QSet<QString> prevAiDrawers = selectedAiDrawerKeys();
    const QString workspaceRoot = QDir::cleanPath(m_workspaceRoot);
    int defaultChecksLeft = prevChecked.isEmpty() ? 3 : 0;

    m_systemFilterList->blockSignals(true);
    m_aiFilterList->blockSignals(true);
    m_systemFilterList->clear();
    m_aiFilterList->clear();

    std::vector<QString> rawTags = tagManager->getAllTags();

    QMap<QString, QSet<QString>> normToFiles;
    for (const QString &t : rawTags) {
        const QString canon = normalizeDisplayTag(t);
        for (const QString &fp : tagManager->getFilesByTag(t)) {
            if (!workspaceRoot.isEmpty() && !sfAbsolutePathUnderWorkspaceRoot(fp, workspaceRoot))
                continue;
            normToFiles[canon].insert(QDir::cleanPath(fp));
        }
    }

    QMap<QString, QSet<QString>> systemWhitelistToFiles;
    for (const QString &t : rawTags) {
        const QString canon = normalizeDisplayTag(t);
        if (TagManager::hasAiPrefix(canon))
            continue;
        const QString sysCanon = mapLooseSystemTagToWhitelistCanon(canon);
        if (sysCanon.isEmpty())
            continue;
        for (const QString &fp : tagManager->getFilesByTag(t)) {
            if (!workspaceRoot.isEmpty() && !sfAbsolutePathUnderWorkspaceRoot(fp, workspaceRoot))
                continue;
            systemWhitelistToFiles[sysCanon].insert(QDir::cleanPath(fp));
        }
    }

    auto shouldCheck = [&](const QString &tagKey) {
        if (prevChecked.contains(tagKey))
            return true;
        if (defaultChecksLeft > 0) {
            --defaultChecksLeft;
            return true;
        }
        return false;
    };

    auto drawerShouldCheck = [&](const QString &drawerKey) {
        if (prevAiDrawers.contains(drawerKey))
            return true;
        const QString drawerRole = drawerTagNodeKey(drawerKey);
        if (prevChecked.contains(drawerRole))
            return true;
        for (const QString &tag : prevChecked) {
            if (TagManager::hasAiPrefix(tag) && aiDrawerKeyForLeaf(tag) == drawerKey)
                return true;
        }
        if (defaultChecksLeft > 0) {
            --defaultChecksLeft;
            return true;
        }
        return false;
    };

    for (const QString &sysCanon : orderedSystemTagWhitelistCanons()) {
        const int n = static_cast<int>(systemWhitelistToFiles.value(sysCanon).size());
        if (n <= 0)
            continue;
        const QString baseZh = systemTagBaseZh(sysCanon);
        const QString emoji = systemTagEmojiPrefix(sysCanon);
        const QString displayName = baseZh.isEmpty()
                                        ? sysCanon
                                        : QStringLiteral("%1 %2").arg(emoji, LanguageManager::instance().getText(baseZh));
        auto *item = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(displayName.trimmed()).arg(n),
                                         m_systemFilterList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(Qt::UserRole, sysCanon);
        item->setCheckState(shouldCheck(sysCanon) ? Qt::Checked : Qt::Unchecked);
    }

    auto countFor = [&](const QString &c) -> int {
        if (normToFiles.contains(c))
            return static_cast<int>(normToFiles.value(c).size());
        int n = 0;
        for (const QString &fp : tagManager->getFilesByTag(c)) {
            if (workspaceRoot.isEmpty() || sfAbsolutePathUnderWorkspaceRoot(fp, workspaceRoot))
                ++n;
        }
        return n;
    };

    QSet<QString> aiLeaves;
    for (const QString &t : rawTags) {
        const QString tt = t.trimmed();
        if (!TagManager::hasAiPrefix(tt))
            continue;
        if (sfIsSyntheticAiDrawerFolderTag(tt))
            continue;
        aiLeaves.insert(tt);
    }

    QHash<QString, QVector<QString>> drawerToLeaves;
    for (const QString &dk : sfFixedAiClusterDrawerKeys())
        drawerToLeaves.insert(dk, {});

    for (const QString &leaf : std::as_const(aiLeaves)) {
        QString dk = sfNormalizePersistedDrawerValue(m_aiTagToDrawerKey.value(leaf));
        static const QString kFallbackDrawer = QStringLiteral("📦 雜項");
        if (dk.isEmpty() || !drawerToLeaves.contains(dk))
            dk = kFallbackDrawer;
        drawerToLeaves[dk].append(leaf);
    }

    auto sortLeaves = [&](QVector<QString> &vec) {
        std::sort(vec.begin(), vec.end(), [&](const QString &a, const QString &b) {
            const int na = countFor(a);
            const int nb = countFor(b);
            if (na != nb)
                return na > nb;
            return a.localeAwareCompare(b) < 0;
        });
    };

    for (auto it = drawerToLeaves.begin(); it != drawerToLeaves.end(); ++it)
        sortLeaves(it.value());

    for (const QString &drawerKey : sfFixedAiClusterDrawerKeys()) {
        const QVector<QString> leaves = drawerToLeaves.value(drawerKey);
        int sumFiles = 0;
        for (const QString &lf : leaves)
            sumFiles += countFor(lf);

        auto *item = new QListWidgetItem(QStringLiteral("(%1) %2")
                                             .arg(sumFiles)
                                             .arg(aiTagLabelForTreeDisplay(drawerKey)),
                                         m_aiFilterList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(Qt::UserRole, drawerTagNodeKey(drawerKey));
        item->setCheckState(drawerShouldCheck(drawerKey) ? Qt::Checked : Qt::Unchecked);
    }

    m_systemFilterList->blockSignals(false);
    m_aiFilterList->blockSignals(false);
}

void GraphWidget::zoomIn()
{
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scaleView(1.15);
}

void GraphWidget::zoomOut()
{
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scaleView(1.0 / 1.15);
}

void GraphWidget::resetView()
{
    fitAllNodes();
}

void GraphWidget::placeNodeWithSpiral(Node *newNode, const QPointF &center)
{
    if (!newNode || !scene())
        return;

    const qreal nodeW = newNode->boundingRect().width();
    const qreal nodeH = newNode->boundingRect().height();

    const int placedSoFar = countGraphNodesInScene();
    const qreal sizeBase = qMax(nodeW, nodeH);
    qreal radius = 35.0 + sizeBase * 0.45 + placedSoFar * 4.0;
    qreal angle = QRandomGenerator::global()->bounded(360);
    bool placed = false;

    const qreal baseAngleStep = 30.0;
    constexpr qreal margin = 20.0;
    constexpr int kMaxSteps = 12000;

    qreal x = center.x();
    qreal y = center.y();

    const qreal radiusStep = qMax(90.0, 80.0 + sizeBase * 0.35 + placedSoFar * 2.0);

    for (int step = 0; !placed && step < kMaxSteps; ++step) {
        x = center.x() + radius * qCos(angle * M_PI / 180.0);
        y = center.y() + radius * qSin(angle * M_PI / 180.0);

        const QRectF proposedRect(x - nodeW / 2.0 - margin,
                                  y - nodeH / 2.0 - margin,
                                  nodeW + margin * 2.0,
                                  nodeH + margin * 2.0);

        if (scene()->items(proposedRect, Qt::IntersectsItemBoundingRect).isEmpty()) {
            placed = true;
            break;
        }

        const qreal jitter = qreal(QRandomGenerator::global()->bounded(11)) - 5.0;
        angle += baseAngleStep + jitter;
        if (angle >= 360.0) {
            angle -= 360.0;
            radius += radiusStep;
        }
    }

    newNode->setPos(x, y);
}

void GraphWidget::fitAllNodes()
{
    if (!scene() || scene()->items().isEmpty())
        return;

    QRectF itemsRect = scene()->itemsBoundingRect();
    if (!itemsRect.isValid() || itemsRect.isNull())
        return;

    scene()->setSceneRect(itemsRect.marginsAdded(QMarginsF(480, 480, 480, 480)));

    resetTransform();
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);

    if (transform().m11() > 1.0)
        setTransform(QTransform::fromScale(1.0, 1.0));
}

void GraphWidget::buildGraph() {
    scene()->clear();
    scene()->setSceneRect(-kGraphSceneHalfExtent,
                          -kGraphSceneHalfExtent,
                          kGraphSceneHalfExtent * 2.0,
                          kGraphSceneHalfExtent * 2.0);
    fileNodes.clear();
    tagNodes.clear();

    if (!tagManager) return;

    const QStringList filterTags = selectedFilterTags();
    const QSet<QString> selectedDrawers = selectedAiDrawerKeys();
    if (filterTags.isEmpty() && selectedDrawers.isEmpty()) {
        return;
    }

    QStringList systemFilters;
    for (const QString &tag : filterTags) {
        if (!tag.startsWith(QStringLiteral("SF_DRAWER:")))
            systemFilters << tag;
    }

    QSet<QString> selectedAiLeaves;
    if (!selectedDrawers.isEmpty()) {
        for (const QString &t : tagManager->getAllTags()) {
            const QString tt = t.trimmed();
            if (!TagManager::hasAiPrefix(tt) || sfIsSyntheticAiDrawerFolderTag(tt))
                continue;
            if (selectedDrawers.contains(aiDrawerKeyForLeaf(tt)))
                selectedAiLeaves.insert(tt);
        }
    }

    QSet<QString> candidateSet;
    const QString workspaceRoot = QDir::cleanPath(m_workspaceRoot);
    for (const QString &filterTag : systemFilters) {
        for (const QString &p : tagManager->getFilesByTag(filterTag)) {
            if (!workspaceRoot.isEmpty() && !sfAbsolutePathUnderWorkspaceRoot(p, workspaceRoot))
                continue;
            candidateSet.insert(QDir::cleanPath(p));
        }
    }
    for (const QString &leaf : std::as_const(selectedAiLeaves)) {
        for (const QString &p : tagManager->getFilesByTag(leaf)) {
            if (!workspaceRoot.isEmpty() && !sfAbsolutePathUnderWorkspaceRoot(p, workspaceRoot))
                continue;
            candidateSet.insert(QDir::cleanPath(p));
        }
    }

    QStringList candidateFiles = candidateSet.values();
    candidateFiles.removeDuplicates();
    std::sort(candidateFiles.begin(), candidateFiles.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });

    if (candidateFiles.isEmpty()) {
        return;
    }

    // 1) Tag nodes: extension categories + 18 AI drawer categories (not individual AI leaf tags).
    QSet<QString> activeDrawerKeys;
    for (const QString &leaf : std::as_const(selectedAiLeaves))
        activeDrawerKeys.insert(aiDrawerKeyForLeaf(leaf));

    std::sort(systemFilters.begin(), systemFilters.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });

    for (const QString &sysCanon : systemFilters) {
        if (countGraphNodesInScene() >= m_maxGraphNodes)
            break;
        const QString display = translateVirtualTagForDisplay(sysCanon);
        Node *tagNode = new Node(this, Node::Tag, display);
        placeNodeWithSpiral(tagNode);
        scene()->addItem(tagNode);
        tagNodes[sysCanon] = tagNode;
    }

    QStringList drawerOrder = sfFixedAiClusterDrawerKeys();
    for (const QString &dk : std::as_const(drawerOrder)) {
        if (!activeDrawerKeys.contains(dk))
            continue;
        if (countGraphNodesInScene() >= m_maxGraphNodes)
            break;

        int linkedFiles = 0;
        for (const QString &fp : std::as_const(candidateFiles)) {
            const auto tags = tagManager->getTags(fp);
            for (const QString &t : tags) {
                if (!selectedAiLeaves.contains(t))
                    continue;
                if (aiDrawerKeyForLeaf(t) == dk) {
                    ++linkedFiles;
                    break;
                }
            }
        }

        const QString mapKey = drawerTagNodeKey(dk);
        const QString label =
            QStringLiteral("(%1) %2").arg(linkedFiles).arg(aiTagLabelForTreeDisplay(dk));
        Node *tagNode = new Node(this, Node::Tag, label);
        placeNodeWithSpiral(tagNode);
        scene()->addItem(tagNode);
        tagNodes[mapKey] = tagNode;
    }

    // 2) File nodes + edges to extension tags or AI drawer hub nodes.
    for (const QString &fp : candidateFiles) {
        if (countGraphNodesInScene() >= m_maxGraphNodes)
            break;

        const QFileInfo fi(fp);
        if (!fi.exists() || !fi.isFile())
            continue;

        const auto tags = tagManager->getTags(fp);
        QSet<QString> fileTags;
        for (const QString &t : tags) {
            if (!t.trimmed().isEmpty())
                fileTags.insert(t);
        }

        Node *fileNode = nullptr;
        if (fileNodes.find(fp) == fileNodes.end()) {
            if (countGraphNodesInScene() >= m_maxGraphNodes)
                break;
            fileNode = new Node(this, Node::File, fi.fileName());
            placeNodeWithSpiral(fileNode);
            scene()->addItem(fileNode);
            fileNodes[fp] = fileNode;
        } else {
            fileNode = fileNodes[fp];
        }

        for (int si = 0; si < systemFilters.size(); ++si) {
            const QString &sysCanon = systemFilters[si];
            if (!fileTags.contains(sysCanon))
                continue;
            const auto tagIt = tagNodes.find(sysCanon);
            if (tagIt == tagNodes.end())
                continue;
            scene()->addItem(new Edge(tagIt->second, fileNode, edgeColorForTag(sysCanon, si)));
        }

        QSet<QString> drawersLinkedForFile;
        for (const QString &leaf : std::as_const(selectedAiLeaves)) {
            if (!fileTags.contains(leaf))
                continue;
            const QString dk = aiDrawerKeyForLeaf(leaf);
            if (drawersLinkedForFile.contains(dk))
                continue;
            drawersLinkedForFile.insert(dk);
            const auto tagIt = tagNodes.find(drawerTagNodeKey(dk));
            if (tagIt == tagNodes.end())
                continue;
            const QStringList drawerKeys = sfFixedAiClusterDrawerKeys();
            const int paletteIdx = qMax(0, drawerKeys.indexOf(dk));
            scene()->addItem(new Edge(tagIt->second, fileNode, edgeColorForTag(dk, paletteIdx)));
        }
    }

    fitAllNodes();
    runAppearPopAnimation();
}

void GraphWidget::runAppearPopAnimation()
{
    m_graphPopNodes.clear();
    if (!scene())
        return;

    for (QGraphicsItem *item : scene()->items()) {
        if (Node *node = qgraphicsitem_cast<Node *>(item))
            m_graphPopNodes.append(node);
    }
    if (m_graphPopNodes.isEmpty())
        return;

    for (Node *node : std::as_const(m_graphPopNodes))
        node->beginAppearPop();

    m_graphPopStep = 0;
    if (!m_graphPopTimer) {
        m_graphPopTimer = new QTimer(this);
        connect(m_graphPopTimer, &QTimer::timeout, this, &GraphWidget::onGraphPopTick);
    }
    m_graphPopTimer->start(30);
}

void GraphWidget::onGraphPopTick()
{
    for (Node *node : std::as_const(m_graphPopNodes))
        node->tickAppearPop();

    ++m_graphPopStep;
    if (m_graphPopStep >= 11) {
        if (m_graphPopTimer)
            m_graphPopTimer->stop();
        m_graphPopNodes.clear();
    }
}
