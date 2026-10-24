#pragma once

#include <QStringList>

// Commands that need no display, for scripts and agents: cyto info, check,
// stats, and skill. The window opens with cyto open.
bool isCliCommand(const QString &word);
QString cliSummary();
int runCli(const QStringList &arguments);
