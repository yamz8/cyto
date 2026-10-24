#pragma once

#include <QColor>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>

#include "backend.h"

// The dot plot or histogram of the current population, with the gates drawn
// inside it on these axes. Draws new gates with the mouse and moves or
// reshapes existing ones.
class PlotItem : public QQuickPaintedItem {
    Q_OBJECT
    // Named model, so QML can hand it the context property called backend.
    Q_PROPERTY(Backend *model READ backend WRITE setBackend NOTIFY backendChanged)
    // 0 selects and edits, 1 draws rectangles, 2 polygons, 3 ellipses. On a
    // histogram every drawing tool draws a range.
    Q_PROPERTY(int tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QColor accent READ accent WRITE setAccent NOTIFY accentChanged)
    Q_PROPERTY(bool drawing READ drawing NOTIFY drawingChanged)
    // The values under the pointer, for the status line.
    Q_PROPERTY(QString pointer READ pointer NOTIFY pointerChanged)

public:
    enum Tool { Select, RectangleTool, PolygonTool, EllipseTool };

    explicit PlotItem(QQuickItem *parent = nullptr);

    Backend *backend() const { return m_backend; }
    void setBackend(Backend *backend);
    int tool() const { return m_tool; }
    void setTool(int tool);
    QColor accent() const { return m_accent; }
    void setAccent(const QColor &accent);
    bool drawing() const { return !m_polygon.isEmpty() || m_drag == Drag::Creating; }
    QString pointer() const { return m_pointer; }

    Q_INVOKABLE void finishPolygon();
    Q_INVOKABLE void cancelDrawing();

    void paint(QPainter *painter) override;

    // Shared with tests: where a scaled value lands, and back.
    QRectF plotArea() const;
    QPointF toPixel(const QPointF &scaled) const;
    QPointF fromPixel(const QPointF &pixel) const;
    // Where a gate point placed at this pixel goes: on the edge of the plot
    // or past it, far out, so events piled up off scale are included.
    QPointF gatePoint(const QPointF &pixel) const;

signals:
    void backendChanged();
    void toolChanged();
    void accentChanged();
    void drawingChanged();
    void pointerChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;

private:
    enum class Drag { None, Creating, Moving, Vertex };
    struct Axis {
        QString channel;
        Scale scale = Scale::Linear;
        double low = 0, high = 1;
        bool valid = false;
    };
    struct Shown {
        Gate gate;
        QVector<QPointF> points; // in this plot's scaled coordinates
        QString label;
    };

    void stale();
    void rebuild();
    void renderDensity(const std::vector<float> &values, const std::vector<char> &members, int stride, int xi, int yi);
    void renderHistogram(const std::vector<float> &values, const std::vector<char> &members, int stride, int xi);
    QVector<QPointF> viewPoints(const Gate &gate) const;
    QVector<QPointF> handles(Gate::Type type, const QVector<QPointF> &points) const;
    int gateAt(const QPointF &pixel) const;
    bool shownContains(const Shown &shown, const QPointF &scaled) const;
    void drawAxes(QPainter *painter, const QRectF &area);
    void drawGate(QPainter *painter, Gate::Type type, const QVector<QPointF> &points, const QString &label, bool selected);
    void updatePointer(const QPointF &pixel);
    void setCursorFor(const QPointF &pixel);
    const Shown *shown(int id) const;

    QPointer<Backend> m_backend;
    int m_tool = Select;
    QColor m_accent = QColor("#FFD60A");
    QString m_pointer;
    bool m_stale = false;

    Axis m_x, m_y;
    bool m_histogram = false;
    QString m_message; // shown instead of data
    QImage m_density;
    QVector<double> m_bins;
    double m_binMax = 0;
    QVector<Shown> m_shown;
    int m_selected = 0;

    Drag m_drag = Drag::None;
    int m_dragGate = 0;
    int m_vertex = -1;
    QPointF m_dragStart; // scaled
    QPointF m_pressPixel;
    QVector<QPointF> m_dragPoints, m_dragOriginal;
    QVector<QPointF> m_polygon;
    QPointF m_hover{-1, -1};
};
