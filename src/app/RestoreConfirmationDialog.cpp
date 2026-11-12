#include "app/RestoreConfirmationDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// Local time, and spelled out rather than as digits-and-slashes: this is read
// once, under pressure, next to a number it is being compared against.
// Stored UTC, shown local (CLAUDE.md §6.2).
QString formatLastChange(const QDateTime& utc)
{
    if (!utc.isValid()) {
        return RestoreConfirmationDialog::tr("nothing recorded");
    }
    return utc.toLocalTime().toString(QStringLiteral("dd MMM yyyy HH:mm"));
}

QLabel* boldLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    QFont font  = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

} // namespace

QString RestoreConfirmationDialog::differenceNotice(const std::optional<BackupSummary>& current,
                                                    const BackupSummary&                backup)
{
    // First run: there is no installation yet, so there is nothing for this
    // backup to differ *from*. Whatever it says the installation is, is what
    // this computer becomes, which is the whole point of restoring here
    // (database-backup-spec §10.2).
    if (!current.has_value()) {
        return QString();
    }
    if (current->nodeId == backup.nodeId) {
        return QString();
    }

    // §5.2: said plainly, above the table. Restoring another installation's
    // backup is a legitimate thing to do — it is how a vessel's database is
    // moved to a new computer — but it changes what this copy *is*, which is
    // not something to discover afterwards.
    if (backup.isVesselMode && !backup.vesselName.isEmpty()) {
        return tr("This backup is from the vessel %1; this computer will become that vessel's "
                  "installation.")
            .arg(backup.vesselName);
    }
    return tr("This backup is from a different installation (%1); this computer will become "
              "that installation.")
        .arg(backup.nodeId);
}

RestoreConfirmationDialog::RestoreConfirmationDialog(const std::optional<BackupSummary>& current,
                                                     const BackupSummary&                backup,
                                                     const QString&                      backupFilePath,
                                                     QWidget*                            parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Restore this backup?"));
    setModal(true);
    setObjectName(QStringLiteral("restoreConfirmationDialog"));

    auto* file = new QLabel(QDir::toNativeSeparators(backupFilePath), this);
    file->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(boldLabel(tr("Restore this backup?"), this));
    layout->addWidget(file);

    const QString notice = differenceNotice(current, backup);
    if (!notice.isEmpty()) {
        auto* noticeLabel = new QLabel(notice, this);
        noticeLabel->setObjectName(QStringLiteral("restoreDifferenceNotice"));
        noticeLabel->setWordWrap(true);
        QFont noticeFont = noticeLabel->font();
        noticeFont.setBold(true);
        noticeLabel->setFont(noticeFont);
        layout->addSpacing(8);
        layout->addWidget(noticeLabel);
    }

    layout->addSpacing(12);

    // The side-by-side table §5.2 asks for. A grid rather than a form,
    // because a form has one value column and this needs two — or one, on
    // first run, where there is nothing on this computer yet (§10.2).
    auto* grid = new QGridLayout;

    if (current.has_value()) {
        grid->addWidget(boldLabel(tr("This computer now"), this), 0, 1);
    }
    grid->addWidget(boldLabel(tr("The backup"), this), 0, current.has_value() ? 2 : 1);

    // Built a row at a time so that the "this computer" cell is simply
    // absent rather than blank or zero: a 0 next to a real number reads as a
    // fact about an empty database, which is not what is being said here.
    const QList<QStringList> rows{
        {tr("Mode"), current.has_value() ? current->modeLabel() : QString(), backup.modeLabel()},
        {tr("Vessels"),
         current.has_value() ? QString::number(current->vesselCount) : QString(),
         QString::number(backup.vesselCount)},
        {tr("Certificates"),
         current.has_value() ? QString::number(current->certificateCount) : QString(),
         QString::number(backup.certificateCount)},
        {tr("Last change"),
         current.has_value() ? formatLastChange(current->lastChangeUtc) : QString(),
         formatLastChange(backup.lastChangeUtc)},
    };

    for (int row = 0; row < rows.size(); ++row) {
        const QStringList& cells = rows.at(row);
        grid->addWidget(new QLabel(cells.at(0), this), row + 1, 0);
        if (current.has_value()) {
            grid->addWidget(new QLabel(cells.at(1), this), row + 1, 1);
        }
        grid->addWidget(new QLabel(cells.at(2), this), row + 1,
                        current.has_value() ? 2 : 1);
    }
    grid->setColumnMinimumWidth(1, 140);
    if (current.has_value()) {
        grid->setColumnMinimumWidth(2, 140);
    }
    layout->addLayout(grid);

    layout->addSpacing(12);

    // On first run the database this replaces is the empty one the program
    // created a moment ago, so "everything will be replaced" would be a
    // small untruth and the safety copy is not worth mentioning.
    auto* consequences =
        new QLabel(current.has_value()
                       ? tr("Everything on this computer will be replaced by the backup.\n"
                            "The current database is kept as a safety copy.\n"
                            "The program will restart to finish.")
                       : tr("This computer will be set up from the backup.\n"
                            "The program will restart to finish."),
                   this);
    consequences->setWordWrap(true);
    layout->addWidget(consequences);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, this);
    buttons->button(QDialogButtonBox::Yes)->setText(tr("Restore"));

    // §5.2: No is the default, so Enter or a stray double-click cannot
    // replace a working database.
    QPushButton* no = buttons->button(QDialogButtonBox::No);
    no->setDefault(true);
    no->setFocus();

    layout->addSpacing(8);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
