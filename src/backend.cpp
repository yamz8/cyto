#include "backend.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>

#include "filepicker.h"

StatsModel::StatsModel(Backend *backend) : QAbstractTableModel(backend), m_backend(backend) {}

int StatsModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_backend->analysis().samples.size());
}

int StatsModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_populations.size()) + 1;
}

QVariant StatsModel::data(const QModelIndex &index, int role) const {
    Analysis &analysis = m_backend->analysis();
    if (!index.isValid() || index.row() >= analysis.samples.size())
        return {};
    if (index.column() == 0) {
        if (role == Qt::DisplayRole)
            return analysis.samples[index.row()]->name;
        if (role == DetailRole)
            return QLocale().toString(analysis.samples[index.row()]->events);
        if (role == AvailableRole)
            return true;
        return {};
    }
    const PopulationStats stats = analysis.stats(index.row(), m_populations.value(index.column() - 1));
    switch (role) {
    case Qt::DisplayRole:
        if (!stats.available)
            return QStringLiteral("—");
        if (m_measure == 2)
            return QLocale().toString(stats.count);
        return QLocale().toString(m_measure == 0 ? stats.percentOfParent() : stats.percentOfTotal(), 'f', 1)
               + QStringLiteral(" %");
    case DetailRole:
        return stats.available && m_measure != 2 ? QLocale().toString(stats.count) : QString();
    case AvailableRole:
        return stats.available;
    default:
        return {};
    }
}

QVariant StatsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || (role != Qt::DisplayRole && role != HeaderRole))
        return {};
    if (section == 0)
        return tr("Sample");
    const int population = m_populations.value(section - 1);
    return role == HeaderRole ? m_backend->analysis().path(population) : m_backend->populationName(population);
}

QHash<int, QByteArray> StatsModel::roleNames() const {
    return {{Qt::DisplayRole, "display"}, {DetailRole, "detail"}, {AvailableRole, "available"}, {HeaderRole, "header"}};
}

void StatsModel::setMeasure(int measure) {
    if (measure == m_measure)
        return;
    m_measure = measure;
    emit measureChanged();
    if (rowCount() && columnCount())
        emit dataChanged(index(0, 1), index(rowCount() - 1, columnCount() - 1));
}

void StatsModel::refresh() {
    beginResetModel();
    m_populations = m_backend->analysis().populationOrder();
    endResetModel();
}

Backend::Backend(FilePicker *picker, QObject *parent) : QObject(parent), m_picker(picker), m_stats(this) {
    connect(m_picker, &FilePicker::samplesSelected, this, &Backend::openUrls);
    connect(m_picker, &FilePicker::workspaceSelected, this, [this](const QUrl &url) { loadWorkspace(url.toLocalFile()); });
    connect(m_picker, &FilePicker::saveSelected, this, [this](const QUrl &url) { writeWorkspace(url.toLocalFile()); });
    connect(m_picker, &FilePicker::exportSelected, this, [this](const QUrl &url) { writeStats(url.toLocalFile()); });
    connect(m_picker, &FilePicker::failed, this, &Backend::setProblem);
    connect(m_picker, &FilePicker::cancelled, this, &Backend::pickerCancelled);
    m_stats.refresh();
}

QVariantList Backend::samples() const {
    QVariantList list;
    for (const auto &sample : m_analysis.samples)
        list << QVariantMap{{"name", sample->name}, {"events", sample->events}, {"path", sample->path}};
    return list;
}

void Backend::setCurrentSample(int index) {
    if (m_analysis.samples.isEmpty())
        index = -1;
    else
        index = qBound(0, index, int(m_analysis.samples.size()) - 1);
    if (index == m_currentSample)
        return;
    m_currentSample = index;
    emit currentSampleChanged();
    emit populationsChanged();
    emit problemChanged();
    emit plotChanged();
}

PopulationStats Backend::currentStats(int population) {
    if (m_currentSample < 0)
        return {};
    return m_analysis.stats(m_currentSample, population);
}

QVariantList Backend::populations() {
    QVariantList list;
    for (int id : m_analysis.populationOrder()) {
        const PopulationStats stats = currentStats(id);
        const Gate *gate = m_analysis.gate(id);
        list << QVariantMap{{"id", id},
                            {"name", populationName(id)},
                            {"depth", m_analysis.depth(id)},
                            {"count", stats.count},
                            {"percent", stats.percentOfParent()},
                            {"available", stats.available && m_currentSample >= 0},
                            {"type", gate ? Gate::typeId(gate->type) : QString()},
                            {"axes", gate ? m_analysis.displayName(gate->x)
                                                + (gate->isOneDimensional() ? QString() : QStringLiteral(" × ") + m_analysis.displayName(gate->y))
                                          : QString()}};
    }
    return list;
}

QString Backend::populationName(int id) const {
    return id == 0 ? tr("All events") : m_analysis.name(id);
}

void Backend::setCurrentPopulation(int id) {
    if (id != 0 && !m_analysis.gate(id))
        id = 0;
    if (id == m_currentPopulation)
        return;
    rememberView();
    m_currentPopulation = id;
    if (m_views.contains(id)) {
        const View view = m_views.value(id);
        if (m_analysis.hasChannel(view.x) && m_analysis.hasChannel(view.y))
            m_view = view;
    }
    m_selectedGate = 0;
    emit currentPopulationChanged();
    emit selectedGateChanged();
    emit viewChanged();
    emit plotChanged();
}

void Backend::setSelectedGate(int id) {
    if (id != 0 && !m_analysis.gate(id))
        id = 0;
    if (id == m_selectedGate)
        return;
    m_selectedGate = id;
    emit selectedGateChanged();
    emit plotChanged();
}

QStringList Backend::channelNames() const {
    QStringList names;
    for (const QString &channel : m_analysis.channels())
        names << m_analysis.displayName(channel);
    return names;
}

void Backend::rememberView() { m_views.insert(m_currentPopulation, m_view); }

void Backend::setXChannel(const QString &channel) {
    if (channel == m_view.x || !m_analysis.hasChannel(channel))
        return;
    m_view.x = channel;
    rememberView();
    emit viewChanged();
    emit plotChanged();
}

void Backend::setYChannel(const QString &channel) {
    if (channel == m_view.y || !m_analysis.hasChannel(channel))
        return;
    m_view.y = channel;
    rememberView();
    emit viewChanged();
    emit plotChanged();
}

void Backend::cycleXChannel(int delta) {
    const QStringList list = channels();
    if (!list.isEmpty())
        setXChannel(list[(list.indexOf(m_view.x) + delta + list.size()) % list.size()]);
}

void Backend::cycleYChannel(int delta) {
    const QStringList list = channels();
    if (!list.isEmpty())
        setYChannel(list[(list.indexOf(m_view.y) + delta + list.size()) % list.size()]);
}

void Backend::swapAxes() {
    if (m_view.histogram)
        return;
    std::swap(m_view.x, m_view.y);
    rememberView();
    emit viewChanged();
    emit plotChanged();
}

void Backend::setXScale(int scale) {
    if (m_view.x.isEmpty() || scale < 0 || scale > 2 || scale == xScale())
        return;
    m_analysis.scales.insert(m_view.x, Scale(scale));
    // Scales are saved in the workspace, so changing one is a change to save.
    setDirty(true);
    emit viewChanged();
    emit plotChanged();
}

void Backend::setYScale(int scale) {
    if (m_view.y.isEmpty() || scale < 0 || scale > 2 || scale == yScale())
        return;
    m_analysis.scales.insert(m_view.y, Scale(scale));
    setDirty(true);
    emit viewChanged();
    emit plotChanged();
}

void Backend::cycleXScale() { setXScale((xScale() + 1) % 3); }
void Backend::cycleYScale() { setYScale((yScale() + 1) % 3); }

void Backend::setHistogram(bool histogram) {
    if (histogram == m_view.histogram)
        return;
    m_view.histogram = histogram;
    rememberView();
    m_selectedGate = 0;
    emit selectedGateChanged();
    emit viewChanged();
    emit plotChanged();
}

void Backend::setCompensate(bool on) {
    on = on && !m_analysis.compensation.isEmpty();
    if (on == m_analysis.compensate)
        return;
    remember();
    m_analysis.compensate = on;
    m_analysis.compensationChanged();
    emit compensationChanged();
    gatesEdited();
}

QVariantList Backend::compensationMatrix() const {
    QVariantList list;
    for (double value : m_analysis.compensation.matrix)
        list << value;
    return list;
}

bool Backend::canResetCompensation() const {
    for (const auto &sample : m_analysis.samples)
        if (!sample->spillover.isEmpty() && !(sample->spillover == m_analysis.compensation))
            return true;
    return false;
}

void Backend::setCompensationValue(int row, int column, double value) {
    const int n = m_analysis.compensation.channels.size();
    if (row < 0 || column < 0 || row >= n || column >= n || m_analysis.compensation.at(row, column) == value)
        return;
    remember();
    m_analysis.compensation.matrix[row * n + column] = value;
    m_analysis.compensationChanged();
    emit compensationChanged();
    gatesEdited();
}

void Backend::resetCompensation() {
    // The current sample's own matrix, or the first one any sample carries.
    Spillover source;
    if (m_currentSample >= 0)
        source = m_analysis.samples[m_currentSample]->spillover;
    for (int i = 0; source.isEmpty() && i < m_analysis.samples.size(); ++i)
        source = m_analysis.samples[i]->spillover;
    if (source.isEmpty() || source == m_analysis.compensation)
        return;
    remember();
    m_analysis.compensation = source;
    m_analysis.compensate = true;
    m_analysis.compensationChanged();
    emit compensationChanged();
    gatesEdited();
}

void Backend::createCompensation() {
    if (m_analysis.samples.isEmpty() || !m_analysis.compensation.isEmpty())
        return;
    const FcsSample &sample = *m_analysis.samples.value(qMax(0, m_currentSample));
    QStringList fluorescence;
    for (int c = 0; c < sample.channels.size(); ++c)
        if (!sample.isScatterOrTime(c))
            fluorescence << sample.channels[c];
    if (fluorescence.isEmpty())
        return;
    remember();
    m_analysis.compensation = Spillover::identity(fluorescence);
    m_analysis.compensate = true;
    m_analysis.compensationChanged();
    emit compensationChanged();
    gatesEdited();
}

QString Backend::problem() {
    if (!m_problem.isEmpty())
        return m_problem;
    return m_currentSample >= 0 ? m_analysis.compensationProblem(m_currentSample) : QString();
}

QString Backend::fileName() const { return m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).fileName(); }

void Backend::setProblem(const QString &problem) {
    m_problem = problem;
    emit problemChanged();
}

void Backend::setNotice(const QString &notice) {
    m_notice = notice;
    emit noticeChanged();
}

void Backend::dismissProblem() { setProblem({}); }
void Backend::dismissNotice() { setNotice({}); }

void Backend::setDirty(bool dirty) {
    if (dirty == m_dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}

QString Backend::suggestedDirectory() const {
    if (!m_path.isEmpty())
        return QFileInfo(m_path).absolutePath();
    if (!m_analysis.samples.isEmpty())
        return QFileInfo(m_analysis.samples.first()->path).absolutePath();
    return QDir::homePath();
}

void Backend::addSample(const QString &path, QStringList *problems) {
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (const auto &sample : m_analysis.samples)
        if (sample->path == absolute)
            return;
    QString error;
    auto sample = Fcs::read(absolute, &error);
    if (!sample) {
        *problems << tr("%1: %2").arg(QFileInfo(path).fileName(), error);
        return;
    }
    m_analysis.addSample(std::make_shared<const FcsSample>(std::move(*sample)));
}

void Backend::openUrls(const QList<QUrl> &urls) {
    QStringList paths;
    for (const QUrl &url : urls)
        paths << (url.isLocalFile() ? url.toLocalFile() : url.toString());
    openPaths(paths);
}

void Backend::openPaths(const QStringList &paths) {
    QStringList samples;
    for (const QString &path : paths) {
        if (path.endsWith(".cyto", Qt::CaseInsensitive)) {
            if (!loadWorkspace(path))
                return;
        } else {
            samples << path;
        }
    }
    if (samples.isEmpty())
        return;
    QStringList problems;
    const qsizetype before = m_analysis.samples.size();
    const Spillover compensation = m_analysis.compensation;
    for (const QString &path : samples)
        addSample(path, &problems);
    if (!problems.isEmpty())
        setProblem(tr("Some files couldn't be opened:\n%1").arg(problems.join('\n')));
    if (m_analysis.samples.size() == before)
        return;
    if (m_currentSample < 0)
        m_currentSample = 0;
    if (!(compensation == m_analysis.compensation))
        m_analysis.compensationChanged();
    pickDefaultAxes();
    setDirty(true);
    everythingChanged();
}

void Backend::pickDefaultAxes() {
    const QStringList list = channels();
    if (list.isEmpty())
        return;
    auto find = [&](const QString &prefix) {
        for (const QString &channel : list)
            if (channel.startsWith(prefix, Qt::CaseInsensitive) && channel.endsWith("-A", Qt::CaseInsensitive))
                return channel;
        for (const QString &channel : list)
            if (channel.startsWith(prefix, Qt::CaseInsensitive))
                return channel;
        return QString();
    };
    if (!list.contains(m_view.x)) {
        m_view.x = find("FSC");
        if (m_view.x.isEmpty())
            m_view.x = list.first();
    }
    if (!list.contains(m_view.y)) {
        m_view.y = find("SSC");
        if (m_view.y.isEmpty() || m_view.y == m_view.x)
            m_view.y = list.value(list.indexOf(m_view.x) == 0 ? 1 : 0, list.first());
    }
}

void Backend::everythingChanged() {
    m_stats.refresh();
    emit samplesChanged();
    emit currentSampleChanged();
    emit populationsChanged();
    emit currentPopulationChanged();
    emit selectedGateChanged();
    emit viewChanged();
    emit compensationChanged();
    emit problemChanged();
    emit historyChanged();
    emit fileChanged();
    emit plotChanged();
}

void Backend::removeSample(int index) {
    if (index < 0 || index >= m_analysis.samples.size())
        return;
    m_analysis.removeSample(index);
    // Stay on the same file when one above it goes.
    if (index < m_currentSample)
        --m_currentSample;
    if (m_currentSample >= m_analysis.samples.size())
        m_currentSample = int(m_analysis.samples.size()) - 1;
    setDirty(true);
    everythingChanged();
}

void Backend::stepSample(int delta) {
    const int n = int(m_analysis.samples.size());
    if (n > 0)
        setCurrentSample(((m_currentSample + delta) % n + n) % n);
}

void Backend::addSamples() { m_picker->openSamples(); }
void Backend::openWorkspace() { m_picker->openWorkspace(); }

void Backend::saveWorkspace() {
    if (m_path.isEmpty())
        saveWorkspaceAs();
    else
        writeWorkspace(m_path);
}

void Backend::saveWorkspaceAs() {
    const QString name = m_path.isEmpty() ? QStringLiteral("analysis.cyto") : QFileInfo(m_path).fileName();
    m_picker->saveWorkspace(QUrl::fromLocalFile(suggestedDirectory() + "/" + name));
}

void Backend::exportStats() {
    if (m_analysis.samples.isEmpty()) {
        setProblem(tr("Add FCS files before exporting statistics."));
        return;
    }
    const QString base = m_path.isEmpty() ? QStringLiteral("cyto") : QFileInfo(m_path).completeBaseName();
    m_picker->exportStats(QUrl::fromLocalFile(suggestedDirectory() + "/" + base + "-stats.csv"));
}

bool Backend::writeWorkspace(const QString &chosen) {
    const QString path = QFileInfo(chosen).suffix().isEmpty() ? chosen + ".cyto" : chosen;
    rememberView();
    QJsonObject root = m_analysis.toJson(QFileInfo(path).absoluteDir());
    QJsonObject views;
    for (auto it = m_views.begin(); it != m_views.end(); ++it)
        if (it.key() == 0 || m_analysis.gate(it.key()))
            views.insert(QString::number(it.key()),
                         QJsonObject{{"x", it->x}, {"y", it->y}, {"histogram", it->histogram}});
    root.insert("views", views);
    root.insert("population", m_currentPopulation);

    // Written beside the target and renamed over it, so a failure never
    // leaves half a workspace behind.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit()) {
        setProblem(tr("Couldn't save %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
        return false;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    setDirty(false);
    setNotice(tr("Saved %1").arg(QFileInfo(path).fileName()));
    emit fileChanged();
    emit saved();
    return true;
}

bool Backend::writeStats(const QString &chosen) {
    const QString path = QFileInfo(chosen).suffix().isEmpty() ? chosen + ".csv" : chosen;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(m_analysis.statsCsv()) < 0 || !file.commit()) {
        setProblem(tr("Couldn't export %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
        return false;
    }
    setNotice(tr("Exported %1").arg(QFileInfo(path).fileName()));
    return true;
}

bool Backend::loadWorkspace(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setProblem(tr("Couldn't open %1: %2").arg(QFileInfo(path).fileName(), file.errorString()));
        return false;
    }
    const QByteArray bytes = file.readAll();
    QStringList problems;
    auto analysis = Analysis::fromJson(bytes, QFileInfo(path).absoluteDir(), &problems);
    if (!analysis) {
        setProblem(tr("Couldn't open %1: %2").arg(QFileInfo(path).fileName(), problems.join('\n')));
        return false;
    }
    m_analysis = std::move(*analysis);
    m_undo.clear();
    m_redo.clear();
    m_path = QFileInfo(path).absoluteFilePath();
    m_views.clear();
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    const QJsonObject views = root.value("views").toObject();
    for (auto it = views.begin(); it != views.end(); ++it) {
        const QJsonObject view = it.value().toObject();
        m_views.insert(it.key().toInt(), {view.value("x").toString(), view.value("y").toString(), view.value("histogram").toBool()});
    }
    m_currentSample = m_analysis.samples.isEmpty() ? -1 : 0;
    m_currentPopulation = m_analysis.gate(root.value("population").toInt()) ? root.value("population").toInt() : 0;
    m_selectedGate = 0;
    m_view = m_views.value(m_currentPopulation, m_views.value(0));
    pickDefaultAxes();
    m_problem = problems.isEmpty() ? QString() : tr("Some samples couldn't be opened:\n%1").arg(problems.join('\n'));
    m_dirty = false;
    emit dirtyChanged();
    everythingChanged();
    return true;
}

Backend::Snapshot Backend::snapshot() const {
    return {m_analysis.gates, m_analysis.compensation, m_analysis.compensate, m_currentPopulation, m_selectedGate};
}

void Backend::remember() {
    m_undo.append(snapshot());
    if (m_undo.size() > 200)
        m_undo.removeFirst();
    m_redo.clear();
}

void Backend::restore(const Snapshot &state) {
    const bool compensationDiffers = !(state.compensation == m_analysis.compensation) || state.compensate != m_analysis.compensate;
    m_analysis.gates = state.gates;
    m_analysis.compensation = state.compensation;
    m_analysis.compensate = state.compensate;
    if (compensationDiffers) {
        m_analysis.compensationChanged();
        emit compensationChanged();
    }
    const int population = m_analysis.gate(state.population) ? state.population : 0;
    if (population != m_currentPopulation) {
        rememberView();
        m_currentPopulation = population;
        if (m_views.contains(population))
            m_view = m_views.value(population);
        pickDefaultAxes();
        emit currentPopulationChanged();
        emit viewChanged();
    }
    m_selectedGate = m_analysis.gate(state.selected) ? state.selected : 0;
    emit selectedGateChanged();
    gatesEdited();
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(snapshot());
    restore(m_undo.takeLast());
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(snapshot());
    restore(m_redo.takeLast());
}

void Backend::gatesEdited() {
    m_analysis.gatesChanged();
    m_stats.refresh();
    setDirty(true);
    emit populationsChanged();
    emit problemChanged();
    emit historyChanged();
    emit plotChanged();
}

int Backend::addGate(Gate::Type type, const QVector<QPointF> &points) {
    if (m_analysis.samples.isEmpty())
        return 0;
    Gate gate;
    gate.id = m_analysis.nextGateId();
    gate.parent = m_currentPopulation;
    gate.name = m_analysis.uniqueName("P");
    gate.type = m_view.histogram ? Gate::Range : type;
    gate.x = m_view.x;
    gate.y = m_view.histogram ? QString() : m_view.y;
    gate.xScale = m_analysis.scale(gate.x);
    gate.yScale = m_view.histogram ? Scale::Linear : m_analysis.scale(gate.y);
    gate.points = points;
    if (!gate.isValid())
        return 0;
    remember();
    m_analysis.gates << gate;
    m_selectedGate = gate.id;
    emit selectedGateChanged();
    gatesEdited();
    return gate.id;
}

void Backend::updateGate(int id, const QVector<QPointF> &points, Scale xScale, Scale yScale) {
    const int index = m_analysis.gateIndex(id);
    if (index < 0)
        return;
    Gate edited = m_analysis.gates[index];
    edited.points = points;
    edited.xScale = xScale;
    edited.yScale = yScale;
    if (!edited.isValid())
        return;
    remember();
    m_analysis.gates[index] = edited;
    gatesEdited();
}

void Backend::deleteGate(int id) {
    const Gate *gate = m_analysis.gate(id);
    if (!gate)
        return;
    remember();
    const int parent = gate->parent;
    QVector<int> removed = m_analysis.descendants(id);
    removed << id;
    m_analysis.gates.removeIf([&](const Gate &g) { return removed.contains(g.id); });
    if (removed.contains(m_currentPopulation))
        setCurrentPopulation(parent);
    // A new gate can reuse a deleted one's id; it mustn't inherit its axes.
    for (int gone : removed)
        m_views.remove(gone);
    m_selectedGate = 0;
    emit selectedGateChanged();
    gatesEdited();
}

bool Backend::renameGate(int id, const QString &name) {
    const int index = m_analysis.gateIndex(id);
    // A slash would make the population's path ambiguous.
    const QString cleaned = QString(name).replace('/', '-').trimmed();
    if (index < 0 || cleaned.isEmpty())
        return false;
    if (cleaned == m_analysis.gates[index].name)
        return true;
    remember();
    m_analysis.gates[index].name = cleaned;
    gatesEdited();
    return true;
}

void Backend::stepPopulation(int delta) {
    const QVector<int> order = m_analysis.populationOrder();
    const int at = order.indexOf(m_currentPopulation);
    setCurrentPopulation(order.value(qBound(0, at + delta, int(order.size()) - 1)));
}

void Backend::openParent() {
    if (const Gate *gate = m_analysis.gate(m_currentPopulation)) {
        const int child = gate->id;
        setCurrentPopulation(gate->parent);
        // Leaving a gate selects it, so Enter goes straight back in.
        setSelectedGate(child);
    }
}

void Backend::openGate(int id) {
    if (m_analysis.gate(id))
        setCurrentPopulation(id);
}
