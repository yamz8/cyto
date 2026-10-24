#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>
#include <vector>

// A spillover matrix as instruments write it: row i is how much of channel i's
// dye shows up in every channel, so observed = true × matrix.
struct Spillover {
    QStringList channels;
    QVector<double> matrix; // channels² values, row-major

    bool isEmpty() const { return channels.isEmpty(); }
    double at(int row, int column) const { return matrix.value(row * channels.size() + column); }
    bool operator==(const Spillover &other) const { return channels == other.channels && matrix == other.matrix; }

    // "n,name1,…,nameN,v11,v12,…", the $SPILLOVER keyword's format.
    static Spillover parse(const QString &text);
    QString toKeyword() const;
    static Spillover identity(const QStringList &channels);
};

// One FCS file's events in list mode, every parameter as a float.
struct FcsSample {
    QString path;
    QString name;
    QString version;
    QStringList channels; // $PnN, what gates and matrices refer to
    QStringList labels;   // $PnS, the marker or stain, often empty
    QVector<double> ranges; // $PnR
    qsizetype events = 0;
    std::vector<float> data; // events × channels, row-major
    Spillover spillover;
    QHash<QString, QString> keywords; // upper-cased keys

    int channelIndex(const QString &channel) const { return channels.indexOf(channel); }
    float value(qsizetype event, int channel) const { return data[event * channels.size() + channel]; }
    // "FL1-A · CD4", or just the channel when it has no stain.
    QString displayName(int channel) const;
    // Scatter and time read best on a linear axis, fluorescence on arcsinh.
    bool isScatterOrTime(int channel) const;
};

namespace Fcs {
std::optional<FcsSample> read(const QString &path, QString *error);
std::optional<FcsSample> parse(const QByteArray &bytes, QString *error);
// FCS 3.1, 32-bit floats, little-endian; for tests and made-up demo data.
QByteArray write(const FcsSample &sample);
}

// Inverts the spillover matrix and applies it to a sample's values, so each
// fluorescence channel carries only its own dye.
namespace Compensation {
// The inverse, or nothing when the matrix is singular.
std::optional<QVector<double>> invert(const QVector<double> &matrix, int size);
// Compensated copy of sample.data; channels the matrix names but the sample
// lacks are listed in *missing and leave the data uncompensated.
std::vector<float> apply(const FcsSample &sample, const Spillover &spillover, QStringList *missing, QString *error);
}
