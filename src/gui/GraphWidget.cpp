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
    
    std::vector<QString> allTags = tagManager->getAllTags();
    if (allTags.empty()) {
        //scene()->addText("No tags found.", QFont("Arial", 20))->setDefaultTextColor(Qt::white);
        return;
    }

    // 1. Create Tag Nodes (Blue)
    int i = 0;
    int count = allTags.size();
    for (const QString& qTag : allTags) {
        Node* tagNode = new Node(this, Node::Tag, qTag);
        
        // Distribute in a circle
        double angle = 2.0 * M_PI * i / count;
        tagNode->setPos(200 * cos(angle), 200 * sin(angle));
        
        scene()->addItem(tagNode);
        tagNodes[qTag] = tagNode;
        i++;
    }
    
    // 2. Create File Nodes (Green) for files that have tags
    // This is a bit inefficient (O(Tags * Files)), but fine for MVP
    for (const auto& qTag : allTags) {
        Node* tagNode = tagNodes[qTag];
        
        std::vector<QString> files = tagManager->getFilesByTag(qTag);
        for (const auto& qFileFullPath : files) {
            QString qFile = QString::fromStdString(std::filesystem::path(qFileFullPath.toStdString()).filename().string());
            
            Node* fileNode;
            if (fileNodes.find(qFile) == fileNodes.end()) {
                fileNode = new Node(this, Node::File, qFile);
                fileNode->setPos(
                    QRandomGenerator::global()->bounded(400) - 200, 
                    QRandomGenerator::global()->bounded(400) - 200
                ); // Random pos near center
                scene()->addItem(fileNode);
                fileNodes[qFile] = fileNode;
            } else {
                fileNode = fileNodes[qFile];
            }
            
            // Create Edge
            scene()->addItem(new Edge(tagNode, fileNode));
        }
    }
}
