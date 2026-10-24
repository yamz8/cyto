#include "analysis.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Analysis", text); }

QByteArray csvField(const QString &text) {
    if (!text.contains(',') && !text.contains('"') && !text.contains('\n'))
        return text.toUtf8();
    QString quoted = text;
    quoted.replace('"', "\"\"");
    return '"' + quoted.toUtf8() + '"';
}

// Numbers in files are always written the same way, whatever the language.
QByteArray number(double value, int decimals) { return QByteArray::number(value, 'f', decimals); }
}

void Analysis::addSample(std::shared_ptr<const FcsSample> sample) {
    if (compensation.isEmpty() && !sample->spillover.isEmpty()) {
        compensation = sample->spillover;
        compensate = true;
    }
    samples.append(std::move(sample));
    m_ranges.clear();
}

void Analysis::removeSample(int index) {
    if (index < 0 || index >= samples.size())
        return;
    m_cache.erase(samples[index].get());
    samples.removeAt(index);
    m_ranges.clear();
}

QStringList Analysis::channels() const {
    QStringList list;
    for (const auto &sample : samples)
        for (const QString &channel : sample->channels)
            if (!list.contains(channel))
                list << channel;
    return list;
}

QString Analysis::displayName(const QString &channel) const {
    for (const auto &sample : samples) {
        const int index = sample->channelIndex(channel);
        if (index >= 0)
            return sample->displayName(index);
    }
    return channel;
}

Scale Analysis::scale(const QString &channel) const {
    if (scales.contains(channel))
        return scales.value(channel);
    for (const auto &sample : samples) {
        const int index = sample->channelIndex(channel);
        if (index >= 0)
            return sample->isScatterOrTime(index) ? Scale::Linear : Scale::Arcsinh;
    }
    return Scale::Linear;
}

const Gate *Analysis::gate(int id) const {
    const int index = gateIndex(id);
    return index >= 0 ? &gates[index] : nullptr;
}

int Analysis::gateIndex(int id) const {
    for (int i = 0; i < gates.size(); ++i)
        if (gates[i].id == id)
            return i;
    return -1;
}

QVector<int> Analysis::children(int population) const {
    QVector<int> list;
    for (const Gate &gate : gates)
        if (gate.parent == population)
            list << gate.id;
    return list;
}

QVector<int> Analysis::populationOrder() const {
    QVector<int> order;
    std::function<void(int)> visit = [&](int id) {
        order << id;
        for (int child : children(id))
            visit(child);
    };
    visit(0);
    return order;
}

QVector<int> Analysis::descendants(int population) const {
    QVector<int> list;
    for (int child : children(population))
        list << child << descendants(child);
    return list;
}

int Analysis::depth(int population) const {
    int depth = 0;
    for (const Gate *g = gate(population); g && depth < 64; g = gate(g->parent))
        ++depth;
    return depth;
}

QString Analysis::name(int population) const {
    const Gate *g = gate(population);
    return g ? g->name : QString(allEvents);
}

QString Analysis::path(int population) const {
    QStringList parts;
    for (const Gate *g = gate(population); g && parts.size() < 64; g = gate(g->parent))
        parts.prepend(g->name);
    return parts.isEmpty() ? QString(allEvents) : parts.join('/');
}

int Analysis::nextGateId() const {
    int id = 0;
    for (const Gate &gate : gates)
        id = std::max(id, gate.id);
    return id + 1;
}

QString Analysis::uniqueName(const QString &base) const {
    auto taken = [&](const QString &name) {
        return std::any_of(gates.begin(), gates.end(), [&](const Gate &g) { return g.name == name; });
    };
    for (int n = 1;; ++n) {
        const QString name = base + QString::number(n);
        if (!taken(name))
            return name;
    }
}

Analysis::Cache &Analysis::cache(int sample) {
    Cache &entry = m_cache[samples[sample].get()];
    if (!entry.ready) {
        entry.ready = true;
        entry.problem.clear();
        const FcsSample &s = *samples[sample];
        if (compensate && !compensation.isEmpty()) {
            QStringList missing;
            QString error;
            entry.values = Compensation::apply(s, compensation, &missing, &error);
            if (!missing.isEmpty())
                entry.problem = tr("%1 lacks %2 from the compensation matrix, so it is shown uncompensated.")
                                    .arg(s.name, missing.join(", "));
            else if (!error.isEmpty())
                entry.problem = error;
        } else {
            entry.values = s.data;
        }
    }
    return entry;
}

const std::vector<float> &Analysis::values(int sample) { return cache(sample).values; }

QString Analysis::compensationProblem(int sample) { return cache(sample).problem; }

const std::vector<char> &Analysis::members(int sample, int population) {
    Cache &entry = cache(sample);
    auto found = entry.members.find(population);
    if (found != entry.members.end())
        return found->second;

    const FcsSample &s = *samples[sample];
    std::vector<char> result;
    const Gate *g = gate(population);
    if (!g) {
        result.assign(s.events, 1);
    } else {
        // Copied, since computing the parent may grow the hash under us.
        const std::vector<char> parent = members(sample, g->parent);
        result.assign(s.events, 0);
        const int xi = s.channelIndex(g->x), yi = g->isOneDimensional() ? xi : s.channelIndex(g->y);
        if (xi >= 0 && yi >= 0) {
            const int stride = s.channels.size();
            const float *data = entry.values.data();
            for (qsizetype e = 0; e < s.events; ++e)
                if (parent[e])
                    result[e] = g->contains(data[e * stride + xi], data[e * stride + yi]);
        }
    }
    return entry.members.emplace(population, std::move(result)).first->second;
}

PopulationStats Analysis::stats(int sample, int population) {
    PopulationStats stats;
    const FcsSample &s = *samples[sample];
    stats.total = s.events;
    for (const Gate *g = gate(population); g; g = gate(g->parent))
        if (s.channelIndex(g->x) < 0 || (!g->isOneDimensional() && s.channelIndex(g->y) < 0))
            stats.available = false;
    const auto &mine = members(sample, population);
    stats.count = std::count(mine.begin(), mine.end(), 1);
    const Gate *g = gate(population);
    if (g) {
        const auto &parent = members(sample, g->parent);
        stats.parentCount = std::count(parent.begin(), parent.end(), 1);
    } else {
        stats.parentCount = s.events;
    }
    return stats;
}

double Analysis::median(int sample, int population, const QString &channel) {
    const FcsSample &s = *samples[sample];
    const int index = s.channelIndex(channel);
    if (index < 0)
        return std::nan("");
    const auto &mine = members(sample, population);
    const auto &data = values(sample);
    const int stride = s.channels.size();
    std::vector<float> picked;
    for (qsizetype e = 0; e < s.events; ++e)
        if (mine[e])
            picked.push_back(data[e * stride + index]);
    if (picked.empty())
        return std::nan("");
    const size_t middle = picked.size() / 2;
    std::nth_element(picked.begin(), picked.begin() + middle, picked.end());
    if (picked.size() % 2)
        return picked[middle];
    const float upper = picked[middle];
    return (upper + *std::max_element(picked.begin(), picked.begin() + middle)) / 2.0;
}

QPair<double, double> Analysis::range(const QString &channel, Scale scale) {
    const QString key = channel + '|' + Scales::id(scale) + '|' + (compensate ? "c" : "r");
    if (m_ranges.contains(key))
        return m_ranges.value(key);

    // A sample of every file is plenty to place the axes.
    std::vector<double> picked;
    for (int i = 0; i < samples.size(); ++i) {
        const FcsSample &s = *samples[i];
        const int index = s.channelIndex(channel);
        if (index < 0 || s.events == 0)
            continue;
        const auto &data = values(i);
        const qsizetype step = std::max<qsizetype>(1, s.events / 50000);
        for (qsizetype e = 0; e < s.events; e += step)
            picked.push_back(Scales::forward(scale, data[e * s.channels.size() + index]));
    }
    double low = 0, high = 1;
    if (!picked.empty()) {
        std::sort(picked.begin(), picked.end());
        low = picked[size_t(0.001 * (picked.size() - 1))];
        high = picked[size_t(0.999 * (picked.size() - 1))];
        // Scatter on a linear axis starts at zero, as cytometers draw it.
        if (scale == Scale::Linear && low >= 0)
            low = 0;
        if (high <= low)
            high = low + 1;
        const double pad = (high - low) * 0.04;
        high += pad;
        if (!(scale == Scale::Linear && low == 0))
            low -= pad;
    }
    m_ranges.insert(key, {low, high});
    return {low, high};
}

void Analysis::gatesChanged() {
    for (auto &[sample, entry] : m_cache)
        entry.members.clear();
}

void Analysis::compensationChanged() {
    m_cache.clear();
    m_ranges.clear();
}

QByteArray Analysis::statsCsv() {
    const QStringList channelList = channels();
    QByteArray csv = "Sample,Population,Parent,Events,% of parent,% of total";
    for (const QString &channel : channelList)
        csv += ',' + csvField("Median " + channel);
    csv += '\n';
    const QVector<int> order = populationOrder();
    for (int i = 0; i < samples.size(); ++i) {
        for (int population : order) {
            const PopulationStats s = stats(i, population);
            const Gate *g = gate(population);
            csv += csvField(samples[i]->name) + ',' + csvField(path(population)) + ','
                 + csvField(g ? path(g->parent) : QString()) + ',';
            if (s.available) {
                csv += QByteArray::number(s.count) + ',' + number(s.percentOfParent(), 2) + ',' + number(s.percentOfTotal(), 2);
                for (const QString &channel : channelList) {
                    const double m = median(i, population, channel);
                    csv += ',' + (std::isnan(m) ? QByteArray() : number(m, 2));
                }
            } else {
                csv += ",,";
                for (qsizetype c = 0; c < channelList.size(); ++c)
                    csv += ',';
            }
            csv += '\n';
        }
    }
    return csv;
}

QJsonArray Analysis::statsJson() {
    QJsonArray rows;
    const QStringList channelList = channels();
    for (int i = 0; i < samples.size(); ++i) {
        for (int population : populationOrder()) {
            const PopulationStats s = stats(i, population);
            const Gate *g = gate(population);
            QJsonObject row{{"sample", samples[i]->name}, {"file", samples[i]->path}, {"population", path(population)},
                            {"parent", g ? QJsonValue(path(g->parent)) : QJsonValue()}, {"available", s.available}};
            if (s.available) {
                row.insert("events", s.count);
                row.insert("percentOfParent", std::round(s.percentOfParent() * 100) / 100);
                row.insert("percentOfTotal", std::round(s.percentOfTotal() * 100) / 100);
                QJsonObject medians;
                for (const QString &channel : channelList) {
                    const double m = median(i, population, channel);
                    medians.insert(channel, std::isnan(m) ? QJsonValue() : QJsonValue(std::round(m * 100) / 100));
                }
                row.insert("medians", medians);
            }
            rows.append(row);
        }
    }
    return rows;
}

QJsonObject Analysis::toJson(const QDir &base) const {
    QJsonArray sampleList, gateList;
    for (const auto &sample : samples)
        sampleList.append(base.relativeFilePath(sample->path));
    for (const Gate &gate : gates)
        gateList.append(gate.toJson());
    QJsonArray rows;
    const int n = compensation.channels.size();
    for (int r = 0; r < n; ++r) {
        QJsonArray row;
        for (int c = 0; c < n; ++c)
            row.append(compensation.at(r, c));
        rows.append(row);
    }
    QJsonObject scaleMap;
    for (auto it = scales.begin(); it != scales.end(); ++it)
        scaleMap.insert(it.key(), Scales::id(it.value()));
    return {{"format", "cyto"},
            {"version", 1},
            {"samples", sampleList},
            {"compensation", QJsonObject{{"enabled", compensate},
                                         {"channels", QJsonArray::fromStringList(compensation.channels)},
                                         {"matrix", rows}}},
            {"scales", scaleMap},
            {"gates", gateList}};
}

std::optional<Analysis> Analysis::fromJson(const QByteArray &bytes, const QDir &base, QStringList *problems, bool loadSamples) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    auto refuse = [&](const QString &message) -> std::optional<Analysis> {
        if (problems)
            *problems << message;
        return std::nullopt;
    };
    if (!document.isObject())
        return refuse(tr("The workspace is not valid JSON: %1").arg(parseError.errorString()));
    const QJsonObject root = document.object();
    if (root.value("format").toString() != "cyto")
        return refuse(tr("This is not a cyto workspace (its \"format\" isn't \"cyto\")."));
    if (root.value("version").toInt() > 1)
        return refuse(tr("The workspace was made by a newer cyto (version %1).").arg(root.value("version").toInt()));

    Analysis analysis;
    for (const QJsonValue &value : root.value("gates").toArray()) {
        QString error;
        const auto gate = Gate::fromJson(value.toObject(), &error);
        if (!gate)
            return refuse(error);
        if (analysis.gate(gate->id))
            return refuse(tr("Two gates share the id %1.").arg(gate->id));
        analysis.gates << *gate;
    }
    for (const Gate &gate : analysis.gates) {
        if (gate.parent != 0 && !analysis.gate(gate.parent))
            return refuse(tr("Gate %1's parent %2 doesn't exist.").arg(gate.name).arg(gate.parent));
        if (analysis.depth(gate.id) >= 64)
            return refuse(tr("Gate %1 is its own ancestor.").arg(gate.name));
    }

    const QJsonObject compensation = root.value("compensation").toObject();
    for (const QJsonValue &channel : compensation.value("channels").toArray())
        analysis.compensation.channels << channel.toString();
    const QJsonArray rows = compensation.value("matrix").toArray();
    const int n = analysis.compensation.channels.size();
    if (rows.size() != n)
        return refuse(tr("The compensation matrix needs one row per channel."));
    for (const QJsonValue &row : rows) {
        if (row.toArray().size() != n)
            return refuse(tr("The compensation matrix needs one value per channel in every row."));
        for (const QJsonValue &value : row.toArray())
            analysis.compensation.matrix << value.toDouble();
    }
    analysis.compensate = compensation.value("enabled").toBool() && n > 0;

    const QJsonObject scaleMap = root.value("scales").toObject();
    for (auto it = scaleMap.begin(); it != scaleMap.end(); ++it) {
        const auto scale = Scales::fromId(it.value().toString());
        if (!scale)
            return refuse(tr("Channel %1's scale must be linear, log, or arcsinh.").arg(it.key()));
        analysis.scales.insert(it.key(), *scale);
    }

    if (loadSamples) {
        for (const QJsonValue &value : root.value("samples").toArray()) {
            const QString path = base.absoluteFilePath(value.toString());
            QString error;
            auto sample = Fcs::read(path, &error);
            if (!sample) {
                if (problems)
                    *problems << tr("%1: %2").arg(QDir::toNativeSeparators(path), error);
                continue;
            }
            // The workspace's own matrix wins over the file's.
            analysis.samples.append(std::make_shared<const FcsSample>(std::move(*sample)));
        }
    }
    return analysis;
}
