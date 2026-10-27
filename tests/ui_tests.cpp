// Drives the real window offscreen with the mouse and keyboard, as a person
// would. Set CYTO_UI_SHOTS to a directory to keep a screenshot of each test.
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QJSValue>
#include <QtTest>
#include <memory>

#include "backend.h"
#include "filepicker.h"
#include "plotitem.h"
#include "testdata.h"
#include "theme.h"
#include "translator.h"

namespace {
class FakePicker : public FilePicker {
public:
    QString path;
    void openSamples() override { if (!path.isEmpty()) emit samplesSelected({QUrl::fromLocalFile(path)}); }
    void openWorkspace() override { if (!path.isEmpty()) emit workspaceSelected(QUrl::fromLocalFile(path)); }
    void saveWorkspace(const QUrl &) override {
        if (!path.isEmpty())
            emit saveSelected(QUrl::fromLocalFile(path));
        else
            emit cancelled();
    }
    void exportStats(const QUrl &) override { if (!path.isEmpty()) emit exportSelected(QUrl::fromLocalFile(path)); }
};

void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

// One window, its backend, and a private theme and language.
struct Session {
    QTemporaryDir dir;
    FakePicker picker;
    std::unique_ptr<Translator> translator;
    std::unique_ptr<Theme> theme;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;
    PlotItem *plot = nullptr;

    explicit Session(const QString &language = "en", bool samples = true, QSize size = QSize(1320, 820)) {
        write(dir.filePath("language"), language.toUtf8());
        write(dir.filePath("state/theme/colors.toml"), "accent = \"#FFD60A\"\n");
        translator = std::make_unique<Translator>(dir.filePath("language"));
        qApp->installTranslator(translator.get());
        theme = std::make_unique<Theme>(dir.filePath("state"), nullptr, "/bin/false");
        backend = std::make_unique<Backend>(&picker);
        if (samples)
            backend->openPaths({writeTestSample(dir.filePath("donor-a.fcs"), testSample("donor-a", 6000, 4000, 0.6, 1)),
                                writeTestSample(dir.filePath("donor-b.fcs"), testSample("donor-b", 4000, 6000, 0.4, 2))});
        engine = std::make_unique<QQmlApplicationEngine>();
        QObject::connect(translator.get(), &Translator::changed, engine.get(), &QQmlEngine::retranslate);
        engine->rootContext()->setContextProperty("translator", translator.get());
        engine->rootContext()->setContextProperty("theme", theme.get());
        engine->rootContext()->setContextProperty("backend", backend.get());
        engine->load(QUrl("qrc:/Main.qml"));
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        if (!window)
            return;
        window->resize(size);
        window->show();
        plot = window->findChild<PlotItem *>();
        settle();
    }
    ~Session() {
        if (window)
            window->close();
        engine.reset();
        qApp->removeTranslator(translator.get());
    }

    void settle(int rounds = 10) {
        for (int i = 0; i < rounds; ++i) {
            QCoreApplication::processEvents();
            QTest::qWait(15);
        }
    }
    void key(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString &text = {}) {
        QKeyEvent press(QEvent::KeyPress, key, modifiers, text), release(QEvent::KeyRelease, key, modifiers, text);
        QCoreApplication::sendEvent(window, &press);
        QCoreApplication::sendEvent(window, &release);
        settle();
    }
    void press(QChar c) { key(c.toUpper().unicode(), Qt::NoModifier, QString(c)); }
    void waitForDialog(bool open) {
        QTRY_VERIFY_WITH_TIMEOUT(window->property("dialogOpen").toBool() == open, 2000);
        QTest::qWait(300);
        settle();
    }
    // Where a value on the plot's current (scaled) axes is in the window.
    QPoint at(double x, double y) const { return plot->mapToScene(plot->toPixel(QPointF(x, y))).toPoint(); }
    QPoint at(Scale scale, double x, double y) const { return at(Scales::forward(scale, x), Scales::forward(scale, y)); }
    void drag(QPoint from, QPoint to) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        for (int step = 1; step <= 6; ++step)
            QTest::mouseMove(window, from + (to - from) * step / 6);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        settle();
    }
    void click(QPoint point) {
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
        settle();
    }
    QObject *named(const char *name) { return window->findChild<QObject *>(name); }
    bool shown(const char *name) {
        QObject *object = named(name);
        return object && object->property("visible").toBool();
    }
    // Every visible text, dialogs included: the overlay is a child of the root item too.
    QStringList texts() {
        QStringList list;
        QList<QQuickItem *> pending{window->contentItem()};
        while (!pending.isEmpty()) {
            QQuickItem *item = pending.takeFirst();
            if (!item->isVisible() || item->opacity() == 0)
                continue;
            const QString text = item->property("text").toString();
            if (!text.isEmpty())
                list << text;
            pending.append(item->childItems());
        }
        return list;
    }
    void shot(const QString &name) {
        const QString directory = qEnvironmentVariable("CYTO_UI_SHOTS");
        if (directory.isEmpty())
            return;
        QTest::qWait(400);
        settle();
        QDir().mkpath(directory);
        window->grabWindow().save(directory + "/" + name + ".png");
    }
};
}

class UiTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
        QQuickStyle::setStyle("Material");
        qmlRegisterType<PlotItem>("Cyto", 1, 0, "Plot");
    }

    void anEmptyWindowInvitesFiles() {
        Session s("en", false);
        QVERIFY(s.window);
        QVERIFY(s.shown("empty"));
        QVERIFY(!s.plot->isVisible());
        QVERIFY(s.texts().contains("Drop FCS files here"));
        s.picker.path = writeTestSample(s.dir.filePath("c.fcs"), testSample("c", 50, 50, 0.5));
        s.key(Qt::Key_O, Qt::ControlModifier);
        QCOMPARE(s.backend->samples().size(), 1);
        QVERIFY(!s.shown("empty"));
        s.shot("empty-then-added");
    }

    void drawsARectangleGateWithTheMouse() {
        Session s;
        QVERIFY(s.plot && s.plot->isVisible());
        s.press('r');
        QCOMPARE(s.plot->tool(), 1);
        s.drag(s.at(30000, 5000), s.at(80000, 50000));
        QCOMPARE(s.backend->analysis().gates.size(), 1);
        const Gate &gate = s.backend->analysis().gates.first();
        QCOMPARE(gate.type, Gate::Rectangle);
        QCOMPARE(gate.x, QString("FSC-A"));
        QCOMPARE(s.backend->currentStats(gate.id).count, 6000);
        QCOMPARE(s.plot->tool(), 0); // back to selecting
        QVERIFY(s.backend->dirty());
        s.shot("rectangle");
    }

    void drawsAPolygonAndClosesItWithEnter() {
        Session s;
        s.press('p');
        for (const QPoint &corner : {s.at(25000, 5000), s.at(85000, 5000), s.at(85000, 45000), s.at(25000, 45000)})
            s.click(corner);
        QVERIFY(s.plot->drawing());
        s.shot("polygon-drawing");
        s.key(Qt::Key_Return, Qt::NoModifier, "\r");
        QVERIFY(!s.plot->drawing());
        QCOMPARE(s.backend->analysis().gates.size(), 1);
        QCOMPARE(s.backend->analysis().gates.first().type, Gate::Polygon);
        QCOMPARE(s.backend->analysis().gates.first().points.size(), 4);
        QCOMPARE(s.backend->currentStats(s.backend->analysis().gates.first().id).count, 6000);
    }

    void movesAGateAndUndoesTheMove() {
        Session s;
        const int id = s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        s.backend->setSelectedGate(0);
        s.settle();
        // Picked up and dragged off the small cluster onto the large one.
        s.drag(s.at(55000, 25000), s.at(155000, 105000));
        QCOMPARE(s.backend->selectedGate(), id);
        QCOMPARE(s.backend->currentStats(id).count, 4000);
        s.key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(s.backend->currentStats(id).count, 6000);
        s.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(s.backend->currentStats(id).count, 4000);
    }

    void reshapesAGateByItsHandle() {
        Session s;
        const int id = s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {40000, 50000}});
        s.settle();
        QVERIFY(s.backend->currentStats(id).count < 6000);
        // The far corner, pulled out past the cluster.
        s.drag(s.at(40000, 50000), s.at(80000, 50000));
        QCOMPARE(s.backend->currentStats(id).count, 6000);
        s.shot("reshaped");
    }

    void doubleClickLooksInsideAGateAndLeftComesBack() {
        Session s;
        const int id = s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        s.settle();
        QTest::mouseDClick(s.window, Qt::LeftButton, Qt::NoModifier, s.at(55000, 25000));
        s.settle();
        QCOMPARE(s.backend->currentPopulation(), id);
        s.key(Qt::Key_Left);
        QCOMPARE(s.backend->currentPopulation(), 0);
        QCOMPARE(s.backend->selectedGate(), id);
        s.key(Qt::Key_Return, Qt::NoModifier, "\r");
        QCOMPARE(s.backend->currentPopulation(), id);
    }

    void aHistogramGatesARange() {
        Session s;
        const int small = s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        s.backend->openGate(small);
        s.backend->setXChannel("FL1-A");
        s.press('h');
        QVERIFY(s.backend->histogram());
        s.press('r');
        // Out past the right edge, to take in the events piled up there.
        s.drag(s.at(Scale::Arcsinh, 2000, 0), s.plot->mapToScene(QPointF(s.plot->plotArea().right() + 6, 300)).toPoint());
        const Gate &gate = s.backend->analysis().gates.last();
        QCOMPARE(gate.type, Gate::Range);
        QCOMPARE(gate.parent, small);
        QCOMPARE(s.backend->currentStats(gate.id).count, 3600);
        // The far bound stays finite on every scale, so it survives a switch and a save.
        QVERIFY(gate.isValid());
        QVERIFY(std::isfinite(Scales::inverse(gate.xScale, std::max(gate.points[0].x(), gate.points[1].x()))));
        s.shot("histogram");
    }

    void spaceStepsThroughTheSamplesWithTheSameGates() {
        Session s;
        const int id = s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        QCOMPARE(s.backend->currentStats(id).count, 6000);
        s.key(Qt::Key_Space, Qt::NoModifier, " ");
        QCOMPARE(s.backend->currentSample(), 1);
        QCOMPARE(s.backend->currentStats(id).count, 4000);
        s.key(Qt::Key_Space, Qt::NoModifier, " ");
        QCOMPARE(s.backend->currentSample(), 0);
    }

    void deleteRemovesTheGateAndWhatIsInside() {
        Session s;
        const int outer = s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        s.backend->openGate(outer);
        s.backend->addGate(Gate::Rectangle, {{0, 0}, {5, 5}});
        s.backend->openParent();
        QCOMPARE(s.backend->selectedGate(), outer);
        s.key(Qt::Key_Delete);
        QVERIFY(s.backend->analysis().gates.isEmpty());
    }

    void renamesWithF2() {
        Session s;
        s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        s.key(Qt::Key_F2);
        s.waitForDialog(true);
        for (QChar c : QString("Lymphocytes"))
            s.key(c.toUpper().unicode(), c.isUpper() ? Qt::ShiftModifier : Qt::NoModifier, QString(c));
        s.key(Qt::Key_Return, Qt::NoModifier, "\r");
        s.waitForDialog(false);
        QCOMPARE(s.backend->analysis().gates.first().name, QString("Lymphocytes"));
        // Typing in the field didn't trigger the single-key shortcuts.
        QCOMPARE(s.plot->tool(), 0);
        QVERIFY(!s.backend->histogram());
    }

    void quitAsksWhenThereAreUnsavedGates() {
        Session s;
        s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        s.press('q');
        s.waitForDialog(true);
        QVERIFY(s.shown("unsaved"));
        s.shot("unsaved");
        s.key(Qt::Key_Escape);
        s.waitForDialog(false);
        QVERIFY(s.window->isVisible());
    }

    void savingBeforeQuittingWritesTheWorkspace() {
        Session s;
        s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        s.picker.path = s.dir.filePath("analysis.cyto");
        s.press('q');
        s.waitForDialog(true);
        s.key(Qt::Key_Return, Qt::NoModifier, "\r");
        QTRY_VERIFY(QFile::exists(s.dir.filePath("analysis.cyto")));
        QVERIFY(!s.backend->dirty());
    }

    // Cancelling the chooser after choosing Save on quit must not leave the
    // quit waiting for the next save.
    void aCancelledSaveForgetsTheQuit() {
        Session s;
        s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        s.press('q');
        s.waitForDialog(true);
        s.key(Qt::Key_Return, Qt::NoModifier, "\r");
        s.waitForDialog(false);
        QVERIFY(s.window->property("afterSave").isNull() || !s.window->property("afterSave").isValid()
                || s.window->property("afterSave").value<QJSValue>().isNull());
        QVERIFY(s.backend->dirty());
    }

    void theStatisticsTableShowsEverySample() {
        Session s;
        const int id = s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        s.backend->renameGate(id, "Small");
        s.press('t');
        QVERIFY(s.shown("stats"));
        QVERIFY(!s.plot->isVisible());
        auto *model = qobject_cast<StatsModel *>(s.backend->statsModel());
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->columnCount(), 3);
        QCOMPARE(model->data(model->index(1, 2), Qt::DisplayRole).toString(), QString("40.0 %"));
        s.press('m');
        QCOMPARE(model->measure(), 1);
        s.press('m');
        QCOMPARE(model->data(model->index(1, 2), Qt::DisplayRole).toString(), QString("4,000"));
        s.shot("stats");
        s.picker.path = s.dir.filePath("out.csv");
        s.key(Qt::Key_E, Qt::ControlModifier);
        QVERIFY(QFile::exists(s.dir.filePath("out.csv")));
    }

    void theCompensationMatrixOpensAndToggles() {
        Session s;
        QVERIFY(s.backend->compensate());
        s.press('c');
        QVERIFY(!s.backend->compensate());
        s.press('c');
        QVERIFY(s.backend->compensate());
        s.key(Qt::Key_C, Qt::ShiftModifier, "C");
        s.waitForDialog(true);
        QVERIFY(s.shown("compensation"));
        s.shot("compensation");
    }

    // Focusing a matrix cell and leaving it must not round the value it shows.
    void leavingAMatrixCellKeepsItsFullValue() {
        Session s;
        s.backend->setCompensationValue(0, 1, 0.123456);
        const bool dirtyBefore = s.backend->dirty();
        s.key(Qt::Key_C, Qt::ShiftModifier, "C");
        s.waitForDialog(true);
        QQuickItem *field = nullptr;
        QList<QQuickItem *> pending{s.window->contentItem()};
        while (!pending.isEmpty() && !field) {
            QQuickItem *item = pending.takeFirst();
            if (item->isVisible() && item->property("text").toString() == "0.1235" && item->property("validator").isValid())
                field = item;
            pending.append(item->childItems());
        }
        QVERIFY(field);
        field->forceActiveFocus();
        s.settle();
        s.key(Qt::Key_Tab);
        QCOMPARE(s.backend->analysis().compensation.at(0, 1), 0.123456);
        QCOMPARE(s.backend->dirty(), dirtyBefore);
    }

    void helpListsTheShortcuts() {
        Session s;
        s.press('?');
        s.waitForDialog(true);
        QVERIFY(s.texts().contains("Keyboard shortcuts"));
        s.shot("help");
        s.press('?');
        s.waitForDialog(false);
    }

    void danishLeavesNoEnglishBehind() {
        Session s("da");
        s.backend->addGate(Gate::Rectangle, {{30000, 5000}, {80000, 50000}});
        s.settle();
        s.shot("danish");
        // Words that would only show untranslated; channel names and keys stay.
        for (const QString &text : s.texts())
            for (const char *english : {"Samples", "Populations", "All events", "Statistics", "Compensate", "events", "Sample "})
                QVERIFY2(!text.contains(english), qPrintable(text));
        s.press('?');
        s.waitForDialog(true);
        s.shot("danish-help");
        for (const QString &text : s.texts())
            for (const char *english : {"Keyboard shortcuts", "Draw a", "Undo", "Quit", "Next sample"})
                QVERIFY2(!text.contains(english), qPrintable(text));
    }
};

QTEST_MAIN(UiTests)
#include "ui_tests.moc"
