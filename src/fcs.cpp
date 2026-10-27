#include "fcs.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Fcs", text); }

QString fail(QString *error, const QString &message) {
    if (error)
        *error = message;
    return message;
}

// TEXT is delimiter-separated key/value pairs; a doubled delimiter is the
// delimiter itself inside a key or value.
QHash<QString, QString> parseText(const QByteArray &text) {
    QHash<QString, QString> keywords;
    if (text.size() < 2)
        return keywords;
    const char delimiter = text[0];
    QList<QByteArray> tokens;
    QByteArray current;
    for (qsizetype i = 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c != delimiter) {
            current += c;
        } else if (i + 1 < text.size() && text[i + 1] == delimiter) {
            current += delimiter;
            ++i;
        } else {
            tokens << current;
            current.clear();
        }
    }
    if (!current.trimmed().isEmpty())
        tokens << current;
    for (qsizetype i = 0; i + 1 < tokens.size(); i += 2)
        keywords.insert(QString::fromUtf8(tokens[i]).trimmed().toUpper(), QString::fromUtf8(tokens[i + 1]).trimmed());
    return keywords;
}

qint64 headerOffset(const QByteArray &bytes, int at) { return bytes.mid(at, 8).trimmed().toLongLong(); }

QByteArray escape(QString value, char delimiter) {
    return value.replace(QChar(delimiter), QString(2, QChar(delimiter))).toUtf8();
}
}

Spillover Spillover::parse(const QString &text) {
    const QStringList parts = text.split(',');
    bool ok = false;
    const int n = parts.value(0).trimmed().toInt(&ok);
    if (!ok || n <= 0 || parts.size() < 1 + n + n * n)
        return {};
    Spillover spillover;
    for (int i = 0; i < n; ++i)
        spillover.channels << parts[1 + i].trimmed();
    for (int i = 0; i < n * n; ++i) {
        const double value = parts[1 + n + i].trimmed().toDouble(&ok);
        if (!ok)
            return {};
        spillover.matrix << value;
    }
    return spillover;
}

QString Spillover::toKeyword() const {
    QStringList parts{QString::number(channels.size())};
    parts << channels;
    for (double value : matrix)
        parts << QString::number(value, 'g', 10);
    return parts.join(',');
}

Spillover Spillover::identity(const QStringList &channels) {
    Spillover spillover;
    spillover.channels = channels;
    const int n = channels.size();
    spillover.matrix.fill(0.0, n * n);
    for (int i = 0; i < n; ++i)
        spillover.matrix[i * n + i] = 1.0;
    return spillover;
}

QString FcsSample::displayName(int channel) const {
    const QString name = channels.value(channel), label = labels.value(channel);
    return label.isEmpty() || label == name ? name : name + QStringLiteral(" · ") + label;
}

bool FcsSample::isScatterOrTime(int channel) const {
    const QString name = channels.value(channel).toUpper();
    return name.startsWith("FSC") || name.startsWith("SSC") || name.contains("TIME") || name.startsWith("EVENT");
}

std::optional<FcsSample> Fcs::read(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, file.errorString());
        return std::nullopt;
    }
    auto sample = parse(file.readAll(), error);
    if (sample) {
        sample->path = QFileInfo(path).absoluteFilePath();
        sample->name = QFileInfo(path).completeBaseName();
    }
    return sample;
}

std::optional<FcsSample> Fcs::parse(const QByteArray &bytes, QString *error) {
    if (bytes.size() < 58 || !bytes.startsWith("FCS")) {
        fail(error, tr("This is not an FCS file."));
        return std::nullopt;
    }
    FcsSample sample;
    sample.version = QString::fromLatin1(bytes.left(6));
    const qint64 textStart = headerOffset(bytes, 10), textEnd = headerOffset(bytes, 18);
    if (textStart <= 0 || textEnd <= textStart || textEnd >= bytes.size()) {
        fail(error, tr("The file's TEXT segment is out of bounds."));
        return std::nullopt;
    }
    sample.keywords = parseText(bytes.mid(textStart, textEnd - textStart + 1));
    const auto &k = sample.keywords;

    const QString mode = k.value("$MODE", "L").toUpper();
    if (mode != "L") {
        fail(error, tr("Only list-mode FCS files can be gated (this one is mode %1).").arg(mode));
        return std::nullopt;
    }
    const QString type = k.value("$DATATYPE").toUpper();
    if (type != "F" && type != "D" && type != "I") {
        fail(error, type == "A" ? tr("ASCII FCS data is not supported.") : tr("The file doesn't say how its data is stored."));
        return std::nullopt;
    }
    bool ok = false;
    const int parameters = k.value("$PAR").toInt(&ok);
    if (!ok || parameters <= 0) {
        fail(error, tr("The file doesn't say how many parameters it has."));
        return std::nullopt;
    }

    // Large files put the data offsets in TEXT and zeros in the header.
    qint64 dataStart = headerOffset(bytes, 26), dataEnd = headerOffset(bytes, 34);
    if (dataStart == 0 || dataEnd == 0) {
        dataStart = k.value("$BEGINDATA").toLongLong();
        dataEnd = k.value("$ENDDATA").toLongLong();
    }

    QVector<int> bits;
    QVector<double> logDecades, logOffsets;
    for (int p = 1; p <= parameters; ++p) {
        const QString n = QString::number(p);
        sample.channels << k.value("$P" + n + "N", "P" + n);
        sample.labels << k.value("$P" + n + "S");
        sample.ranges << k.value("$P" + n + "R").toDouble();
        int b = k.value("$P" + n + "B").toInt();
        if (type == "F")
            b = 32;
        if (type == "D")
            b = 64;
        if (b != 8 && b != 16 && b != 32 && b != 64) {
            fail(error, tr("Parameter %1 uses %2-bit integers, which are not supported.").arg(p).arg(b));
            return std::nullopt;
        }
        bits << b;
        const QStringList amplification = k.value("$P" + n + "E", "0,0").split(',');
        logDecades << amplification.value(0).toDouble();
        logOffsets << amplification.value(1).toDouble();
    }

    int eventBytes = 0;
    for (int b : bits)
        eventBytes += b / 8;
    const qint64 available = dataEnd >= dataStart && dataStart > 0 ? qMin<qint64>(dataEnd - dataStart + 1, bytes.size() - dataStart) : 0;
    qint64 events = k.contains("$TOT") ? k.value("$TOT").toLongLong() : available / eventBytes;
    // Compared by division, so a huge $TOT can't overflow past the check.
    if (events < 0 || events > available / eventBytes) {
        fail(error, tr("The file is truncated: it lists %1 events but holds fewer.").arg(events));
        return std::nullopt;
    }
    sample.events = events;

    const bool little = !k.value("$BYTEORD", "1,2,3,4").trimmed().startsWith("4") && !k.value("$BYTEORD").trimmed().startsWith("2");
    const uchar *in = reinterpret_cast<const uchar *>(bytes.constData()) + dataStart;
    sample.data.resize(size_t(events) * parameters);
    float *out = sample.data.data();

    // Integer data is masked to its range, and log-amplified parameters
    // ($PnE f1,f2) are expanded back to linear values.
    QVector<quint64> masks;
    for (int p = 0; p < parameters; ++p) {
        quint64 mask = bits[p] == 64 ? ~quint64(0) : (quint64(1) << bits[p]) - 1;
        if (type == "I" && sample.ranges[p] > 1) {
            const int needed = int(std::ceil(std::log2(sample.ranges[p])));
            if (needed < bits[p])
                mask = (quint64(1) << needed) - 1;
        }
        masks << mask;
    }

    for (qint64 e = 0; e < events; ++e) {
        for (int p = 0; p < parameters; ++p) {
            double value = 0;
            if (type == "F") {
                const quint32 raw = little ? qFromLittleEndian<quint32>(in) : qFromBigEndian<quint32>(in);
                float f;
                std::memcpy(&f, &raw, 4);
                value = f;
            } else if (type == "D") {
                const quint64 raw = little ? qFromLittleEndian<quint64>(in) : qFromBigEndian<quint64>(in);
                double d;
                std::memcpy(&d, &raw, 8);
                value = d;
            } else {
                quint64 raw = 0;
                switch (bits[p]) {
                case 8: raw = *in; break;
                case 16: raw = little ? qFromLittleEndian<quint16>(in) : qFromBigEndian<quint16>(in); break;
                case 32: raw = little ? qFromLittleEndian<quint32>(in) : qFromBigEndian<quint32>(in); break;
                default: raw = little ? qFromLittleEndian<quint64>(in) : qFromBigEndian<quint64>(in); break;
                }
                value = double(raw & masks[p]);
                if (logDecades[p] > 0 && sample.ranges[p] > 0)
                    value = (logOffsets[p] > 0 ? logOffsets[p] : 1.0) * std::pow(10.0, logDecades[p] * value / sample.ranges[p]);
            }
            *out++ = float(value);
            in += bits[p] / 8;
        }
    }

    for (const char *key : {"$SPILLOVER", "SPILL", "$SPILL", "SPILLOVER", "$COMP"}) {
        const Spillover spillover = Spillover::parse(k.value(key));
        if (!spillover.isEmpty()) {
            sample.spillover = spillover;
            break;
        }
    }
    sample.name = k.value("$FIL", "sample");
    return sample;
}

QByteArray Fcs::write(const FcsSample &sample) {
    const int parameters = sample.channels.size();
    QByteArray data;
    data.resize(qsizetype(sample.events) * parameters * 4);
    uchar *out = reinterpret_cast<uchar *>(data.data());
    for (float value : sample.data) {
        quint32 raw;
        std::memcpy(&raw, &value, 4);
        qToLittleEndian(raw, out);
        out += 4;
    }

    QList<QPair<QString, QString>> keywords{
        {"$BEGINANALYSIS", "0"}, {"$ENDANALYSIS", "0"}, {"$BEGINSTEXT", "0"}, {"$ENDSTEXT", "0"},
        {"$BYTEORD", "1,2,3,4"}, {"$DATATYPE", "F"}, {"$MODE", "L"}, {"$NEXTDATA", "0"},
        {"$PAR", QString::number(parameters)}, {"$TOT", QString::number(sample.events)},
        {"$FIL", sample.name.isEmpty() ? "sample.fcs" : sample.name + ".fcs"}};
    for (int p = 0; p < parameters; ++p) {
        const QString n = QString::number(p + 1);
        keywords << qMakePair("$P" + n + "B", QString("32")) << qMakePair("$P" + n + "E", QString("0,0"))
                 << qMakePair("$P" + n + "N", sample.channels[p])
                 << qMakePair("$P" + n + "R", QString::number(sample.ranges.value(p, 262144), 'g', 10));
        if (!sample.labels.value(p).isEmpty())
            keywords << qMakePair("$P" + n + "S", sample.labels[p]);
    }
    if (!sample.spillover.isEmpty())
        keywords << qMakePair(QString("$SPILLOVER"), sample.spillover.toKeyword());

    // The data offsets live inside TEXT, whose length depends on them, so
    // settle them in a few rounds.
    const qint64 textStart = 58;
    qint64 dataStart = 0;
    QByteArray text;
    for (int round = 0; round < 4; ++round) {
        text = "/";
        auto add = [&](const QString &key, const QString &value) {
            text += escape(key, '/') + '/' + escape(value, '/') + '/';
        };
        add("$BEGINDATA", QString::number(dataStart));
        add("$ENDDATA", QString::number(dataStart + data.size() - 1));
        for (const auto &[key, value] : keywords)
            add(key, value);
        const qint64 settled = textStart + text.size();
        if (settled == dataStart)
            break;
        dataStart = settled;
    }

    auto offset = [](qint64 value) { return QByteArray::number(value > 99999999 ? 0 : value).rightJustified(8, ' '); };
    QByteArray file = "FCS3.1    ";
    file += offset(textStart) + offset(textStart + text.size() - 1) + offset(dataStart)
          + offset(dataStart + data.size() - 1) + offset(0) + offset(0);
    file += text;
    file += data;
    return file;
}

std::optional<QVector<double>> Compensation::invert(const QVector<double> &matrix, int n) {
    if (matrix.size() != n * n)
        return std::nullopt;
    // Gauss-Jordan elimination with partial pivoting on [matrix | identity].
    QVector<double> a = matrix, inverse(n * n, 0.0);
    for (int i = 0; i < n; ++i)
        inverse[i * n + i] = 1.0;
    for (int column = 0; column < n; ++column) {
        int pivot = column;
        for (int row = column + 1; row < n; ++row)
            if (std::abs(a[row * n + column]) > std::abs(a[pivot * n + column]))
                pivot = row;
        if (std::abs(a[pivot * n + column]) < 1e-12)
            return std::nullopt;
        if (pivot != column) {
            for (int j = 0; j < n; ++j) {
                std::swap(a[pivot * n + j], a[column * n + j]);
                std::swap(inverse[pivot * n + j], inverse[column * n + j]);
            }
        }
        const double scale = a[column * n + column];
        for (int j = 0; j < n; ++j) {
            a[column * n + j] /= scale;
            inverse[column * n + j] /= scale;
        }
        for (int row = 0; row < n; ++row) {
            if (row == column)
                continue;
            const double factor = a[row * n + column];
            if (factor == 0)
                continue;
            for (int j = 0; j < n; ++j) {
                a[row * n + j] -= factor * a[column * n + j];
                inverse[row * n + j] -= factor * inverse[column * n + j];
            }
        }
    }
    return inverse;
}

std::vector<float> Compensation::apply(const FcsSample &sample, const Spillover &spillover, QStringList *missing, QString *error) {
    std::vector<float> data = sample.data;
    const int n = spillover.channels.size();
    if (n == 0)
        return data;
    QVector<int> index;
    for (const QString &channel : spillover.channels) {
        const int i = sample.channelIndex(channel);
        if (i < 0 && missing)
            *missing << channel;
        index << i;
    }
    if (index.contains(-1))
        return data;
    const auto inverse = invert(spillover.matrix, n);
    if (!inverse) {
        fail(error, tr("The compensation matrix can't be inverted."));
        return data;
    }
    const int stride = sample.channels.size();
    QVector<double> observed(n);
    for (qsizetype e = 0; e < sample.events; ++e) {
        float *row = data.data() + e * stride;
        for (int i = 0; i < n; ++i)
            observed[i] = row[index[i]];
        for (int j = 0; j < n; ++j) {
            double value = 0;
            for (int i = 0; i < n; ++i)
                value += observed[i] * (*inverse)[i * n + j];
            row[index[j]] = float(value);
        }
    }
    return data;
}
