#include "core/RestoreSwap.h"

#include "core/Database.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

const QString kPendingName = QStringLiteral("ship-management.restore-pending.db");
const QString kSafetyStem  = QStringLiteral("ship-management.before-restore-");

// SQLite's side files. They belong to the database they sit beside, and
// leaving one next to a *different* database is how a good file gets ruined,
// so they travel with the old database rather than being deleted.
const QStringList& sideCarSuffixes()
{
    static const QStringList suffixes{QStringLiteral("-journal"), QStringLiteral("-wal"),
                                      QStringLiteral("-shm")};
    return suffixes;
}

// What §10.1 says to tell the user. The restore is cancelled rather than
// carried out with a side file thrown away, so the reassurance comes first
// and the thing to do about it second.
const QString kSideFileInUseMessage = QStringLiteral(
    "The restore has not happened. Your data is untouched.\n\nA file next to the database is "
    "in use — close any program that might be using it and start again.");

// Puts back whatever has already been moved aside: the side files first, so
// the database is whole again before it carries the name the program will
// open, and then the database itself.
//
// Returns whether the database is back under its own name. Nothing is
// deleted here and nothing is reported — every caller is already on a failure
// path and says its own piece.
bool undoMoves(const QString& live, const QString& safety, const QStringList& movedSideCars)
{
    for (const QString& suffix : movedSideCars) {
        QFile::rename(safety + suffix, live + suffix);
    }
    return QFile::rename(safety, live);
}

// When even putting the database back fails, which needs something outside
// the program holding the files. Says where the database actually is rather
// than pretending it is where it should be.
QString couldNotPutBackMessage(const QString& safety, const QString& liveName)
{
    return QStringLiteral("The restore failed and the database could not be put back. It is "
                          "now at:\n%1\n\nRename it to %2 before starting again.")
        .arg(QDir::toNativeSeparators(safety), liveName);
}

} // namespace

QString RestoreSwap::liveFileName()
{
    // Taken from the one place the database's name is decided, rather than
    // spelled out a second time here: two literals that have to match are two
    // literals that will eventually not.
    return QFileInfo(Database::defaultFilePath()).fileName();
}

QString RestoreSwap::pendingFileName()
{
    return kPendingName;
}

QString RestoreSwap::liveFilePath(const QString& dataFolder)
{
    return QDir(dataFolder).filePath(liveFileName());
}

QString RestoreSwap::pendingFilePath(const QString& dataFolder)
{
    return QDir(dataFolder).filePath(kPendingName);
}

bool RestoreSwap::stage(const QString& dataFolder, const QString& backupFilePath,
                        QString* errorString)
{
    errorString->clear();

    if (!QDir(dataFolder).exists() && !QDir().mkpath(dataFolder)) {
        *errorString = QStringLiteral("The program's data folder could not be created:\n%1")
                           .arg(QDir::toNativeSeparators(dataFolder));
        return false;
    }

    const QString pending   = pendingFilePath(dataFolder);
    const QString temporary = pending + QStringLiteral(".tmp");

    // A leftover from an interrupted attempt. Our own file, and nothing else
    // ever carries this name.
    if (QFile::exists(temporary) && !QFile::remove(temporary)) {
        *errorString = QStringLiteral("An unfinished restore is in the way and could not be "
                                      "removed:\n%1")
                           .arg(QDir::toNativeSeparators(temporary));
        return false;
    }
    if (QFile::exists(pending) && !QFile::remove(pending)) {
        *errorString = QStringLiteral("An earlier pending restore could not be replaced:\n%1")
                           .arg(QDir::toNativeSeparators(pending));
        return false;
    }

    if (!QFile::copy(backupFilePath, temporary)) {
        *errorString = QStringLiteral("The backup could not be copied into the program's data "
                                      "folder:\n%1")
                           .arg(QDir::toNativeSeparators(pending));
        return false;
    }

    // Renamed only once the copy is whole, so a restore interrupted halfway
    // through leaves a .tmp that the next attempt clears rather than a short
    // file that the next *start* would treat as a database.
    if (!QFile::rename(temporary, pending)) {
        QFile::remove(temporary);
        *errorString = QStringLiteral("The copied backup could not be put in place:\n%1")
                           .arg(QDir::toNativeSeparators(pending));
        return false;
    }

    return true;
}

RestoreSwap::Outcome RestoreSwap::applyPending(const QString& dataFolder, QString* detail)
{
    detail->clear();

    const QString pending = pendingFilePath(dataFolder);
    if (!QFile::exists(pending)) {
        return Outcome::NothingPending;
    }

    const QString live = liveFilePath(dataFolder);

    // A fresh installation that has never opened a database: there is nothing
    // to keep, so the pending file simply becomes the database.
    if (!QFile::exists(live)) {
        if (!QFile::rename(pending, live)) {
            *detail = QStringLiteral("The restored database could not be put in place at %1.")
                          .arg(QDir::toNativeSeparators(live));
            return Outcome::Failed;
        }
        *detail = QStringLiteral("Restored from backup. There was no previous database to keep.");
        return Outcome::Restored;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"));
    const QString safetyStem = QDir(dataFolder).filePath(kSafetyStem + stamp);
    const QString safety     = safetyStem + QStringLiteral(".db");

    if (!QFile::rename(live, safety)) {
        // Nothing has moved, so the folder is exactly as it was and the
        // program can open its own database as usual. The pending file stays
        // where it is: the restore has not happened, and a later start can
        // try again.
        *detail = QStringLiteral(
                      "The current database could not be set aside, so the restore has not "
                      "happened. Your data is untouched.\n\nCould not rename:\n%1")
                      .arg(QDir::toNativeSeparators(live));
        return Outcome::Failed;
    }

    // Moved after the main file, and each one separately: a missing side file
    // is the normal case, not a failure.
    QStringList movedSideCars;
    for (const QString& suffix : sideCarSuffixes()) {
        const QString from = live + suffix;
        if (!QFile::exists(from)) {
            continue;
        }
        if (QFile::rename(from, safety + suffix)) {
            movedSideCars.append(suffix);
            continue;
        }

        // §10.1: it is **not** deleted, and the restore is cancelled instead.
        //
        // An earlier version removed it, on the reasoning that its contents
        // were already in the safety copy because the database had been
        // closed cleanly. That reasoning was wrong about when these files
        // exist at all: a -journal or -wal beside the database means the last
        // session did *not* end cleanly — a crash, a power cut — and the
        // journal is what holds the information needed to repair the
        // database. Throwing it away can leave the safety copy damaged, which
        // is the one outcome this whole feature exists to prevent.
        //
        // So everything already moved goes back, the pending file is left
        // where it is, and the start carries on with the original database. A
        // restore that has not happened is always safe.
        qWarning() << "Could not move" << from << "aside; cancelling the restore.";

        if (!undoMoves(live, safety, movedSideCars)) {
            *detail = couldNotPutBackMessage(safety, liveFileName());
            return Outcome::Failed;
        }
        *detail = kSideFileInUseMessage;
        return Outcome::Failed;
    }

    if (!QFile::rename(pending, live)) {
        // The same undo the side-file failure above performs, for the same
        // reason: whatever has been moved goes back, and nothing is deleted.
        if (!undoMoves(live, safety, movedSideCars)) {
            *detail = couldNotPutBackMessage(safety, liveFileName());
            return Outcome::Failed;
        }
        *detail = QStringLiteral("The restore failed, so the database has been put back as it "
                                 "was. Your data is untouched.");
        return Outcome::Failed;
    }

    *detail = QStringLiteral("Restored from backup. The previous database is kept as:\n%1")
                  .arg(QDir::toNativeSeparators(safety));
    return Outcome::Restored;
}
