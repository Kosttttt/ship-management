#pragma once

#include <QString>

class QSqlDatabase;
class QWidget;

// The whole "restore from a backup" sequence, in one place
// (database-backup-spec §5, and §10.2 for why it is one place).
//
// Choose a file, run the §5.1 checks, show the §5.2 confirmation, copy the
// backup in beside the live database under its pending name, and start the
// program again. The swap itself happens at the next start, before anything
// opens the database — see RestoreSwap.
//
// Two screens offer this: the Settings section, and the first-run setup,
// where Settings cannot be reached yet (§10.2). They differ in exactly one
// thing — whether there is a database on this computer worth comparing the
// backup against — so they share this and nothing is duplicated. The checks
// in particular live in one place only: a second copy of them is the kind
// that eventually disagrees with the first.
namespace RestoreFromBackup {

enum class Outcome {
    // No file chosen, a check failed, or the user said no. Nothing was
    // written and the caller carries on as it was.
    NothingDone,

    // The backup is in place and the program must now end this session, so
    // that the next start performs the swap. Settings quits the application;
    // the wizard closes itself as cancelled, which makes main() exit.
    //
    // Returned whether or not the restart could be launched: the pending file
    // is there either way, so the restore is never lost — at worst it waits
    // for the person to start the program themselves, which is what the
    // message then says.
    Staged
};

// `showThisComputer` is false on first run, where there is nothing on this
// computer to put in the confirmation's left-hand column (§10.2).
//
// `dataFolder` is where the live database sits, passed in rather than looked
// up so that only main() ever names the real one (§6).
Outcome run(QWidget* parent, QSqlDatabase& connection, const QString& dataFolder,
            bool showThisComputer);

} // namespace RestoreFromBackup
