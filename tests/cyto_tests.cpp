#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include "analysis.h"
#include "backend.h"
#include "fcs.h"
#include "filepicker.h"
#include "gating.h"
#include "testdata.h"
#include "theme.h"
#include "translator.h"

namespace {
// Answers like the portal once a file is chosen; with no path, like a cancelled chooser.
class FakePicker : public FilePicker {
public:
    QString path;
    void openSamples() override { if (!path.isEmpty()) emit samplesSelected({QUrl::fromLocalFile(path)}); }
    void openWorkspace() override { if (!path.isEmpty()) emit workspaceSelected(QUrl::fromLocalFile(path)); }
    void saveWorkspace(const QUrl &) override { if (!path.isEmpty()) emit saveSelected(QUrl::fromLocalFile(path)); }
    void exportStats(const QUrl &) override { if (!path.isEmpty()) emit exportSelected(QUrl::fromLocalFile(path)); }
};

Gate rectangle(int id, int parent, const QString &x, const QString &y, QPointF a, QPointF b,
               Scale xs = Scale::Linear, Scale ys = Scale::Linear) {
    Gate gate;
    gate.id = id;
    gate.parent = parent;
    gate.name = QString("G%1").arg(id);
    gate.type = Gate::Rectangle;
    gate.x = x;
    gate.y = y;
    gate.xScale = xs;
    gate.yScale = ys;
    gate.points = {a, b};
    return gate;
}

// Small cluster, then CD4 (FL1-high, FL2 under 1000) inside it, on arcsinh axes.
Analysis gatedAnalysis(int small = 600, int large = 400, double cd4 = 0.25) {
    Analysis analysis;
    analysis.addSample(std::make_shared<const FcsSample>(testSample("a", small, large, cd4)));
    analysis.gates << rectangle(1, 0, "FSC-A", "SSC-A", {30000, 5000}, {80000, 50000});
    Gate cd4Gate = rectangle(2, 1, "FL1-A", "FL2-A", {Scales::forward(Scale::Arcsinh, 2000), -3},
                             {9, Scales::forward(Scale::Arcsinh, 1000)}, Scale::Arcsinh, Scale::Arcsinh);
    cd4Gate.name = "CD4";
    analysis.gates << cd4Gate;
    return analysis;
}

void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}
}

class CytoTests : public QObject {
    Q_OBJECT

private slots:
    void fcsRoundTrips() {
        const FcsSample sample = testSample("round", 30, 20, 0.5);
        QString error;
        const auto parsed = Fcs::parse(Fcs::write(sample), &error);
        QVERIFY2(parsed, qPrintable(error));
        QCOMPARE(parsed->channels, sample.channels);
        QCOMPARE(parsed->labels, sample.labels);
        QCOMPARE(parsed->events, sample.events);
        QCOMPARE(parsed->data, sample.data);
        QCOMPARE(parsed->spillover.channels, sample.spillover.channels);
        QCOMPARE(parsed->spillover.matrix, sample.spillover.matrix);
        QCOMPARE(parsed->version, QString("FCS3.1"));
    }

    // FCS 2.0-style: 16-bit big-endian integers, one parameter log-amplified,
    // and a doubled delimiter inside a value.
    void fcsReadsBigEndianIntegers() {
        const QByteArray text = "/$BYTEORD/2,1/$DATATYPE/I/$MODE/L/$PAR/2/$TOT/2/$P1B/16/$P1R/1024/$P1N/FSC/"
                                "$P2B/16/$P2R/1024/$P2E/4,1/$P2N/FL1//2/$P2S/CD3/";
        const qint64 textStart = 58, dataStart = textStart + text.size();
        QByteArray data;
        for (quint16 value : {quint16(100), quint16(0), quint16(1023 + 0x8000), quint16(512)}) {
            uchar bytes[2];
            qToBigEndian(value, bytes);
            data.append(reinterpret_cast<char *>(bytes), 2);
        }
        auto offset = [](qint64 v) { return QByteArray::number(v).rightJustified(8, ' '); };
        const QByteArray file = "FCS2.0    " + offset(textStart) + offset(dataStart - 1) + offset(dataStart)
                              + offset(dataStart + data.size() - 1) + offset(0) + offset(0) + text + data;
        QString error;
        const auto sample = Fcs::parse(file, &error);
        QVERIFY2(sample, qPrintable(error));
        QCOMPARE(sample->channels, QStringList({"FSC", "FL1/2"}));
        QCOMPARE(sample->labels[1], QString("CD3"));
        QCOMPARE(sample->value(0, 0), 100.0f);
        QCOMPARE(sample->value(0, 1), 1.0f); // 10^(4 × 0 / 1024)
        QCOMPARE(sample->value(1, 0), 1023.0f); // masked to the range's 10 bits
        QCOMPARE(sample->value(1, 1), 100.0f); // 10^(4 × 512 / 1024)
    }

    void fcsRefusesWhatItCantRead() {
        QString error;
        QVERIFY(!Fcs::parse("not an fcs file at all, though long enough to have a header....", &error));
        QVERIFY(!error.isEmpty());
        FcsSample sample = testSample("t", 10, 0, 0.5);
        QByteArray bytes = Fcs::write(sample);
        bytes.chop(20);
        QVERIFY(!Fcs::parse(bytes, &error));
        QVERIFY(error.contains("truncated"));
    }

    void spilloverParses() {
        const Spillover spillover = Spillover::parse("2,FL1-A,FL2-A,1,0.2,0.05,1");
        QCOMPARE(spillover.channels, QStringList({"FL1-A", "FL2-A"}));
        QCOMPARE(spillover.at(0, 1), 0.2);
        QCOMPARE(spillover.at(1, 0), 0.05);
        QVERIFY(Spillover::parse("3,a,b").isEmpty());
        QCOMPARE(Spillover::parse(spillover.toKeyword()).matrix, spillover.matrix);
    }

    void compensationRemovesSpillover() {
        FcsSample sample;
        sample.channels = {"FSC-A", "FL1-A", "FL2-A"};
        sample.events = 1;
        // True FL1 1000, FL2 0; 20 % of FL1 spills into FL2.
        sample.data = {5000, 1000, 200};
        QStringList missing;
        QString error;
        const auto values = Compensation::apply(sample, testSpillover(), &missing, &error);
        QVERIFY(missing.isEmpty() && error.isEmpty());
        QCOMPARE(values[0], 5000.0f);
        QVERIFY(std::abs(values[1] - 1000) < 1e-3);
        QVERIFY(std::abs(values[2]) < 1e-3);

        Spillover singular = testSpillover();
        singular.matrix = {1, 1, 1, 1};
        Compensation::apply(sample, singular, &missing, &error);
        QVERIFY(!error.isEmpty());
    }

    void gatesContainWhatTheyShouldOnTheirScales() {
        Gate box = rectangle(1, 0, "x", "y", {0, 0}, {10, 10});
        QVERIFY(box.contains(5, 5));
        QVERIFY(box.contains(10, 10));
        QVERIFY(!box.contains(11, 5));

        Gate ellipse = box;
        ellipse.type = Gate::Ellipse;
        QVERIFY(ellipse.contains(5, 5));
        QVERIFY(!ellipse.contains(0.5, 0.5));

        Gate triangle = box;
        triangle.type = Gate::Polygon;
        triangle.points = {{0, 0}, {10, 0}, {0, 10}};
        QVERIFY(triangle.contains(2, 2));
        QVERIFY(!triangle.contains(8, 8));

        // Drawn on arcsinh axes: the bounds are scaled values.
        Gate range;
        range.type = Gate::Range;
        range.x = "FL1-A";
        range.xScale = Scale::Arcsinh;
        range.points = {{Scales::forward(Scale::Arcsinh, 1000), 0}, {Scales::forward(Scale::Arcsinh, 10000), 0}};
        QVERIFY(range.isValid());
        QVERIFY(range.contains(5000, 0));
        QVERIFY(!range.contains(500, 0));
        QVERIFY(!range.contains(-5000, 0));
        QVERIFY(std::abs(Scales::inverse(Scale::Arcsinh, Scales::forward(Scale::Arcsinh, -321)) + 321) < 1e-9);
        QVERIFY(std::abs(Scales::inverse(Scale::Log, Scales::forward(Scale::Log, 4321)) - 4321) < 1e-6);
    }

    void populationsNestInsideTheirParents() {
        Analysis analysis = gatedAnalysis(600, 400, 0.25);
        analysis.compensate = true;
        const PopulationStats all = analysis.stats(0, 0), small = analysis.stats(0, 1), cd4 = analysis.stats(0, 2);
        QCOMPARE(all.count, 1000);
        QCOMPARE(small.count, 600);
        QCOMPARE(small.percentOfParent(), 60.0);
        QCOMPARE(cd4.parentCount, 600);
        QCOMPARE(cd4.count, 150);
        QCOMPARE(cd4.percentOfTotal(), 15.0);
        QCOMPARE(analysis.path(2), QString("G1/CD4"));
        QCOMPARE(analysis.populationOrder(), QVector<int>({0, 1, 2}));
        QCOMPARE(analysis.descendants(1), QVector<int>({2}));
    }

    // Without compensation, FL1's spillover pushes CD4 cells over the FL2
    // bound, so turning it on changes the answer.
    void compensationChangesTheCounts() {
        Analysis analysis = gatedAnalysis(600, 400, 0.25);
        analysis.compensate = false;
        analysis.compensationChanged();
        QCOMPARE(analysis.stats(0, 2).count, 0);
        analysis.compensate = true;
        analysis.compensationChanged();
        QCOMPARE(analysis.stats(0, 2).count, 150);
    }

    void aSampleWithoutTheChannelIsUnavailable() {
        Analysis analysis = gatedAnalysis();
        FcsSample other = testSample("b", 10, 10, 0.5);
        other.channels[2] = "FITC-A";
        other.spillover = {};
        analysis.addSample(std::make_shared<const FcsSample>(other));
        QVERIFY(analysis.stats(0, 2).available);
        QVERIFY(!analysis.stats(1, 2).available);
        QVERIFY(analysis.stats(1, 1).available);
        QVERIFY(!analysis.compensationProblem(1).isEmpty() || !analysis.compensate);
        const QByteArray csv = analysis.statsCsv();
        QVERIFY(csv.startsWith("Sample,Population,Parent,Events,% of parent,% of total,Median FSC-A"));
        QVERIFY(csv.contains("\nb,G1/CD4,G1,,,"));
    }

    void workspacesRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("data"));
        const QString path = writeTestSample(dir.filePath("data/a.fcs"), testSample("a", 60, 40, 0.5));

        Analysis analysis;
        QString error;
        analysis.addSample(std::make_shared<const FcsSample>(*Fcs::read(path, &error)));
        analysis.gates = gatedAnalysis().gates;
        analysis.scales.insert("FL1-A", Scale::Log);
        const QByteArray bytes = QJsonDocument(analysis.toJson(QDir(dir.path()))).toJson();
        QVERIFY(bytes.contains("\"data/a.fcs\""));

        QStringList problems;
        const auto loaded = Analysis::fromJson(bytes, QDir(dir.path()), &problems);
        QVERIFY2(loaded && problems.isEmpty(), qPrintable(problems.join('\n')));
        QCOMPARE(loaded->samples.size(), 1);
        QCOMPARE(loaded->gates.size(), 2);
        QCOMPARE(loaded->gates[1].name, QString("CD4"));
        QCOMPARE(loaded->gates[1].points, analysis.gates[1].points);
        QCOMPARE(loaded->compensation.matrix, analysis.compensation.matrix);
        QVERIFY(loaded->compensate);
        QCOMPARE(loaded->scales.value("FL1-A"), Scale::Log);

        // A sample that has gone missing is reported, the rest still loads.
        QFile::remove(path);
        const auto partial = Analysis::fromJson(bytes, QDir(dir.path()), &problems);
        QVERIFY(partial);
        QCOMPARE(partial->samples.size(), 0);
        QCOMPARE(problems.size(), 1);
    }

    void malformedWorkspacesAreRefused() {
        auto refused = [](const QByteArray &json) {
            QStringList problems;
            return !Analysis::fromJson(json, QDir::temp(), &problems, false) && !problems.isEmpty();
        };
        QVERIFY(refused("{"));
        QVERIFY(refused(R"({"format":"plate"})"));
        QVERIFY(refused(R"({"format":"cyto","gates":[{"id":1,"parent":7,"name":"a","type":"rectangle","x":"A","y":"B","points":[[0,0],[1,1]]}]})"));
        QVERIFY(refused(R"({"format":"cyto","gates":[{"id":1,"parent":2,"name":"a","type":"range","x":"A","points":[0,1]},{"id":2,"parent":1,"name":"b","type":"range","x":"A","points":[0,1]}]})"));
        QVERIFY(refused(R"({"format":"cyto","gates":[{"id":1,"name":"a","type":"polygon","x":"A","y":"B","points":[[0,0],[1,1]]}]})"));
        QVERIFY(refused(R"({"format":"cyto","gates":[{"id":1,"name":"a","type":"star","x":"A","y":"B","points":[[0,0],[1,1]]}]})"));
        QVERIFY(refused(R"({"format":"cyto","compensation":{"channels":["A","B"],"matrix":[[1,0]]}})"));
        QVERIFY(refused(R"({"format":"cyto","scales":{"A":"cubic"}})"));
        QVERIFY(!refused(R"({"format":"cyto","version":1})"));
    }

    void backendDrawsUndoesAndRedoesGates() {
        QTemporaryDir dir;
        FakePicker picker;
        Backend backend(&picker);
        backend.openPaths({writeTestSample(dir.filePath("a.fcs"), testSample("a", 600, 400, 0.25))});
        QCOMPARE(backend.samples().size(), 1);
        QCOMPARE(backend.xChannel(), QString("FSC-A"));
        QCOMPARE(backend.yChannel(), QString("SSC-A"));
        QVERIFY(backend.compensate()); // adopted from the file

        const int small = backend.addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        QVERIFY(small > 0);
        QCOMPARE(backend.selectedGate(), small);
        QCOMPARE(backend.currentStats(small).count, 600);
        backend.openGate(small);
        QCOMPARE(backend.currentPopulation(), small);
        backend.setXChannel("FL1-A");
        backend.setHistogram(true);
        const int cd4 = backend.addGate(Gate::Rectangle, {{Scales::forward(Scale::Arcsinh, 2000), 0}, {9, 0}});
        QCOMPARE(backend.analysis().gate(cd4)->type, Gate::Range);
        QCOMPARE(backend.analysis().gate(cd4)->parent, small);
        QCOMPARE(backend.currentStats(cd4).count, 150);
        QVERIFY(backend.renameGate(cd4, "CD4/hi"));
        QCOMPARE(backend.populationName(cd4), QString("CD4-hi"));

        // Back out to the parent: the axes it was drawn on come back.
        backend.openParent();
        QCOMPARE(backend.currentPopulation(), 0);
        QCOMPARE(backend.xChannel(), QString("FSC-A"));
        QVERIFY(!backend.histogram());

        backend.deleteGate(small);
        QVERIFY(backend.analysis().gates.isEmpty());
        backend.undo();
        QCOMPARE(backend.analysis().gates.size(), 2);
        QCOMPARE(backend.populationName(cd4), QString("CD4-hi"));
        backend.undo();
        QCOMPARE(backend.populationName(cd4), QString("P2"));
        backend.redo();
        backend.redo();
        QVERIFY(backend.analysis().gates.isEmpty());
        QVERIFY(!backend.canRedo());
    }

    void backendSavesAtomicallyAndReopens() {
        QTemporaryDir dir;
        FakePicker picker;
        Backend backend(&picker);
        backend.openPaths({writeTestSample(dir.filePath("a.fcs"), testSample("a", 600, 400, 0.25)),
                           writeTestSample(dir.filePath("b.fcs"), testSample("b", 300, 700, 0.5, 2))});
        const int gate = backend.addGate(Gate::Ellipse, {{30000, 5000}, {80000, 50000}});
        backend.setSelectedGate(0);
        QVERIFY(backend.dirty());
        QSignalSpy saved(&backend, &Backend::saved);
        picker.path = dir.filePath("analysis");
        backend.saveWorkspace();
        QCOMPARE(saved.count(), 1);
        QVERIFY(!backend.dirty());
        QCOMPARE(backend.fileName(), QString("analysis.cyto"));
        QCOMPARE(QDir(dir.path()).entryList({"*.cyto*"}), QStringList({"analysis.cyto"}));

        Backend reopened(&picker);
        QVERIFY(reopened.loadWorkspace(dir.filePath("analysis.cyto")));
        QCOMPARE(reopened.samples().size(), 2);
        QCOMPARE(reopened.analysis().gate(gate)->type, Gate::Ellipse);
        QVERIFY(!reopened.dirty());
        QVERIFY(reopened.problem().isEmpty());

        picker.path = dir.filePath("stats.csv");
        reopened.exportStats();
        QFile csv(dir.filePath("stats.csv"));
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const QList<QByteArray> lines = csv.readAll().split('\n');
        QCOMPARE(lines.size(), 1 + 2 * 2 + 1);
        QVERIFY(lines[2].startsWith("a,P1,All events,"));
    }

    void backendReportsFilesItCantOpen() {
        QTemporaryDir dir;
        write(dir.filePath("bad.fcs"), "definitely not FCS");
        FakePicker picker;
        Backend backend(&picker);
        backend.openPaths({dir.filePath("bad.fcs")});
        QVERIFY(backend.samples().isEmpty());
        QVERIFY(backend.problem().contains("bad.fcs"));
        QVERIFY(!backend.dirty());
    }

    void backendStartsAMatrixWhenFilesHaveNone() {
        QTemporaryDir dir;
        FcsSample sample = testSample("a", 10, 10, 0.5);
        sample.spillover = {};
        FakePicker picker;
        Backend backend(&picker);
        backend.openPaths({writeTestSample(dir.filePath("a.fcs"), sample)});
        QVERIFY(!backend.compensate());
        backend.createCompensation();
        QCOMPARE(backend.compensationChannels(), QStringList({"FL1-A", "FL2-A"}));
        backend.setCompensationValue(0, 1, 0.2);
        QCOMPARE(backend.analysis().compensation.at(0, 1), 0.2);
        backend.undo();
        QCOMPARE(backend.analysis().compensation.at(0, 1), 0.0);
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding() {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    // Every catalog parses, and keeps the placeholders of the English it translates.
    void catalogsKeepTheirPlaceholders() {
        const QStringList catalogs = QDir(":/i18n").entryList({"*.json"});
        QVERIFY(catalogs.contains("da.json"));
        const QRegularExpression placeholder("%(L?n|\\d)");
        for (const QString &name : catalogs) {
            QFile file(":/i18n/" + name);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            const QJsonObject catalog = QJsonDocument::fromJson(file.readAll(), &error).object();
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(name + ": " + error.errorString()));
            for (auto it = catalog.begin(); it != catalog.end(); ++it) {
                QStringList wanted;
                for (auto m = placeholder.globalMatch(it.key()); m.hasNext();)
                    wanted << m.next().captured();
                wanted.sort();
                const QJsonValue value = it.value();
                const QStringList forms = value.isObject() ? QStringList(value.toObject().toVariantMap().keys()) : QStringList{"_"};
                for (const QString &form : forms) {
                    const QString text = value.isObject() ? value.toObject().value(form).toString() : value.toString();
                    QStringList found;
                    for (auto m = placeholder.globalMatch(text); m.hasNext();)
                        found << m.next().captured();
                    found.sort();
                    found.removeDuplicates();
                    wanted.removeDuplicates();
                    QVERIFY2(found == wanted, qPrintable(name + ": " + it.key() + " → " + text));
                }
            }
        }
    }
};

QTEST_MAIN(CytoTests)
#include "cyto_tests.moc"
