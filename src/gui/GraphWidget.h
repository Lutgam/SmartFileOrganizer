#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QGraphicsView>
#include <QGraphicsItem>
#include <vector>
#include <map>

class Node;
class Edge;
class TagManager;
class GraphWidget; // Forward declaration
class QComboBox;
class QLabel;
class QWidget;

// --- Edge Class ---
class Edge : public QGraphicsItem
{
public:
    Edge(Node *sourceNode, Node *destNode);

    Node *sourceNode() const { return source; }
    Node *destNode() const { return dest; }

    void adjust();

    enum { Type = UserType + 2 };
    int type() const override { return Type; }

protected:
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    Node *source, *dest;
    QPointF sourcePoint;
    QPointF destPoint;
    qreal arrowSize;
};

// --- Node Class ---
class Node : public QGraphicsItem
{
public:
    enum NodeType { File, Tag };
    
    Node(GraphWidget *graph, NodeType type, const QString &text);

    void addEdge(Edge *edge);
    QList<Edge *> edges() const;
    int type() const override { return Type; }
    enum { Type = UserType + 1 };

    void calculateForces();
    bool advancePosition();

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QString text() const { return m_text; }
    NodeType nodeType() const { return m_type; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QList<Edge *> edgeList;
    QPointF newPos;
    GraphWidget *graph;
    NodeType m_type;
    QString m_text;
};

// --- GraphWidget Class ---
class GraphWidget : public QGraphicsView
{
    Q_OBJECT

public:
    GraphWidget(TagManager* tagMgr, QWidget *parent = nullptr);
    
    void itemMoved();
    void buildGraph(); // Rebuilds graph from TagManager

public slots:
    void zoomIn();
    void zoomOut();
    void timerEvent(QTimerEvent *event) override;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void scaleView(qreal scaleFactor);
    void resizeEvent(QResizeEvent *event) override;

private:
    static constexpr int MAX_NODES_RENDER = 50;

    void ensureToolbar();
    void rebuildTagFilterOptions();
    QString selectedFilterTag() const;

    int timerId;
    TagManager* tagManager;
    Node *centerNode;
    
    std::map<QString, Node*> fileNodes;
    std::map<QString, Node*> tagNodes;

    QWidget *m_toolbar = nullptr;
    QLabel *m_filterLabel = nullptr;
    QComboBox *m_tagFilter = nullptr;
    QLabel *m_maxNodesLabel = nullptr;
    QComboBox *m_maxNodesCombo = nullptr;
};

#endif // GRAPHWIDGET_H
