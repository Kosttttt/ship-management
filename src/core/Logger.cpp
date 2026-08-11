#include "core/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdlib>
#include <memory>

namespace {

// Rotate at 5 MB and keep five old files: roughly a year of normal operation
// on a vessel, with a hard ceiling of 30 MB so a logging loop can never fill
// the disk.
constexpr qint64 kMaxFileBytes   = 5LL * 1024 * 1024;
constexpr int    kMaxBackupFiles = 5;

QMutex                 g_mutex;
std::unique_ptr<QFile> g_logFile;
QString                g_logFilePath;

QString severityLabel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return QStringLiteral("DEBUG");
    case QtInfoMsg:     return QStringLiteral("INFO ");
    case QtWarningMsg:  return QStringLiteral("WARN ");
    case QtCriticalMsg: return QStringLiteral("ERROR");
    case QtFatalMsg:    return QStringLiteral("FATAL");
    }
    return QStringLiteral("?????");
}

// Caller must already hold g_mutex.
void rotateIfNeeded()
{
    if (!g_logFile || g_logFile->size() < kMaxFileBytes) {
        return;
    }

    g_logFile->close();

    QFile::remove(g_logFilePath + QStringLiteral(".%1").arg(kMaxBackupFiles));
    for (int i = kMaxBackupFiles - 1; i >= 1; --i) {
        const QString from = g_logFilePath + QStringLiteral(".%1").arg(i);
        const QString to   = g_logFilePath + QStringLiteral(".%1").arg(i + 1);
        if (QFile::exists(from)) {
            QFile::rename(from, to);
        }
    }
    QFile::rename(g_logFilePath, g_logFilePath + QStringLiteral(".1"));

    g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    // CLAUDE.md §6.2: timestamps are UTC. A vessel changes time zone weekly,
    // so a local-time log cannot be put in order after the fact.
    QString line = QStringLiteral("%1 [%2] %3")
                       .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                            severityLabel(type),
                            message);

    if (type != QtDebugMsg && type != QtInfoMsg && context.file != nullptr) {
        line += QStringLiteral("  (%1:%2)").arg(QString::fromUtf8(context.file)).arg(context.line);
    }

    QMutexLocker locker(&g_mutex);

    QTextStream console(stderr);
    console << line << Qt::endl;

    if (g_logFile && g_logFile->isOpen()) {
        QTextStream out(g_logFile.get());
        out << line << Qt::endl;
        out.flush();
        rotateIfNeeded();
    }

    // A custom handler must not return on a fatal message.
    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

bool Logger::initialise(const QString& logDirectory, QString* errorMessage)
{
    QMutexLocker locker(&g_mutex);

    QDir dir(logDirectory);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not create the log folder:\n%1")
                                .arg(QDir::toNativeSeparators(logDirectory));
        }
        return false;
    }

    const QString path = dir.filePath(QStringLiteral("ship-management.log"));

    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not open the log file:\n%1\n\n%2")
                                .arg(QDir::toNativeSeparators(path), file->errorString());
        }
        return false;
    }

    g_logFilePath = path;
    g_logFile     = std::move(file);

    qInstallMessageHandler(messageHandler);
    return true;
}

void Logger::shutdown()
{
    qInstallMessageHandler(nullptr);

    QMutexLocker locker(&g_mutex);
    if (g_logFile) {
        g_logFile->close();
        g_logFile.reset();
    }
}

QString Logger::logFilePath()
{
    QMutexLocker locker(&g_mutex);
    return g_logFilePath;
}
