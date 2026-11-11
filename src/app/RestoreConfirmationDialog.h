#pragma once

#include "core/BackupFileInspector.h"

#include <QDialog>

// The "are you sure" for a restore (database-backup-spec §5.2).
//
// Its own class rather than a QMessageBox: §5.2 asks for the two databases
// side by side, which needs a real layout, and rather than another hundred
// lines inside DatabaseBackupSettings, which CLAUDE.md §9 would put over the
// guideline. The seam is a real one — everything here is about showing a
// comparison and getting a yes or no; nothing here writes anything.
//
// Shown only after BackupFileInspector::inspect() has passed, so both columns
// are real numbers read from real databases.
class RestoreConfirmationDialog : public QDialog
{
    Q_OBJECT

public:
    // `current` is this computer's database, `backup` the file chosen. The
    // path is shown so the person can see which file they picked.
    RestoreConfirmationDialog(const BackupSummary& current,
                              const BackupSummary& backup,
                              const QString&       backupFilePath,
                              QWidget*             parent = nullptr);

private:
    // "This backup is from the vessel V UNO; this computer will become that
    // vessel's installation." Empty when both sides are the same
    // installation, where there is nothing to warn about.
    static QString differenceNotice(const BackupSummary& current, const BackupSummary& backup);
};
