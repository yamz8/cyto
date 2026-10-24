// The command line follows Hype's (MIT, David Heinemeier Hansson).
#include "cli.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
#include <cstdio>

#include "analysis.h"

namespace {
void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonValue &value) {
    const QJsonDocument document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    fprintf(stdout, "%s", document.toJson().constData());
}
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
    }
    void json() { parser.addOption({"json", "Print the result as JSON"}); }
    QString argument(int index) const { return parser.positionalArguments().value(index); }
};

// A workspace, with its own samples or the FCS files given instead, so one
// gating strategy can be run over a new batch.
std::optional<Analysis> loadWorkspace(const QString &path, const QStringList &samples, QStringList *problems) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *problems << path + ": " + file.errorString();
        return std::nullopt;
    }
    auto analysis = Analysis::fromJson(file.readAll(), QFileInfo(path).absoluteDir(), problems, samples.isEmpty());
    if (!analysis)
        return std::nullopt;
    for (const QString &sample : samples) {
        QString error;
        auto fcs = Fcs::read(sample, &error);
        if (!fcs) {
            *problems << sample + ": " + error;
            continue;
        }
        analysis->samples.append(std::make_shared<const FcsSample>(std::move(*fcs)));
    }
    return analysis;
}

// What the gates and the matrix ask of the samples that they don't have.
QStringList mismatches(Analysis &analysis) {
    QStringList list;
    for (int i = 0; i < analysis.samples.size(); ++i) {
        const FcsSample &sample = *analysis.samples[i];
        for (const Gate &gate : analysis.gates)
            for (const QString &channel : {gate.x, gate.y})
                if (!channel.isEmpty() && sample.channelIndex(channel) < 0)
                    list << QString("%1: gate %2 uses %3, which this sample doesn't have").arg(sample.name, gate.name, channel);
        const QString problem = analysis.compensationProblem(i);
        if (!problem.isEmpty())
            list << problem;
    }
    if (analysis.compensate && !Compensation::invert(analysis.compensation.matrix, int(analysis.compensation.channels.size())))
        list << "The compensation matrix can't be inverted.";
    list.removeDuplicates();
    return list;
}

int info(const QStringList &arguments) {
    Command command("info", "Describe an FCS file: its events, channels, value ranges, and spillover matrix.");
    command.parser.addPositionalArgument("file", "FCS file", "<file.fcs>");
    command.json();
    command.parser.process(arguments);
    const QString path = command.argument(1);
    if (path.isEmpty())
        return fail("Name the FCS file to describe.");
    QString error;
    const auto sample = Fcs::read(path, &error);
    if (!sample)
        return fail(path + ": " + error);

    QJsonArray channels;
    for (int c = 0; c < sample->channels.size(); ++c) {
        std::vector<float> values;
        const qsizetype step = std::max<qsizetype>(1, sample->events / 100000);
        for (qsizetype e = 0; e < sample->events; e += step)
            values.push_back(sample->value(e, c));
        std::sort(values.begin(), values.end());
        auto percentile = [&](double p) { return values.empty() ? 0.0 : double(values[size_t(p * (values.size() - 1))]); };
        channels.append(QJsonObject{{"name", sample->channels[c]},
                                    {"label", sample->labels[c]},
                                    {"range", sample->ranges[c]},
                                    {"scale", sample->isScatterOrTime(c) ? "linear" : "arcsinh"},
                                    {"p1", std::round(percentile(0.01) * 100) / 100},
                                    {"median", std::round(percentile(0.5) * 100) / 100},
                                    {"p99", std::round(percentile(0.99) * 100) / 100}});
    }
    if (command.parser.isSet("json")) {
        print(QJsonObject{{"file", sample->path},
                          {"name", sample->name},
                          {"version", sample->version},
                          {"events", sample->events},
                          {"channels", channels},
                          {"spillover", sample->spillover.isEmpty() ? QJsonValue() : QJsonArray::fromStringList(sample->spillover.channels)}});
        return 0;
    }
    print(stdout, QString("%1: %2, %3 events, %4 channels").arg(sample->name, sample->version).arg(sample->events).arg(sample->channels.size()));
    for (const QJsonValue &value : channels) {
        const QJsonObject c = value.toObject();
        print(stdout, QString("  %1%2  median %3, 1–99%: %4 to %5")
                          .arg(c["name"].toString(), c["label"].toString().isEmpty() ? QString() : " (" + c["label"].toString() + ")")
                          .arg(c["median"].toDouble()).arg(c["p1"].toDouble()).arg(c["p99"].toDouble()));
    }
    if (!sample->spillover.isEmpty())
        print(stdout, "  Spillover matrix for " + sample->spillover.channels.join(", "));
    return 0;
}

int check(const QStringList &arguments) {
    Command command("check", "Validate a workspace: its JSON, gate tree, samples, and compensation, reporting every problem.");
    command.parser.addPositionalArgument("workspace", "Workspace (.cyto)", "<workspace.cyto>");
    command.json();
    command.parser.process(arguments);
    const QString path = command.argument(1);
    if (path.isEmpty())
        return fail("Name the .cyto workspace to check.");
    QStringList problems;
    auto analysis = loadWorkspace(path, {}, &problems);
    if (analysis)
        problems << mismatches(*analysis);
    const bool valid = analysis && problems.isEmpty();
    if (command.parser.isSet("json")) {
        QJsonArray populations;
        if (analysis)
            for (int id : analysis->populationOrder())
                populations.append(QJsonObject{{"id", id}, {"path", analysis->path(id)}});
        print(QJsonObject{{"file", QFileInfo(path).absoluteFilePath()},
                          {"valid", valid},
                          {"problems", QJsonArray::fromStringList(problems)},
                          {"samples", analysis ? int(analysis->samples.size()) : 0},
                          {"populations", populations}});
    } else {
        for (const QString &problem : problems)
            print(stderr, path + ": " + problem);
        if (valid)
            print(stdout, QString("%1: valid, %2 samples, %3 gates").arg(path).arg(analysis->samples.size()).arg(analysis->gates.size()));
    }
    return valid ? 0 : 1;
}

int stats(const QStringList &arguments) {
    Command command("stats", "Apply a workspace's gates to every sample and print the statistics as CSV.");
    command.parser.addPositionalArgument("workspace", "Workspace (.cyto)", "<workspace.cyto>");
    command.parser.addPositionalArgument("samples", "FCS files to gate instead of the workspace's own", "[file.fcs…]");
    command.parser.addOption({{"o", "out"}, "Write the CSV to <out> instead of standard output", "out"});
    command.json();
    command.parser.process(arguments);
    const QString path = command.argument(1);
    if (path.isEmpty())
        return fail("Name the .cyto workspace whose gates to apply.");
    QStringList problems;
    auto analysis = loadWorkspace(path, command.parser.positionalArguments().mid(2), &problems);
    if (!analysis || !problems.isEmpty()) {
        for (const QString &problem : problems)
            print(stderr, problem);
        return 1;
    }
    if (analysis->samples.isEmpty())
        return fail(path + " has no samples; name FCS files after it to gate them.");
    // A gate that can't be applied is reported, but the rest still count.
    for (const QString &warning : mismatches(*analysis))
        print(stderr, "warning: " + warning);

    if (command.parser.isSet("out")) {
        const QString out = command.parser.value("out");
        QSaveFile file(out);
        if (!file.open(QIODevice::WriteOnly) || file.write(analysis->statsCsv()) < 0 || !file.commit())
            return fail("Could not write " + out + ": " + file.errorString());
        if (command.parser.isSet("json"))
            print(QJsonObject{{"file", QFileInfo(path).absoluteFilePath()}, {"exported", QFileInfo(out).absoluteFilePath()},
                              {"samples", int(analysis->samples.size())}, {"populations", int(analysis->populationOrder().size())}});
        else
            print(stdout, "Exported " + out);
        return 0;
    }
    if (command.parser.isSet("json")) {
        print(analysis->statsJson());
        return 0;
    }
    const QByteArray csv = analysis->statsCsv();
    fwrite(csv.constData(), 1, csv.size(), stdout);
    return 0;
}

// As Hype does: one shared copy, linked into Claude Code, found directly by Codex.
int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.");
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/cyto and link into ~/.claude/skills", "[install]");
    command.json();
    command.parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return fail("The bundled skill is missing.");
    const QByteArray text = bundled.readAll();
    if (command.argument(1).isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (command.argument(1) != "install")
        return fail("Use cyto skill to print the skill, or cyto skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/cyto");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    const bool json = command.parser.isSet("json");
    QJsonObject result{{"skill", file.fileName()}, {"link", QJsonValue()}};
    if (!json)
        print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude")) {
        if (json)
            print(result);
        return 0;
    }
    const QString link = home.filePath(".claude/skills/cyto");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/cyto", link))
        return fail("Could not link " + link);
    if (json) {
        result.insert("link", link);
        print(result);
    } else {
        print(stdout, "Linked " + link);
    }
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: cyto <command> [options]\n\n" + cliSummary());
    return 0;
}
} // namespace

QString cliSummary() {
    return "Window:\n"
           "  open [files…]                      Gate FCS files, or open a .cyto workspace\n\n"
           "Commands that need no display (cyto help <command> for options):\n"
           "  info <file.fcs> [--json]           Events, channels, value ranges, spillover\n"
           "  check <workspace.cyto> [--json]    Validate a workspace and list every problem\n"
           "  stats <workspace.cyto> [file.fcs…] [-o out.csv] [--json]\n"
           "                                     Apply the gates to every sample; CSV or JSON\n"
           "  skill [install]                    Print the skill for coding agents, or install it";
}

bool isCliCommand(const QString &word) {
    return QStringList{"info", "check", "stats", "skill", "help"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "info"    ? info(arguments)
           : command == "check" ? check(arguments)
           : command == "stats" ? stats(arguments)
           : command == "skill" ? skill(arguments)
                                : help(arguments);
}
