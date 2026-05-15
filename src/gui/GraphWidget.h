#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QGraphicsView>
#include <QGraphicsItem>
#include <QHash>
#include <QSet>
#include <QListWidget>
#include <QTabWidget>
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
    Edge(Node *sourceNode, Node *destNode, const QColor &lineColor = QColor(160, 160, 160));

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
    QColor m_lineColor;
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
    void updateDimensions() const;
    QRectF contentRect() const;
    QList<Edge *> edgeList;
    QPointF newPos;
    GraphWidget *graph;
    NodeType m_type;
    QString m_text;
    mutable qreal m_width = 80.0;
    mutable qreal m_height = 40.0;
    bool m_pressedGlow = false;
};

// --- GraphWidget Class ---
class GraphWidget : public QGraphicsView
{
    Q_OBJECT

public:
    GraphWidget(TagManager* tagMgr, QWidget *parent = nullptr);
    
    void itemMoved();
    void buildGraph(); // Rebuilds graph from TagManager
    void setFilterContext(const QString &workspaceRoot, const QHash<QString, QString> &aiTagToDrawerKey);

public slots:
    void zoomIn();
    void zoomOut();
    void resetView();
    void timerEvent(QTimerEvent *event) override;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void scaleView(qreal scaleFactor);
    void resizeEvent(QResizeEvent *event) override;

private:
    static constexpr int kDefaultMaxGraphNodes = 50;
    static constexpr int kMinMaxGraphNodes = 10;
    static constexpr int kMaxMaxGraphNodes = 500;
    static constexpr qreal kGraphSceneHalfExtent = 12000.0;

    void ensureToolbar();
    void loadMaxGraphNodesSetting();
    void saveMaxGraphNodesSetting();
    int countGraphNodesInScene() const;
    void rebuildTagFilterOptions();
    QStringList selectedFilterTags() const;
    QSet<QString> selectedAiDrawerKeys() const;
    QString aiDrawerKeyForLeaf(const QString &leafTag) const;
    static QString drawerTagNodeKey(const QString &drawerKey);
    QColor edgeColorForTag(const QString &tag, int paletteIndex) const;
    void placeNodeWithSpiral(Node *node, const QPointF &center = QPointF(0, 0));
    void fitAllNodes();
    void setFilterPanelExpanded(bool expanded);
    void updateFilterPanelLayout();

    int timerId;
    TagManager* tagManager;
    Node *centerNode;
    
    std::map<QString, Node*> fileNodes;
    std::map<QString, Node*> tagNodes;

    QWidget *m_toolbar = nullptr;
    QWidget *m_filterPanel = nullptr;
    class QPushButton *m_filterExpandBtn = nullptr;
    class QPushButton *m_btnCollapseFilter = nullptr;
    bool m_filterPanelExpanded = true;
    QLabel *m_filterLabel = nullptr;
    QTabWidget *m_tagFilterTabWidget = nullptr;
    QListWidget *m_systemFilterList = nullptr;
    QListWidget *m_aiFilterList = nullptr;
    QString m_workspaceRoot;
    QHash<QString, QString> m_aiTagToDrawerKey;
    QLabel *m_maxNodesHint = nullptr;
    class QPushButton *m_btnResetView = nullptr;
    class QPushButton *m_btnZoomIn = nullptr;
    class QPushButton *m_btnZoomOut = nullptr;
    bool m_canvasPanActive = false;
    QPoint m_canvasPanLastPos;
    QLabel *m_maxNodesSpinLabel = nullptr;
    class QSpinBox *m_maxNodesSpin = nullptr;
    int m_maxGraphNodes = kDefaultMaxGraphNodes;
};

#endif // GRAPHWIDGET_H
