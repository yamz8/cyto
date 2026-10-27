#include "plotitem.h"

#include <QCursor>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {
constexpr double leftMargin = 66, rightMargin = 16, topMargin = 12, bottomMargin = 48;
constexpr double handleSize = 8, hitDistance = 7;

const QColor background("#121215");
const QColor gridColor("#1f1f25");
const QColor axisColor("#3a3a44");
const QColor tickText("#8a8a94");
const QColor titleText("#c8c8d0");

// Density from sparse to dense, the pseudocolor flow cytometry readers know.
QRgb densityColor(double t) {
    static const QColor stops[] = {QColor("#4a63ff"), QColor("#00b7e8"), QColor("#3ccf4e"), QColor("#f5d327"), QColor("#f0412b")};
    t = std::clamp(t, 0.0, 1.0) * 4;
    const int i = std::min(3, int(t));
    const double f = t - i;
    const QColor &a = stops[i], &b = stops[i + 1];
    return qRgb(int(a.red() + (b.red() - a.red()) * f), int(a.green() + (b.green() - a.green()) * f),
                int(a.blue() + (b.blue() - a.blue()) * f));
}

QString superscript(int value) {
    static const QString digits = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
    QString text;
    for (QChar c : QString::number(std::abs(value)))
        text += digits[c.digitValue()];
    return (value < 0 ? QStringLiteral("⁻") : QString()) + text;
}

QString decade(int exponent, bool negative) {
    return (negative ? QStringLiteral("−") : QString()) + QStringLiteral("10") + superscript(exponent);
}

// 12K, 1.5M: short enough for an axis, in the language's digits.
QString shortNumber(double value) {
    const QLocale locale;
    const double magnitude = std::abs(value);
    if (magnitude >= 1e6)
        return locale.toString(value / 1e6, 'g', 3) + QStringLiteral("M");
    if (magnitude >= 1e3)
        return locale.toString(value / 1e3, 'g', 3) + QStringLiteral("K");
    return locale.toString(value, 'g', 3);
}

QString readout(double value) {
    const QLocale locale;
    return std::abs(value) >= 100 ? locale.toString(value, 'f', 0) : locale.toString(value, 'g', 3);
}

struct Tick {
    double scaled;
    QString label;
    bool major;
};

QVector<Tick> ticks(Scale scale, double low, double high, double pixels) {
    QVector<Tick> list;
    if (scale == Scale::Linear) {
        const double span = high - low, rough = span / std::max(2.0, pixels / 80);
        const double power = std::pow(10, std::floor(std::log10(rough)));
        const double step = rough / power < 1.5 ? power : rough / power < 3.5 ? 2 * power : rough / power < 7.5 ? 5 * power : 10 * power;
        for (double v = std::ceil(low / step) * step; v <= high + step * 1e-9; v += step)
            list << Tick{v, shortNumber(std::abs(v) < step * 1e-6 ? 0 : v), true};
        return list;
    }
    if (scale == Scale::Log) {
        for (int k = int(std::floor(low)); k <= int(std::ceil(high)); ++k) {
            list << Tick{double(k), decade(k, false), true};
            for (int m = 2; m <= 9; ++m)
                list << Tick{k + std::log10(m), QString(), false};
        }
        return list;
    }
    // Arcsinh: zero, then decades either side, with minor ticks between.
    list << Tick{0, QStringLiteral("0"), true};
    for (int k = 1; k <= 8; ++k) {
        for (int sign : {1, -1}) {
            const double v = sign * std::pow(10.0, k);
            // 10¹ sits too close to zero to label at this cofactor.
            list << Tick{Scales::forward(scale, v), k > 1 ? decade(k, sign < 0) : QString(), k > 1};
            for (int m = 2; m <= 9; ++m)
                list << Tick{Scales::forward(scale, sign * m * std::pow(10.0, k)), QString(), false};
        }
    }
    for (int m = 2; m <= 9; ++m)
        for (int sign : {1, -1})
            list << Tick{Scales::forward(scale, sign * m * 1.0), QString(), false};
    return list;
}

double distance(const QPointF &a, const QPointF &b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }
}

PlotItem::PlotItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
    setFillColor(background);
}

void PlotItem::setBackend(Backend *backend) {
    if (backend == m_backend)
        return;
    if (m_backend)
        disconnect(m_backend, nullptr, this, nullptr);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &Backend::plotChanged, this, &PlotItem::stale);
        connect(m_backend, &Backend::viewChanged, this, &PlotItem::cancelDrawing);
        connect(m_backend, &Backend::currentPopulationChanged, this, &PlotItem::cancelDrawing);
    }
    emit backendChanged();
    stale();
}

void PlotItem::setTool(int tool) {
    if (tool == m_tool)
        return;
    cancelDrawing();
    m_tool = tool;
    emit toolChanged();
    setCursorFor(m_hover);
}

void PlotItem::setAccent(const QColor &accent) {
    if (accent == m_accent)
        return;
    m_accent = accent;
    emit accentChanged();
    update();
}

// Rebuilt once the current batch of changes is in, on the GUI thread, where
// the analysis lives.
void PlotItem::stale() {
    if (m_stale)
        return;
    m_stale = true;
    QMetaObject::invokeMethod(this, [this] {
        if (m_stale)
            rebuild();
    }, Qt::QueuedConnection);
}

void PlotItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        stale();
}

QRectF PlotItem::plotArea() const {
    return QRectF(leftMargin, topMargin, std::max(10.0, width() - leftMargin - rightMargin),
                  std::max(10.0, height() - topMargin - bottomMargin));
}

QPointF PlotItem::toPixel(const QPointF &scaled) const {
    const QRectF area = plotArea();
    const double x = area.left() + (scaled.x() - m_x.low) / (m_x.high - m_x.low) * area.width();
    const double y = m_histogram ? area.bottom() : area.bottom() - (scaled.y() - m_y.low) / (m_y.high - m_y.low) * area.height();
    return {x, y};
}

QPointF PlotItem::fromPixel(const QPointF &pixel) const {
    const QRectF area = plotArea();
    const double x = m_x.low + (pixel.x() - area.left()) / area.width() * (m_x.high - m_x.low);
    const double y = m_histogram ? 0 : m_y.low + (area.bottom() - pixel.y()) / area.height() * (m_y.high - m_y.low);
    return {x, y};
}

QPointF PlotItem::gatePoint(const QPointF &pixel) const {
    const QRectF area = plotArea();
    QPointF scaled = fromPixel(QPointF(std::clamp(pixel.x(), area.left(), area.right()),
                                       std::clamp(pixel.y(), area.top(), area.bottom())));
    // Far enough out to take in anything off scale, near enough that the
    // value stays finite in raw units when the gate is shown on another
    // scale: a thousand spans on a linear axis, six decades on log or arcsinh.
    auto far = [](const Axis &axis) { return axis.scale == Scale::Linear ? (axis.high - axis.low) * 1000 : 6.0; };
    if (pixel.x() <= area.left() + 1)
        scaled.setX(m_x.low - far(m_x));
    else if (pixel.x() >= area.right() - 1)
        scaled.setX(m_x.high + far(m_x));
    if (!m_histogram) {
        if (pixel.y() >= area.bottom() - 1)
            scaled.setY(m_y.low - far(m_y));
        else if (pixel.y() <= area.top() + 1)
            scaled.setY(m_y.high + far(m_y));
    }
    return scaled;
}

// Gate shapes in this plot's scales; a gate drawn on another scale is
// carried over point by point.
QVector<QPointF> PlotItem::viewPoints(const Gate &gate) const {
    QVector<QPointF> points;
    for (const QPointF &p : gate.points) {
        const double x = gate.xScale == m_x.scale ? p.x() : Scales::forward(m_x.scale, Scales::inverse(gate.xScale, p.x()));
        const double y = gate.isOneDimensional() || gate.yScale == m_y.scale
                             ? p.y() : Scales::forward(m_y.scale, Scales::inverse(gate.yScale, p.y()));
        points << QPointF(x, y);
    }
    return points;
}

void PlotItem::rebuild() {
    m_stale = false;
    m_density = QImage();
    m_bins.clear();
    m_shown.clear();
    m_message.clear();
    m_x.valid = m_y.valid = false;
    update();
    if (!m_backend)
        return;
    Analysis &analysis = m_backend->analysis();
    const int sample = m_backend->currentSample();
    if (sample < 0)
        return;

    m_histogram = m_backend->histogram();
    m_selected = m_backend->selectedGate();
    m_x.channel = m_backend->xChannel();
    m_x.scale = Scale(m_backend->xScale());
    std::tie(m_x.low, m_x.high) = analysis.range(m_x.channel, m_x.scale);
    m_x.valid = true;
    if (!m_histogram) {
        m_y.channel = m_backend->yChannel();
        m_y.scale = Scale(m_backend->yScale());
        std::tie(m_y.low, m_y.high) = analysis.range(m_y.channel, m_y.scale);
        m_y.valid = true;
    }

    const FcsSample &s = *analysis.samples[sample];
    const int population = m_backend->currentPopulation();
    const int xi = s.channelIndex(m_x.channel), yi = m_histogram ? xi : s.channelIndex(m_y.channel);
    if (xi < 0 || yi < 0) {
        m_message = tr("%1 has no %2 channel.").arg(s.name, xi < 0 ? m_x.channel : m_y.channel);
        return;
    }
    const auto &values = analysis.values(sample);
    const auto &members = analysis.members(sample, population);
    if (m_histogram)
        renderHistogram(values, members, int(s.channels.size()), xi);
    else
        renderDensity(values, members, int(s.channels.size()), xi, yi);

    for (int child : analysis.children(population)) {
        const Gate &gate = *analysis.gate(child);
        const bool fits = m_histogram ? gate.isOneDimensional() && gate.x == m_x.channel
                                      : !gate.isOneDimensional() && gate.x == m_x.channel && gate.y == m_y.channel;
        if (!fits)
            continue;
        const PopulationStats stats = analysis.stats(sample, child);
        const QString percent = stats.available ? QLocale().toString(stats.percentOfParent(), 'f', 1) + QStringLiteral(" %")
                                                : QStringLiteral("—");
        m_shown << Shown{gate, viewPoints(gate), gate.name + QStringLiteral("  ") + percent};
    }
}

void PlotItem::renderDensity(const std::vector<float> &values, const std::vector<char> &members, int stride, int xi, int yi) {
    const QRectF area = plotArea();
    const int w = std::max(1, int(area.width())), h = std::max(1, int(area.height()));
    std::vector<quint32> counts(size_t(w) * h, 0);
    const double xs = w / (m_x.high - m_x.low), ys = h / (m_y.high - m_y.low);
    qsizetype shown = 0;
    // Events off the scale pile up on its edge, as they do on the cytometer.
    for (size_t e = 0; e < members.size(); ++e) {
        if (!members[e])
            continue;
        const double fx = (Scales::forward(m_x.scale, values[e * stride + xi]) - m_x.low) * xs;
        const double fy = (Scales::forward(m_y.scale, values[e * stride + yi]) - m_y.low) * ys;
        if (!std::isfinite(fx) || !std::isfinite(fy))
            continue;
        // Clamped as doubles: converting an out-of-range double to int is undefined.
        const int bx = int(std::clamp(fx, 0.0, w - 1.0)), by = int(std::clamp(fy, 0.0, h - 1.0));
        ++counts[size_t(h - 1 - by) * w + bx];
        ++shown;
    }
    if (!shown) {
        m_message = tr("No events in this population.");
        return;
    }
    const quint32 peak = *std::max_element(counts.begin(), counts.end());
    const double scale = 1.0 / std::log1p(double(peak));
    m_density = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    m_density.fill(Qt::transparent);
    for (int row = 0; row < h; ++row) {
        QRgb *line = reinterpret_cast<QRgb *>(m_density.scanLine(row));
        for (int column = 0; column < w; ++column) {
            const quint32 n = counts[size_t(row) * w + column];
            if (n)
                line[column] = densityColor(peak > 1 ? std::log1p(double(n)) * scale : 0.0);
        }
    }
}

void PlotItem::renderHistogram(const std::vector<float> &values, const std::vector<char> &members, int stride, int xi) {
    const int bins = std::clamp(int(plotArea().width() / 3), 32, 256);
    m_bins.fill(0, bins);
    const double xs = bins / (m_x.high - m_x.low);
    qsizetype shown = 0;
    for (size_t e = 0; e < members.size(); ++e) {
        if (!members[e])
            continue;
        const double fx = (Scales::forward(m_x.scale, values[e * stride + xi]) - m_x.low) * xs;
        if (!std::isfinite(fx))
            continue;
        ++m_bins[int(std::clamp(fx, 0.0, bins - 1.0))];
        ++shown;
    }
    if (!shown) {
        m_message = tr("No events in this population.");
        m_bins.clear();
        return;
    }
    m_binMax = *std::max_element(m_bins.begin(), m_bins.end());
    m_y = Axis{QString(), Scale::Linear, 0, m_binMax * 1.08, true};
}

void PlotItem::paint(QPainter *painter) {
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF area = plotArea();
    if (!m_x.valid)
        return;

    drawAxes(painter, area);
    painter->save();
    painter->setClipRect(area.adjusted(-1, -1, 1, 1));
    if (!m_density.isNull()) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter->drawImage(area, m_density);
    }
    if (!m_bins.isEmpty() && m_binMax > 0) {
        QPainterPath path;
        const double step = area.width() / m_bins.size();
        path.moveTo(area.left(), area.bottom());
        for (int i = 0; i < m_bins.size(); ++i) {
            const double y = area.bottom() - m_bins[i] / (m_binMax * 1.08) * area.height();
            path.lineTo(area.left() + i * step, y);
            path.lineTo(area.left() + (i + 1) * step, y);
        }
        path.lineTo(area.right(), area.bottom());
        path.closeSubpath();
        QColor fill = m_accent;
        fill.setAlphaF(0.28);
        painter->fillPath(path, fill);
        painter->setPen(QPen(m_accent, 1.4));
        painter->drawPath(path);
    }

    for (const Shown &s : m_shown) {
        const bool dragged = m_drag != Drag::None && m_drag != Drag::Creating && s.gate.id == m_dragGate;
        drawGate(painter, s.gate.type, dragged ? m_dragPoints : s.points, s.label, s.gate.id == m_selected);
    }
    if (m_drag == Drag::Creating) {
        const Gate::Type type = m_histogram ? Gate::Range : m_tool == EllipseTool ? Gate::Ellipse : Gate::Rectangle;
        drawGate(painter, type, m_dragPoints, QString(), true);
    }
    if (!m_polygon.isEmpty()) {
        QPolygonF line;
        for (const QPointF &p : m_polygon)
            line << toPixel(p);
        if (m_hover.x() >= 0)
            line << m_hover;
        painter->setPen(QPen(m_accent, 1.6, Qt::DashLine));
        painter->drawPolyline(line);
        painter->setBrush(m_accent);
        for (const QPointF &p : m_polygon)
            painter->drawEllipse(toPixel(p), 3, 3);
        painter->setBrush(Qt::NoBrush);
    }
    painter->restore();

    if (!m_message.isEmpty()) {
        painter->setPen(tickText);
        QFont font = painter->font();
        font.setPixelSize(14);
        painter->setFont(font);
        painter->drawText(area, Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }
}

void PlotItem::drawAxes(QPainter *painter, const QRectF &area) {
    QFont font = painter->font();
    font.setPixelSize(11);
    painter->setFont(font);
    const QFontMetricsF metrics(font);

    auto drawTicks = [&](const Axis &axis, bool horizontal) {
        const double pixels = horizontal ? area.width() : area.height();
        double lastLabel = -1e9;
        QVector<Tick> list = ticks(axis.scale, axis.low, axis.high, pixels);
        std::sort(list.begin(), list.end(), [](const Tick &a, const Tick &b) { return a.scaled < b.scaled; });
        for (const Tick &tick : list) {
            if (tick.scaled < axis.low || tick.scaled > axis.high)
                continue;
            const double t = (tick.scaled - axis.low) / (axis.high - axis.low);
            const double at = horizontal ? area.left() + t * area.width() : area.bottom() - t * area.height();
            const double length = tick.major ? 5 : 3;
            if (tick.major) {
                painter->setPen(gridColor);
                if (horizontal)
                    painter->drawLine(QPointF(at, area.top()), QPointF(at, area.bottom()));
                else
                    painter->drawLine(QPointF(area.left(), at), QPointF(area.right(), at));
            }
            painter->setPen(axisColor);
            if (horizontal)
                painter->drawLine(QPointF(at, area.bottom()), QPointF(at, area.bottom() + length));
            else
                painter->drawLine(QPointF(area.left() - length, at), QPointF(area.left(), at));
            // Labels that would crowd the one before are left out.
            if (tick.label.isEmpty())
                continue;
            const double size = horizontal ? metrics.horizontalAdvance(tick.label) + 10 : metrics.height() + 4;
            if (std::abs(at - lastLabel) < size)
                continue;
            lastLabel = at;
            painter->setPen(tickText);
            if (horizontal)
                painter->drawText(QRectF(at - 40, area.bottom() + 6, 80, 16), Qt::AlignHCenter | Qt::AlignTop, tick.label);
            else
                painter->drawText(QRectF(0, at - 8, area.left() - 8, 16), Qt::AlignRight | Qt::AlignVCenter, tick.label);
        }
    };

    drawTicks(m_x, true);
    if (m_y.valid)
        drawTicks(m_y, false);
    painter->setPen(QPen(axisColor, 1));
    painter->drawLine(area.bottomLeft(), area.bottomRight());
    painter->drawLine(area.bottomLeft(), area.topLeft());

    QFont title = font;
    title.setPixelSize(12);
    painter->setFont(title);
    painter->setPen(titleText);
    const QString xTitle = m_backend ? m_backend->analysis().displayName(m_x.channel) : m_x.channel;
    painter->drawText(QRectF(area.left(), area.bottom() + 24, area.width(), 20), Qt::AlignHCenter | Qt::AlignTop, xTitle);
    const QString yTitle = m_histogram ? tr("Events") : m_backend ? m_backend->analysis().displayName(m_y.channel) : m_y.channel;
    painter->save();
    painter->translate(12, area.center().y());
    painter->rotate(-90);
    painter->drawText(QRectF(-area.height() / 2, -10, area.height(), 20), Qt::AlignCenter, yTitle);
    painter->restore();
}

void PlotItem::drawGate(QPainter *painter, Gate::Type type, const QVector<QPointF> &points, const QString &label, bool selected) {
    if (points.size() < 2)
        return;
    const QRectF area = plotArea();
    QPainterPath path;
    QRectF box;
    if (type == Gate::Range) {
        const double a = toPixel(points[0]).x(), b = toPixel(points[1]).x();
        box = QRectF(QPointF(std::min(a, b), area.top()), QPointF(std::max(a, b), area.bottom()));
        path.addRect(box);
    } else if (type == Gate::Polygon) {
        QPolygonF polygon;
        for (const QPointF &p : points)
            polygon << toPixel(p);
        path.addPolygon(polygon);
        path.closeSubpath();
        box = polygon.boundingRect();
    } else {
        box = QRectF(toPixel(points[0]), toPixel(points[1])).normalized();
        if (type == Gate::Ellipse)
            path.addEllipse(box);
        else
            path.addRect(box);
    }
    QColor fill = m_accent;
    fill.setAlphaF(selected ? 0.16 : 0.07);
    painter->fillPath(path, fill);
    painter->setPen(QPen(m_accent, selected ? 2.2 : 1.4));
    painter->drawPath(path);

    if (!label.isEmpty()) {
        QFont font = painter->font();
        font.setPixelSize(12);
        font.setBold(true);
        painter->setFont(font);
        const QFontMetricsF metrics(font);
        const QSizeF size(metrics.horizontalAdvance(label) + 12, metrics.height() + 6);
        // Placed on the part of the gate that's on screen.
        const QRectF seen = box.intersected(area);
        QPointF at(seen.left(), seen.top() - size.height() - 3);
        if (at.y() < area.top())
            at.setY(seen.top() + 3);
        at.setX(std::clamp(at.x(), area.left(), std::max(area.left(), area.right() - size.width())));
        const QRectF tag(at, size);
        QColor plate = background;
        plate.setAlphaF(0.85);
        painter->setPen(Qt::NoPen);
        painter->setBrush(plate);
        painter->drawRect(tag);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(m_accent);
        painter->drawText(tag, Qt::AlignCenter, label);
    }

    if (selected) {
        painter->setPen(QPen(background, 1));
        painter->setBrush(m_accent);
        for (const QPointF &h : handles(type, points)) {
            const QPointF p = type == Gate::Range ? QPointF(toPixel(h).x(), area.center().y()) : toPixel(h);
            painter->drawRect(QRectF(p.x() - handleSize / 2, p.y() - handleSize / 2, handleSize, handleSize));
        }
        painter->setBrush(Qt::NoBrush);
    }
}

// Where a gate can be grabbed to reshape it, in scaled coordinates.
QVector<QPointF> PlotItem::handles(Gate::Type type, const QVector<QPointF> &points) const {
    if (type == Gate::Polygon || type == Gate::Range)
        return points;
    const QPointF a = points[0], b = points[1];
    return {a, QPointF(b.x(), a.y()), b, QPointF(a.x(), b.y())};
}

const PlotItem::Shown *PlotItem::shown(int id) const {
    for (const Shown &s : m_shown)
        if (s.gate.id == id)
            return &s;
    return nullptr;
}

bool PlotItem::shownContains(const Shown &s, const QPointF &scaled) const {
    Gate probe = s.gate;
    probe.points = s.points;
    return probe.containsScaled(scaled.x(), scaled.y());
}

int PlotItem::gateAt(const QPointF &pixel) const {
    const QPointF scaled = fromPixel(pixel);
    // The selected gate first, then the most recently drawn.
    if (const Shown *s = shown(m_selected); s && shownContains(*s, scaled))
        return s->gate.id;
    for (auto it = m_shown.crbegin(); it != m_shown.crend(); ++it)
        if (shownContains(*it, scaled))
            return it->gate.id;
    return 0;
}

void PlotItem::mousePressEvent(QMouseEvent *event) {
    if (!m_x.valid) {
        event->ignore();
        return;
    }
    const QPointF pixel = event->position();
    if (event->button() == Qt::RightButton) {
        if (!m_polygon.isEmpty())
            finishPolygon();
        else
            cancelDrawing();
        return;
    }
    const QPointF scaled = gatePoint(pixel);
    m_pressPixel = pixel;

    if (m_tool == PolygonTool && !m_histogram) {
        if (m_polygon.size() >= 3 && distance(toPixel(m_polygon.first()), pixel) < hitDistance + 2) {
            finishPolygon();
            return;
        }
        m_polygon << scaled;
        emit drawingChanged();
        update();
        return;
    }
    if (m_tool != Select) {
        m_drag = Drag::Creating;
        m_dragStart = scaled;
        m_dragPoints = {scaled, scaled};
        emit drawingChanged();
        update();
        return;
    }

    // Reshape the selected gate by a handle, or pick up whatever is under the pointer.
    if (const Shown *s = shown(m_selected)) {
        const QVector<QPointF> list = handles(s->gate.type, s->points);
        for (int i = 0; i < list.size(); ++i) {
            const QPointF p = s->gate.type == Gate::Range ? QPointF(toPixel(list[i]).x(), pixel.y()) : toPixel(list[i]);
            if (distance(p, pixel) <= hitDistance) {
                m_drag = Drag::Vertex;
                m_dragGate = s->gate.id;
                m_vertex = i;
                m_dragOriginal = m_dragPoints = s->points;
                m_dragStart = scaled;
                return;
            }
        }
    }
    const int id = gateAt(pixel);
    if (m_backend)
        m_backend->setSelectedGate(id);
    m_selected = id;
    if (const Shown *s = shown(id)) {
        m_drag = Drag::Moving;
        m_dragGate = id;
        m_dragOriginal = m_dragPoints = s->points;
        m_dragStart = fromPixel(pixel);
        setCursor(Qt::ClosedHandCursor);
    }
    update();
}

void PlotItem::mouseMoveEvent(QMouseEvent *event) {
    const QPointF pixel = event->position();
    const QPointF scaled = gatePoint(pixel);
    m_hover = pixel;
    updatePointer(pixel);
    switch (m_drag) {
    case Drag::Creating:
        m_dragPoints = {m_dragStart, scaled};
        break;
    case Drag::Moving: {
        const QPointF delta = fromPixel(pixel) - m_dragStart;
        m_dragPoints = m_dragOriginal;
        for (QPointF &p : m_dragPoints)
            p += m_histogram ? QPointF(delta.x(), 0) : delta;
        break;
    }
    case Drag::Vertex: {
        const Shown *s = shown(m_dragGate);
        if (!s)
            break;
        m_dragPoints = m_dragOriginal;
        if (s->gate.type == Gate::Polygon || s->gate.type == Gate::Range) {
            m_dragPoints[m_vertex] = m_histogram ? QPointF(scaled.x(), 0) : scaled;
        } else {
            // Corners 0 and 2 are the stored points; 1 and 3 mix their coordinates.
            QPointF &a = m_dragPoints[0], &b = m_dragPoints[1];
            switch (m_vertex) {
            case 0: a = scaled; break;
            case 1: b.setX(scaled.x()); a.setY(scaled.y()); break;
            case 2: b = scaled; break;
            default: a.setX(scaled.x()); b.setY(scaled.y()); break;
            }
        }
        break;
    }
    case Drag::None:
        break;
    }
    update();
}

void PlotItem::mouseReleaseEvent(QMouseEvent *event) {
    const Drag drag = m_drag;
    m_drag = Drag::None;
    const bool moved = distance(event->position(), m_pressPixel) >= 3;
    if (drag == Drag::Creating) {
        emit drawingChanged();
        if (moved && m_backend) {
            QVector<QPointF> points = m_dragPoints;
            Gate::Type type = m_tool == EllipseTool ? Gate::Ellipse : Gate::Rectangle;
            if (m_histogram) {
                type = Gate::Range;
                points = {QPointF(std::min(points[0].x(), points[1].x()), 0), QPointF(std::max(points[0].x(), points[1].x()), 0)};
            }
            m_backend->addGate(type, points);
            setTool(Select);
        }
    } else if ((drag == Drag::Moving || drag == Drag::Vertex) && moved && m_backend) {
        m_backend->updateGate(m_dragGate, m_dragPoints, m_x.scale, m_histogram ? Scale::Linear : m_y.scale);
    }
    setCursorFor(event->position());
    update();
}

void PlotItem::mouseDoubleClickEvent(QMouseEvent *event) {
    if (!m_polygon.isEmpty()) {
        // The double click's first press already placed a vertex here.
        if (m_polygon.size() > 1 && distance(toPixel(m_polygon.last()), toPixel(m_polygon[m_polygon.size() - 2])) < 4)
            m_polygon.removeLast();
        finishPolygon();
        return;
    }
    if (m_tool == Select && m_backend) {
        const int id = gateAt(event->position());
        if (id)
            m_backend->openGate(id);
    }
}

void PlotItem::hoverMoveEvent(QHoverEvent *event) {
    m_hover = event->position();
    updatePointer(m_hover);
    setCursorFor(m_hover);
    if (!m_polygon.isEmpty())
        update();
}

void PlotItem::hoverLeaveEvent(QHoverEvent *) {
    m_hover = QPointF(-1, -1);
    m_pointer.clear();
    emit pointerChanged();
    if (!m_polygon.isEmpty())
        update();
}

void PlotItem::updatePointer(const QPointF &pixel) {
    QString text;
    if (m_x.valid && plotArea().contains(pixel)) {
        const QPointF scaled = fromPixel(pixel);
        text = m_x.channel + QStringLiteral(" ") + readout(Scales::inverse(m_x.scale, scaled.x()));
        if (!m_histogram)
            text += QStringLiteral("   ") + m_y.channel + QStringLiteral(" ") + readout(Scales::inverse(m_y.scale, scaled.y()));
    }
    if (text != m_pointer) {
        m_pointer = text;
        emit pointerChanged();
    }
}

void PlotItem::setCursorFor(const QPointF &pixel) {
    if (m_tool != Select) {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (const Shown *s = shown(m_selected)) {
        for (const QPointF &h : handles(s->gate.type, s->points)) {
            const QPointF p = s->gate.type == Gate::Range ? QPointF(toPixel(h).x(), pixel.y()) : toPixel(h);
            if (distance(p, pixel) <= hitDistance) {
                setCursor(s->gate.type == Gate::Range ? Qt::SizeHorCursor : Qt::SizeAllCursor);
                return;
            }
        }
    }
    setCursor(gateAt(pixel) ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void PlotItem::finishPolygon() {
    QVector<QPointF> points = m_polygon;
    m_polygon.clear();
    emit drawingChanged();
    update();
    if (points.size() >= 3 && m_backend) {
        m_backend->addGate(Gate::Polygon, points);
        setTool(Select);
    }
}

void PlotItem::cancelDrawing() {
    if (m_polygon.isEmpty() && m_drag == Drag::None)
        return;
    m_polygon.clear();
    m_drag = Drag::None;
    emit drawingChanged();
    update();
}
