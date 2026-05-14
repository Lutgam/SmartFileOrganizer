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
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSet>
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
Edge::Edge(Node *sourceNode, Node *destNode)
    : source(sourceNode), dest(destNode)
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

    painter->setPen(QPen(Qt::gray, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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

QRectF Node::boundingRect() const
{
    qreal adjust = 2;
    // Tags might be wider
    int width = 20 + m_text.length() * 6; // Rough estimate
    return QRectF(-width/2 - adjust, -15 - adjust, width + adjust, 30 + adjust);
}

QPainterPath Node::shape() const
{
    QPainterPath path;
    int width = 20 + m_text.length() * 6;
    path.addRoundedRect(-width/2, -15, width, 30, 5, 5);
    return path;
}

void Node::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    // Shadow
    painter->setPen(Qt::NoPen);
    painter->setBrush(Qt::darkGray);
    int width = 20 + m_text.length() * 6;
    painter->drawRoundedRect(-width/2 + 3, -15 + 3, width, 30, 5, 5);

    // Body
    QColor color;
    if (m_type == File) {
        color = QColor(100, 200, 100); // Green
    } else {
        color = QColor(100, 150, 255); // Blue
    }
    
    QRadialGradient gradient(-3, -3, width/2);
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
    painter->drawRoundedRect(-width/2, -15, width, 30, 5, 5);
    
    // Text
    painter->setPen(Qt::black);
    painter->drawText(boundingRect(), Qt::AlignCenter, m_text);
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
    scale(0.8, 0.8);
    setMinimumSize(400, 400);

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
    if (!m_toolbar) return;
    const int margin = 10;
    const QSize s = m_toolbar->sizeHint();
    m_toolbar->setGeometry(width() - s.width() - margin, margin, s.width(), s.height());
}

void GraphWidget::ensureToolbar() {
    if (m_toolbar) return;

    m_toolbar = new QWidget(this);
    m_toolbar->setObjectName(QStringLiteral("graphToolbar"));
    m_toolbar->setStyleSheet(QStringLiteral(
        "QWidget#graphToolbar { background: rgba(30,30,30,200); border: 1px solid rgba(255,255,255,40); border-radius: 8px; }"
        "QLabel { color: white; }"
        "QComboBox { padding: 2px 6px; }"));

    auto *row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(10, 8, 10, 8);
    row->setSpacing(8);

    m_filterLabel = new QLabel(QStringLiteral("標籤過濾"), m_toolbar);
    row->addWidget(m_filterLabel);

    m_tagFilter = new QComboBox(m_toolbar);
    row->addWidget(m_tagFilter, 1);

    connect(m_tagFilter, &QComboBox::currentIndexChanged, this, [this](int) { buildGraph(); });
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        if (m_filterLabel) m_filterLabel->setText(LanguageManager::instance().getText(QStringLiteral("標籤過濾")));
        if (m_maxNodesHint) {
            m_maxNodesHint->setText(LanguageManager::instance().language() == LanguageManager::Language::EN_US
                                        ? QStringLiteral("Max nodes: 50 (fixed)")
                                        : QStringLiteral("節點上限：50（固定）"));
        }
        rebuildTagFilterOptions();
    });

    m_maxNodesHint = new QLabel(QStringLiteral("節點上限：50（固定）"), m_toolbar);
    row->addWidget(m_maxNodesHint);

    m_toolbar->show();
}

QString GraphWidget::selectedFilterTag() const {
    if (!m_tagFilter || m_tagFilter->count() <= 0) return {};
    return m_tagFilter->currentData().toString().trimmed();
}

void GraphWidget::rebuildTagFilterOptions() {
    ensureToolbar();
    if (!m_tagFilter) return;

    if (m_filterLabel) m_filterLabel->setText(LanguageManager::instance().getText(QStringLiteral("標籤過濾")));

    const QString prev = selectedFilterTag();

    m_tagFilter->blockSignals(true);
    m_tagFilter->clear();
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

        constexpr int kCrowdedTagThreshold = 30;
        const bool crowded = static_cast<int>(ranked.size()) > kCrowdedTagThreshold;
        if (crowded) {
            ranked.erase(std::remove_if(ranked.begin(), ranked.end(),
                                        [](const TagRank &entry) { return entry.count <= 1; }),
                         ranked.end());
        }
        if (ranked.empty() && crowded) {
            ranked.clear();
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
        }

        std::sort(ranked.begin(), ranked.end(), [](const TagRank &a, const TagRank &b) {
            if (a.count != b.count)
                return a.count > b.count;
            return a.tag.localeAwareCompare(b.tag) < 0;
        });

        for (const TagRank &entry : ranked)
            m_tagFilter->addItem(translateVirtualTagForDisplay(entry.tag), entry.tag);
    }
    // restore selection if possible; otherwise first tag (no global "show all")
    if (m_tagFilter->count() > 0) {
        const int idx = prev.isEmpty() ? 0 : m_tagFilter->findData(prev);
        m_tagFilter->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_tagFilter->blockSignals(false);
}

void GraphWidget::zoomIn()
{
    scaleView(1.2);
}

void GraphWidget::zoomOut()
{
    scaleView(1 / 1.2);
}

void GraphWidget::buildGraph() {
    scene()->clear();
    fileNodes.clear();
    tagNodes.clear();

    if (!tagManager) return;

    rebuildTagFilterOptions();

    const QString filterTag = selectedFilterTag();
    if (filterTag.isEmpty()) {
        return;
    }

    QStringList candidateFiles;
    const std::vector<QString> files = tagManager->getFilesByTag(filterTag);
    for (const auto &p : files) candidateFiles.push_back(p);
    candidateFiles.removeDuplicates();
    std::sort(candidateFiles.begin(), candidateFiles.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });

    const int userCap = MAX_NODES_RENDER;
    if (candidateFiles.size() > userCap) {
        candidateFiles = candidateFiles.mid(0, userCap);
    }
    
    // Decide which tag nodes should exist
    QSet<QString> tagsToRender;
    if (!filterTag.isEmpty()) {
        tagsToRender.insert(filterTag);
    } else {
        for (const QString &fp : candidateFiles) {
            const auto tags = tagManager->getTags(fp);
            for (const auto &t : tags) {
                if (!t.trimmed().isEmpty()) tagsToRender.insert(t);
            }
        }
    }
    if (tagsToRender.isEmpty() || candidateFiles.isEmpty()) {
        return;
    }

    QStringList tagList = tagsToRender.values();
    std::sort(tagList.begin(), tagList.end(), [](const QString &a, const QString &b) {
        return a.localeAwareCompare(b) < 0;
    });

    // 1) Create Tag Nodes (Blue)
    const int tagCount = tagList.size();
    for (int i = 0; i < tagCount; ++i) {
        const QString &qTag = tagList[i];
        Node *tagNode = new Node(this, Node::Tag, translateVirtualTagForDisplay(qTag));
        const double angle = 2.0 * M_PI * i / std::max(1, tagCount);
        tagNode->setPos(220 * cos(angle), 220 * sin(angle));
        scene()->addItem(tagNode);
        tagNodes[qTag] = tagNode;
    }

    // 2) Create File Nodes (Green) + Edges
    for (const QString &fp : candidateFiles) {
        const QFileInfo fi(fp);
        if (!fi.exists() || !fi.isFile()) continue;

        // Filter logic: if a tag is selected, only include files that have that tag.
        if (!filterTag.isEmpty()) {
            const auto tags = tagManager->getTags(fp);
            bool ok = false;
            for (const auto &t : tags) {
                if (t == filterTag) {
                    ok = true;
                    break;
                }
            }
            if (!ok) continue;
        }

        Node *fileNode = nullptr;
        if (fileNodes.find(fp) == fileNodes.end()) {
            fileNode = new Node(this, Node::File, fi.fileName());
            fileNode->setPos(
                QRandomGenerator::global()->bounded(400) - 200,
                QRandomGenerator::global()->bounded(400) - 200);
            scene()->addItem(fileNode);
            fileNodes[fp] = fileNode;
        } else {
            fileNode = fileNodes[fp];
        }

        if (!filterTag.isEmpty()) {
            Node *tagNode = tagNodes[filterTag];
            if (tagNode) scene()->addItem(new Edge(tagNode, fileNode));
            continue;
        }

        const auto tags = tagManager->getTags(fp);
        for (const auto &t : tags) {
            auto it = tagNodes.find(t);
            if (it == tagNodes.end()) continue;
            scene()->addItem(new Edge(it->second, fileNode));
        }
    }
}
