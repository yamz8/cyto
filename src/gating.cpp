#include "gating.h"

#include <QJsonArray>
#include <QRectF>
#include <cmath>

double Scales::forward(Scale scale, double value) {
    switch (scale) {
    case Scale::Log: return std::log10(std::max(value, 1.0));
    case Scale::Arcsinh: return std::asinh(value / cofactor);
    default: return value;
    }
}

double Scales::inverse(Scale scale, double value) {
    switch (scale) {
    case Scale::Log: return std::pow(10.0, value);
    case Scale::Arcsinh: return std::sinh(value) * cofactor;
    default: return value;
    }
}

QString Scales::id(Scale scale) {
    switch (scale) {
    case Scale::Log: return "log";
    case Scale::Arcsinh: return "arcsinh";
    default: return "linear";
    }
}

std::optional<Scale> Scales::fromId(const QString &id) {
    if (id == "linear")
        return Scale::Linear;
    if (id == "log")
        return Scale::Log;
    if (id == "arcsinh")
        return Scale::Arcsinh;
    return std::nullopt;
}

QString Gate::typeId(Type type) {
    switch (type) {
    case Polygon: return "polygon";
    case Ellipse: return "ellipse";
    case Range: return "range";
    default: return "rectangle";
    }
}

bool Gate::isValid() const {
    if (x.isEmpty() || (!isOneDimensional() && y.isEmpty()))
        return false;
    // JSON can't hold infinities; they would come back as zero.
    for (const QPointF &p : points)
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            return false;
    return type == Polygon ? points.size() >= 3 : points.size() == 2;
}

bool Gate::containsScaled(double sx, double sy) const {
    switch (type) {
    case Range:
        return sx >= std::min(points[0].x(), points[1].x()) && sx <= std::max(points[0].x(), points[1].x());
    case Rectangle:
        return sx >= std::min(points[0].x(), points[1].x()) && sx <= std::max(points[0].x(), points[1].x())
            && sy >= std::min(points[0].y(), points[1].y()) && sy <= std::max(points[0].y(), points[1].y());
    case Ellipse: {
        const QRectF box = QRectF(points[0], points[1]).normalized();
        if (box.width() <= 0 || box.height() <= 0)
            return false;
        const double dx = (sx - box.center().x()) / (box.width() / 2), dy = (sy - box.center().y()) / (box.height() / 2);
        return dx * dx + dy * dy <= 1.0;
    }
    case Polygon: {
        // Even-odd ray casting.
        bool inside = false;
        for (qsizetype i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            const QPointF &a = points[i], &b = points[j];
            if ((a.y() > sy) != (b.y() > sy) && sx < (b.x() - a.x()) * (sy - a.y()) / (b.y() - a.y()) + a.x())
                inside = !inside;
        }
        return inside;
    }
    }
    return false;
}

QJsonObject Gate::toJson() const {
    QJsonArray list;
    for (const QPointF &point : points)
        list.append(isOneDimensional() ? QJsonValue(point.x()) : QJsonValue(QJsonArray{point.x(), point.y()}));
    QJsonObject object{{"id", id}, {"parent", parent}, {"name", name}, {"type", typeId(type)},
                       {"x", x}, {"xScale", Scales::id(xScale)}, {"points", list}};
    if (!isOneDimensional()) {
        object.insert("y", y);
        object.insert("yScale", Scales::id(yScale));
    }
    return object;
}

std::optional<Gate> Gate::fromJson(const QJsonObject &object, QString *error) {
    Gate gate;
    gate.id = object.value("id").toInt();
    gate.parent = object.value("parent").toInt();
    gate.name = object.value("name").toString();
    const QString type = object.value("type").toString();
    bool known = false;
    for (Type t : {Rectangle, Polygon, Ellipse, Range}) {
        if (typeId(t) == type) {
            gate.type = t;
            known = true;
        }
    }
    auto refuse = [&](const QString &message) -> std::optional<Gate> {
        if (error)
            *error = QString("gate %1: %2").arg(gate.name.isEmpty() ? QString::number(gate.id) : gate.name, message);
        return std::nullopt;
    };
    if (gate.id <= 0)
        return refuse("needs an id above 0");
    if (!known)
        return refuse("type must be rectangle, polygon, ellipse, or range");
    gate.x = object.value("x").toString();
    gate.y = object.value("y").toString();
    const auto xScale = Scales::fromId(object.value("xScale").toString("linear"));
    const auto yScale = Scales::fromId(object.value("yScale").toString("linear"));
    if (!xScale || !yScale)
        return refuse("scales must be linear, log, or arcsinh");
    gate.xScale = *xScale;
    gate.yScale = *yScale;
    for (const QJsonValue &value : object.value("points").toArray()) {
        if (gate.isOneDimensional() && value.isDouble())
            gate.points << QPointF(value.toDouble(), 0);
        else if (value.isArray() && value.toArray().size() == 2)
            gate.points << QPointF(value.toArray()[0].toDouble(), value.toArray()[1].toDouble());
        else
            return refuse("points must be [x, y] pairs (numbers for a range)");
    }
    if (!gate.isValid())
        return refuse(gate.isOneDimensional() || gate.type != Polygon
                          ? "needs channels and exactly two points"
                          : "needs channels and at least three points");
    return gate;
}
