#pragma once

#include <QString>

// Routes every qDebug/qInfo/qWarning/qCritical/qFatal message to two places at
// once: the console (for the developer) and a rotating file (for a vessel that
// reports a problem weeks later, offline, with no developer present).
//
// This class is never instantiated. Qt's message handler is a plain function
// pointer, so the state has to be global to the process anyway; making that
// explicit is clearer than pretending an object owns it.
class Logger
{
public:
    // Creates logDirectory if needed and starts capturing messages.
    // Returns false and fills errorMessage if the folder or file is unusable.
    static bool initialise(const QString& logDirectory, QString* errorMessage = nullptr);

    // Restores Qt's default handler and closes the file.
    static void shutdown();

    // Full path of the current log file; empty before initialise() succeeds.
    static QString logFilePath();

    Logger() = delete;
};
