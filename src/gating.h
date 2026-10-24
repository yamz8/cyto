#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <optional>

// How an axis spreads values out. Arcsinh is linear near zero and
// logarithmic further out, so compensated values below zero still show.
enum class Scale { Linear, Log, Arcsinh };

namespace Scales {
constexpr double cofactor = 150.0;
double forward(Scale scale, double value);
double inverse(Scale scale, double value);
QString id(Scale scale);
std::optional<Scale> fromId(const QString &id);
}

// A gate keeps its shape in the scaled coordinates it was drawn in, along
// with those scales, so what it includes doesn't change when the plot's axes
// are switched to another scale later.
struct Gate {
    enum Type { Rectangle, Polygon, Ellipse, Range };

    int id = 0;
    int parent = 0; // 0 is all events
    QString name;
    Type type = Rectangle;
    QString x, y; // channels; a range gate has only x
    Scale xScale = Scale::Linear, yScale = Scale::Linear;
    // Rectangle and ellipse: two opposite corners of the bounding box.
    // Polygon: its vertices. Range: two points whose x are the bounds.
    QVector<QPointF> points;

    bool isOneDimensional() const { return type == Range; }
    // Takes values already scaled with xScale and yScale.
    bool containsScaled(double sx, double sy) const;
    bool contains(double x, double y) const {
        return containsScaled(Scales::forward(xScale, x), isOneDimensional() ? 0 : Scales::forward(yScale, y));
    }
    bool isValid() const;

    QJsonObject toJson() const;
    static std::optional<Gate> fromJson(const QJsonObject &object, QString *error);
    static QString typeId(Type type);
};
