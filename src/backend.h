#pragma once

#include <QAbstractTableModel>
#include <QObject>
#include <QPointF>
#include <QUrl>
#include <QVariantList>

#include "analysis.h"

class Backend;
class FilePicker;

// Samples down, populations across, for the statistics view.
class StatsModel : public QAbstractTableModel {
    Q_OBJECT
    // 0 is % of parent, 1 is % of total, 2 is the event count.
    Q_PROPERTY(int measure READ measure WRITE setMeasure NOTIFY measureChanged)

public:
    enum Roles { DetailRole = Qt::UserRole + 1, AvailableRole, HeaderRole };

    explicit StatsModel(Backend *backend);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int measure() const { return m_measure; }
    void setMeasure(int measure);
    Q_INVOKABLE void cycleMeasure() { setMeasure((m_measure + 1) % 3); }
    void refresh();

signals:
    void measureChanged();

private:
    Backend *m_backend;
    QVector<int> m_populations;
    int m_measure = 0;
};

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList samples READ samples NOTIFY samplesChanged)
    Q_PROPERTY(int currentSample READ currentSample WRITE setCurrentSample NOTIFY currentSampleChanged)
    Q_PROPERTY(QVariantList populations READ populations NOTIFY populationsChanged)
    Q_PROPERTY(int currentPopulation READ currentPopulation WRITE setCurrentPopulation NOTIFY currentPopulationChanged)
    Q_PROPERTY(int selectedGate READ selectedGate WRITE setSelectedGate NOTIFY selectedGateChanged)
    Q_PROPERTY(QStringList channels READ channels NOTIFY samplesChanged)
    Q_PROPERTY(QStringList channelNames READ channelNames NOTIFY samplesChanged)
    Q_PROPERTY(QString xChannel READ xChannel WRITE setXChannel NOTIFY viewChanged)
    Q_PROPERTY(QString yChannel READ yChannel WRITE setYChannel NOTIFY viewChanged)
    Q_PROPERTY(int xScale READ xScale WRITE setXScale NOTIFY viewChanged)
    Q_PROPERTY(int yScale READ yScale WRITE setYScale NOTIFY viewChanged)
    Q_PROPERTY(bool histogram READ histogram WRITE setHistogram NOTIFY viewChanged)
    Q_PROPERTY(bool compensate READ compensate WRITE setCompensate NOTIFY compensationChanged)
    Q_PROPERTY(QStringList compensationChannels READ compensationChannels NOTIFY compensationChanged)
    Q_PROPERTY(QVariantList compensationMatrix READ compensationMatrix NOTIFY compensationChanged)
    Q_PROPERTY(bool canResetCompensation READ canResetCompensation NOTIFY compensationChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QObject *stats READ statsModel CONSTANT)

public:
    explicit Backend(FilePicker *picker, QObject *parent = nullptr);

    Analysis &analysis() { return m_analysis; }

    QVariantList samples() const;
    int currentSample() const { return m_currentSample; }
    void setCurrentSample(int index);
    QVariantList populations();
    int currentPopulation() const { return m_currentPopulation; }
    void setCurrentPopulation(int id);
    int selectedGate() const { return m_selectedGate; }
    void setSelectedGate(int id);
    QStringList channels() const { return m_analysis.channels(); }
    QStringList channelNames() const;

    QString xChannel() const { return m_view.x; }
    QString yChannel() const { return m_view.y; }
    void setXChannel(const QString &channel);
    void setYChannel(const QString &channel);
    int xScale() const { return int(m_analysis.scale(m_view.x)); }
    int yScale() const { return int(m_analysis.scale(m_view.y)); }
    void setXScale(int scale);
    void setYScale(int scale);
    bool histogram() const { return m_view.histogram; }
    void setHistogram(bool histogram);

    bool compensate() const { return m_analysis.compensate; }
    void setCompensate(bool on);
    QStringList compensationChannels() const { return m_analysis.compensation.channels; }
    QVariantList compensationMatrix() const;
    bool canResetCompensation() const;

    QString problem();
    QString notice() const { return m_notice; }
    bool dirty() const { return m_dirty; }
    QString fileName() const;
    QString workspacePath() const { return m_path; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QObject *statsModel() { return &m_stats; }

    // Files from the command line, a drop, or the chooser: FCS files are
    // added, a .cyto workspace replaces the analysis.
    Q_INVOKABLE void openPaths(const QStringList &paths);
    Q_INVOKABLE void openUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void addSamples();
    Q_INVOKABLE void openWorkspace();
    Q_INVOKABLE void saveWorkspace();
    Q_INVOKABLE void saveWorkspaceAs();
    Q_INVOKABLE void exportStats();
    bool writeWorkspace(const QString &path);
    bool writeStats(const QString &path);
    bool loadWorkspace(const QString &path);
    Q_INVOKABLE void removeSample(int index);
    Q_INVOKABLE void stepSample(int delta);

    Q_INVOKABLE void cycleXChannel(int delta);
    Q_INVOKABLE void cycleYChannel(int delta);
    Q_INVOKABLE void swapAxes();
    Q_INVOKABLE void cycleXScale();
    Q_INVOKABLE void cycleYScale();
    Q_INVOKABLE QString channelName(const QString &channel) const { return m_analysis.displayName(channel); }

    // Drawn with the current axes, inside the current population. Returns
    // the new gate's id.
    int addGate(Gate::Type type, const QVector<QPointF> &points);
    void updateGate(int id, const QVector<QPointF> &points, Scale xScale, Scale yScale);
    Q_INVOKABLE void deleteGate(int id);
    Q_INVOKABLE bool renameGate(int id, const QString &name);
    Q_INVOKABLE QString populationName(int id) const;
    Q_INVOKABLE void stepPopulation(int delta);
    Q_INVOKABLE void openParent();
    Q_INVOKABLE void openGate(int id);

    Q_INVOKABLE void setCompensationValue(int row, int column, double value);
    Q_INVOKABLE void resetCompensation();
    // An identity matrix over the fluorescence channels, to fill in by hand.
    Q_INVOKABLE void createCompensation();

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void dismissProblem();
    Q_INVOKABLE void dismissNotice();

    // Every population's count for the current sample, by id.
    PopulationStats currentStats(int population);

signals:
    void samplesChanged();
    void currentSampleChanged();
    void populationsChanged();
    void currentPopulationChanged();
    void selectedGateChanged();
    void viewChanged();
    void compensationChanged();
    void problemChanged();
    void noticeChanged();
    void dirtyChanged();
    void fileChanged();
    void historyChanged();
    // Anything the plot draws changed.
    void plotChanged();
    // The workspace was written; a quit waiting on it can go ahead.
    void saved();
    // A file chooser was closed without a choice, so nothing waits on it any more.
    void pickerCancelled();

private:
    struct View {
        QString x, y;
        bool histogram = false;
    };
    struct Snapshot {
        QVector<Gate> gates;
        Spillover compensation;
        bool compensate = false;
        int population = 0;
        int selected = 0;
    };

    void addSample(const QString &path, QStringList *problems);
    void remember();
    void restore(const Snapshot &snapshot);
    Snapshot snapshot() const;
    void gatesEdited();
    void setDirty(bool dirty);
    void setProblem(const QString &problem);
    void setNotice(const QString &notice);
    void pickDefaultAxes();
    void rememberView();
    void everythingChanged();
    QString suggestedDirectory() const;

    FilePicker *m_picker;
    Analysis m_analysis;
    StatsModel m_stats;
    int m_currentSample = -1;
    int m_currentPopulation = 0;
    int m_selectedGate = 0;
    View m_view;
    // The axes last used inside each population, so drilling in and back
    // out returns to the plot that defined the gate.
    QHash<int, View> m_views;
    QVector<Snapshot> m_undo, m_redo;
    QString m_path;
    QString m_problem;
    QString m_notice;
    bool m_dirty = false;
};
