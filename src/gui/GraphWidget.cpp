#include "GraphWidget.h"
#include "../core/TagManager.h"
#include <QGraphicsScene>
#include <QPainter>
#include <QTimer>
#include <QDebug>
#include <qmath.h>
#include <QWheelEvent>
#include <QStyleOptionGraphicsItem>
#include <QRandomGenerator>
#include <QFileInfo>
#include <QLineF>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSet>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
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
    setFlag(ItemIsMovable);
    setFlag(ItemSendsGeometryChanges);
    setCacheMode(DeviceCoordinateCache);
    setZValue(1);
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
    if (!scene() || scene()->mouseGrabberItem() == this) {
        newPos = pos();
        return;
    }

    // Sum up all forces
    qreal xvel = 0;
    qreal yvel = 0;

    // Repulsion from other nodes
    for (QGraphicsItem *item : scene()->items()) {
        Node *node = qgraphicsitem_cast<Node *>(item);
        if (!node) continue;

        QPointF vec = mapToItem(node, 0, 0);
        qreal dx = vec.x();
        qreal dy = vec.y();
        double l = 2.0 * (dx * dx + dy * dy);
        if (l > 0) {
            xvel += (dx * 150.0) / l;
            yvel += (dy * 150.0) / l;
        }
    }

    // Attraction to connected nodes (Edges)
    double weight = (edgeList.size() + 1) * 10;
    for (const Edge *edge : edgeList) {
        QPointF vec;
        if (edge->sourceNode() == this)
            vec = mapToItem(edge->destNode(), 0, 0);
        else
            vec = mapToItem(edge->sourceNode(), 0, 0);
        
        xvel -= vec.x() / weight;
        yvel -= vec.y() / weight;
    }

    if (qAbs(xvel) < 0.1 && qAbs(yvel) < 0.1)
        xvel = yvel = 0;

    QRectF sceneRect = scene()->sceneRect();
    newPos = pos() + QPointF(xvel, yvel);
    
    // Keep within bounds
    newPos.setX(qMin(qMax(newPos.x(), sceneRect.left() + 10), sceneRect.right() - 10));
    newPos.setY(qMin(qMax(newPos.y(), sceneRect.top() + 10), sceneRect.bottom() - 10));
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
    QFont font;
    font.setBold(true);
    const QFontMetrics fm(font);
    const int width = fm.horizontalAdvance(m_text) + 30;
    return QRectF(-width / 2.0, -15.0, width, 30.0);
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

void Node::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    const QRectF rect = contentRect();
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

    QRadialGradient gradient(-3, -3, width / 2.0);
    if (option->state & QStyle::State_Sunken) {
        gradient.setCenter(3, 3);
        gradient.setFocalPoint(3, 3);
        gradient.setColorAt(1, color.lighter(120));
        gradient.setColorAt(0, color.darker(120));
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
    update();
    QGraphicsItem::mousePressEvent(event);
}

void Node::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    update();
    QGraphicsItem::mouseReleaseEvent(event);
}

// --- GraphWidget Implementation ---
GraphWidget::GraphWidget(TagManager* tagMgr, QWidget *parent)
    : QGraphicsView(parent), timerId(0), tagManager(tagMgr)
{
    QGraphicsScene *scene = new QGraphicsScene(this);
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    scene->setSceneRect(-400, -400, 800, 800);
    setScene(scene);
    
    setCacheMode(CacheBackground);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(AnchorUnderMouse);
    setFrameShape(QFrame::NoFrame);
    scale(0.9, 0.9);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(0, 0);

    // Initial build
    // buildGraph(); 

    ensureToolbar();
    rebuildTagFilterOptions();
}

void GraphWidget::itemMoved()
{
    if (!timerId)
        timerId = startTimer(1000 / 25);
}

void GraphWidget::timerEvent(QTimerEvent *event)
{
    QList<Node *> nodes;
    const QList<QGraphicsItem *> items = scene()->items();
    for (QGraphicsItem *item : items) {
        if (Node *node = qgraphicsitem_cast<Node *>(item))
            nodes << node;
    }

    for (Node *node : nodes)
        node->calculateForces();

    bool itemsMoved = false;
    for (Node *node : nodes) {
        if (node->advancePosition())
            itemsMoved = true;
    }

    if (!itemsMoved) {
        killTimer(timerId);
        timerId = 0;
    }
}

void GraphWidget::wheelEvent(QWheelEvent *event)
{
    scaleView(pow(2., -event->angleDelta().y() / 240.0));
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

void GraphWidget::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    const int margin = 10;
    if (m_filterPanel) {
        const int panelWidth = qMin(240, qMax(180, width() / 4));
        m_filterPanel->setGeometry(margin, margin + 8, panelWidth, height() - margin * 2 - 8);
    }
    if (!m_toolbar) return;
    const QSize s = m_toolbar->sizeHint();
    m_toolbar->setGeometry(width() - s.width() - margin, margin, s.width(), s.height());
}

void GraphWidget::ensureToolbar() {
    if (m_toolbar) return;

    m_filterPanel = new QWidget(this);
    m_filterPanel->setObjectName(QStringLiteral("graphFilterPanel"));
    m_filterPanel->setStyleSheet(QStringLiteral(
        "QWidget#graphFilterPanel { background: rgba(20,20,20,220); border: 1px solid rgba(255,255,255,35); border-radius: 8px; }"
        "QLabel { color: white; }"
        "QListWidget { background: rgba(0,0,0,40); color: #e2e8f0; border: none; }"
        "QListWidget::item { padding: 4px 2px; }"));
    auto *filterLayout = new QVBoxLayout(m_filterPanel);
    filterLayout->setContentsMargins(8, 8, 8, 8);
    filterLayout->setSpacing(6);

    m_filterLabel = new QLabel(QStringLiteral("標籤過濾"), m_filterPanel);
    filterLayout->addWidget(m_filterLabel);

    m_tagFilterList = new QListWidget(m_filterPanel);
    m_tagFilterList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterLayout->addWidget(m_tagFilterList, 1);

    connect(m_tagFilterList, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { buildGraph(); });

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("graphToolbar"));
    m_toolbar->setStyleSheet(QStringLiteral(
        "QWidget#graphToolbar { background: rgba(30,30,30,200); border: 1px solid rgba(255,255,255,40); border-radius: 8px; }"
        "QLabel { color: white; }"
        "QComboBox { padding: 2px 6px; }"));

    auto *row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(10, 8, 10, 8);
    row->setSpacing(8);

    m_maxNodesHint = new QLabel(QStringLiteral("節點上限：50（固定）"), m_toolbar);
    row->addWidget(m_maxNodesHint);

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        if (m_filterLabel)
            m_filterLabel->setText(LanguageManager::instance().getText(QStringLiteral("標籤過濾")));
        if (m_maxNodesHint) {
            m_maxNodesHint->setText(LanguageManager::instance().language() == LanguageManager::Language::EN_US
                                        ? QStringLiteral("Max nodes: 50 (fixed)")
                                        : QStringLiteral("節點上限：50（固定）"));
        }
        rebuildTagFilterOptions();
    });

    m_toolbar->show();
    m_filterPanel->show();
}

QStringList GraphWidget::selectedFilterTags() const
{
    QStringList out;
    if (!m_tagFilterList)
        return out;
    for (int i = 0; i < m_tagFilterList->count(); ++i) {
        QListWidgetItem *item = m_tagFilterList->item(i);
        if (!item || item->checkState() != Qt::Checked)
            continue;
        const QString tag = item->data(Qt::UserRole).toString().trimmed();
        if (!tag.isEmpty())
            out << tag;
    }
    return out;
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
    if (!m_tagFilterList) return;

    if (m_filterLabel) m_filterLabel->setText(LanguageManager::instance().getText(QStringLiteral("標籤過濾")));

    const QStringList prevChecked = selectedFilterTags();

    m_tagFilterList->blockSignals(true);
    m_tagFilterList->clear();
    if (tagManager) {
        struct TagRank {
            QString tag;
            int count = 0;
        };
        std::vector<TagRank> ranked;
        ranked.reserve(64);

        for (const QString &t : tagManager->getAllTags()) {
            const QString tag = t.trimmed();
            if (tag.isEmpty())
                continue;

            int count = 0;
            for (const QString &p : tagManager->getFilesByTag(tag)) {
                const QFileInfo fi(p);
                if (fi.exists() && fi.isFile())
                    ++count;
            }
            if (count <= 0)
                continue;
            ranked.push_back(TagRank{tag, count});
        }

        std::sort(ranked.begin(), ranked.end(), [](const TagRank &a, const TagRank &b) {
            if (a.count != b.count)
                return a.count > b.count;
            return a.tag.localeAwareCompare(b.tag) < 0;
        });

        int added = 0;
        for (const TagRank &entry : ranked) {
            auto *item = new QListWidgetItem(translateVirtualTagForDisplay(entry.tag), m_tagFilterList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setData(Qt::UserRole, entry.tag);
            const bool checked = prevChecked.contains(entry.tag) || (prevChecked.isEmpty() && added < 3);
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            ++added;
        }
    }
    m_tagFilterList->blockSignals(false);
}

void GraphWidget::zoomIn()
{
    scaleView(1.2);
}

void GraphWidget::zoomOut()
{
    scaleView(1 / 1.2);
}

namespace {
QRectF sceneBoundsForNodeAt(const Node *node, const QPointF &scenePos)
{
    const QRectF local = node->boundingRect();
    return QRectF(scenePos.x() + local.x(), scenePos.y() + local.y(), local.width(), local.height());
}

bool nodePlacementCollides(QGraphicsScene *scene, const QRectF &candidate, const QList<Node *> &placed)
{
    for (Node *other : placed) {
        if (!other)
            continue;
        if (other->sceneBoundingRect().intersects(candidate))
            return true;
    }

    const QList<QGraphicsItem *> hits = scene->items(candidate, Qt::IntersectsItemBoundingRect);
    for (QGraphicsItem *item : hits) {
        if (qgraphicsitem_cast<Node *>(item))
            return true;
    }
    return false;
}

QPointF findSpiralNodePosition(QGraphicsScene *scene, Node *node, const QList<Node *> &placed, QPointF desired)
{
    const auto boundsAt = [&](const QPointF &pos) { return sceneBoundsForNodeAt(node, pos); };

    if (!nodePlacementCollides(scene, boundsAt(desired), placed))
        return desired;

    for (int step = 1; step < 600; ++step) {
        const qreal theta = step * 0.45;
        const qreal radius = 8.0 + step * 3.0;
        const QPointF pos(desired.x() + radius * qCos(theta), desired.y() + radius * qSin(theta));
        if (!nodePlacementCollides(scene, boundsAt(pos), placed))
            return pos;
    }

    for (int attempt = 0; attempt < 64; ++attempt) {
        const QPointF pos(QRandomGenerator::global()->bounded(920) - 460,
                          QRandomGenerator::global()->bounded(920) - 460);
        if (!nodePlacementCollides(scene, boundsAt(pos), placed))
            return pos;
    }

    return desired;
}
} // namespace

void GraphWidget::buildGraph() {
    scene()->clear();
    fileNodes.clear();
    tagNodes.clear();

    if (!tagManager) return;

    rebuildTagFilterOptions();

    const QStringList filterTags = selectedFilterTags();
    if (filterTags.isEmpty()) {
        return;
    }

    QSet<QString> candidateSet;
    for (const QString &filterTag : filterTags) {
        const std::vector<QString> files = tagManager->getFilesByTag(filterTag);
        for (const auto &p : files)
            candidateSet.insert(p);
    }

    QStringList candidateFiles = candidateSet.values();
    candidateFiles.removeDuplicates();
    std::sort(candidateFiles.begin(), candidateFiles.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });

    const int userCap = MAX_NODES_RENDER;
    if (candidateFiles.size() > userCap) {
        candidateFiles = candidateFiles.mid(0, userCap);
    }

    QStringList tagList = filterTags;
    std::sort(tagList.begin(), tagList.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });
    if (tagList.isEmpty() || candidateFiles.isEmpty()) {
        return;
    }

    // 1) Create Tag Nodes (Blue)
    QList<Node *> placedNodes;
    const int tagCount = tagList.size();
    for (int i = 0; i < tagCount; ++i) {
        const QString &qTag = tagList[i];
        Node *tagNode = new Node(this, Node::Tag, translateVirtualTagForDisplay(qTag));
        const double angle = 2.0 * M_PI * i / std::max(1, tagCount);
        const QPointF desired(260 * cos(angle), 260 * sin(angle));
        tagNode->setPos(findSpiralNodePosition(scene(), tagNode, placedNodes, desired));
        scene()->addItem(tagNode);
        tagNodes[qTag] = tagNode;
        placedNodes.push_back(tagNode);
    }

    // 2) Create File Nodes (Green) + colored edges per selected tag
    for (const QString &fp : candidateFiles) {
        const QFileInfo fi(fp);
        if (!fi.exists() || !fi.isFile())
            continue;

        const auto tags = tagManager->getTags(fp);
        QSet<QString> fileTags;
        for (const auto &t : tags) {
            if (!t.trimmed().isEmpty())
                fileTags.insert(t);
        }

        Node *fileNode = nullptr;
        if (fileNodes.find(fp) == fileNodes.end()) {
            fileNode = new Node(this, Node::File, fi.fileName());
            const QPointF desired(
                QRandomGenerator::global()->bounded(460) - 230,
                QRandomGenerator::global()->bounded(460) - 230);
            fileNode->setPos(findSpiralNodePosition(scene(), fileNode, placedNodes, desired));
            scene()->addItem(fileNode);
            fileNodes[fp] = fileNode;
            placedNodes.push_back(fileNode);
        } else {
            fileNode = fileNodes[fp];
        }

        for (int ti = 0; ti < filterTags.size(); ++ti) {
            const QString &filterTag = filterTags[ti];
            if (!fileTags.contains(filterTag))
                continue;
            Node *tagNode = nullptr;
            const auto tagIt = tagNodes.find(filterTag);
            if (tagIt != tagNodes.end())
                tagNode = tagIt->second;
            if (!tagNode)
                continue;
            scene()->addItem(new Edge(tagNode, fileNode, edgeColorForTag(filterTag, ti)));
        }
    }
}
