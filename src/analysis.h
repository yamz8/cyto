#pragma once

#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <memory>
#include <unordered_map>
#include <optional>

#include "fcs.h"
#include "gating.h"

struct PopulationStats {
    qint64 count = 0;
    qint64 parentCount = 0;
    qint64 total = 0;
    // False when a gate on the way down uses a channel the sample lacks.
    bool available = true;

    double percentOfParent() const { return parentCount ? 100.0 * count / parentCount : 0.0; }
    double percentOfTotal() const { return total ? 100.0 * count / total : 0.0; }
};

// The whole analysis: the samples, one gate tree applied to every one of
// them, and the compensation matrix. Population 0 is all events; every gate
// is a population inside its parent. Derived data is cached per sample until
// the gates or the compensation change.
class Analysis {
public:
    // Shared, so copies and undo snapshots don't copy events.
    QVector<std::shared_ptr<const FcsSample>> samples;
    QVector<Gate> gates;
    Spillover compensation;
    bool compensate = false;
    QHash<QString, Scale> scales;

    static constexpr const char *allEvents = "All events";

    // Adopts the sample's own spillover matrix when there is none yet.
    void addSample(std::shared_ptr<const FcsSample> sample);
    void removeSample(int index);

    // The first sample's channels, then any the others add.
    QStringList channels() const;
    QString displayName(const QString &channel) const;
    Scale scale(const QString &channel) const;
    bool hasChannel(const QString &channel) const { return channels().contains(channel); }

    const Gate *gate(int id) const;
    int gateIndex(int id) const;
    QVector<int> children(int population) const;
    // Every population, parents before their children, starting with 0.
    QVector<int> populationOrder() const;
    QVector<int> descendants(int population) const;
    int depth(int population) const;
    QString name(int population) const;
    // "Lymphocytes/CD4+", unique for a population even when names repeat.
    QString path(int population) const;
    int nextGateId() const;
    QString uniqueName(const QString &base) const;

    // Values after compensation when it is on.
    const std::vector<float> &values(int sample);
    // Why a sample can't be compensated, or empty.
    QString compensationProblem(int sample);
    const std::vector<char> &members(int sample, int population);
    PopulationStats stats(int sample, int population);
    double median(int sample, int population, const QString &channel);
    // Scaled axis bounds that fit nearly every event of every sample.
    QPair<double, double> range(const QString &channel, Scale scale);

    // Drop what the gates or the compensation decided.
    void gatesChanged();
    void compensationChanged();

    // One row per sample and population: counts, percentages, and the
    // median of every channel.
    QByteArray statsCsv();
    QJsonArray statsJson();

    QJsonObject toJson(const QDir &base) const;
    // Samples load relative to base. Problems with individual files are
    // listed but don't stop the rest from loading; a malformed workspace does.
    static std::optional<Analysis> fromJson(const QByteArray &bytes, const QDir &base, QStringList *problems,
                                            bool loadSamples = true);

private:
    struct Cache {
        bool ready = false;
        std::vector<float> values;
        QString problem;
        // References into these stay valid as entries are added.
        std::unordered_map<int, std::vector<char>> members;
    };
    Cache &cache(int sample);

    std::unordered_map<const FcsSample *, Cache> m_cache;
    QHash<QString, QPair<double, double>> m_ranges;
};
