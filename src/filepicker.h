#pragma once

// The picker interface follows Omacut (MIT, David Heinemeier Hansson), so
// tests can stand in for the desktop portal.

#include <QList>
#include <QObject>
#include <QUrl>

class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    // One or more FCS files to add.
    virtual void openSamples() = 0;
    virtual void openWorkspace() = 0;
    virtual void saveWorkspace(const QUrl &suggestedUrl) = 0;
    virtual void exportStats(const QUrl &suggestedUrl) = 0;

signals:
    void samplesSelected(const QList<QUrl> &urls);
    void workspaceSelected(const QUrl &url);
    void saveSelected(const QUrl &url);
    void exportSelected(const QUrl &url);
    // The chooser was closed without picking anything.
    void cancelled();
    void failed(const QString &message);
};
