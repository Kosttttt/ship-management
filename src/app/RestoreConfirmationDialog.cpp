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

QString RestoreConfirmationDialog::differenceNotice(const BackupSummary& current,
                                                    const BackupSummary& backup)
{
    if (current.nodeId == backup.nodeId) {
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

RestoreConfirmationDialog::RestoreConfirmationDialog(const BackupSummary& current,
                                                     const BackupSummary& backup,
                                                     const QString&       backupFilePath,
                                                     QWidget*             parent)
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
    // because a form has one value column and this needs two.
    auto* grid = new QGridLayout;
    grid->addWidget(boldLabel(tr("This computer now"), this), 0, 1);
    grid->addWidget(boldLabel(tr("The backup"), this), 0, 2);

    const QList<QStringList> rows{
        {tr("Mode"), current.modeLabel(), backup.modeLabel()},
        {tr("Vessels"), QString::number(current.vesselCount),
         QString::number(backup.vesselCount)},
        {tr("Certificates"), QString::number(current.certificateCount),
         QString::number(backup.certificateCount)},
        {tr("Last change"), formatLastChange(current.lastChangeUtc),
         formatLastChange(backup.lastChangeUtc)},
    };

    for (int row = 0; row < rows.size(); ++row) {
        const QStringList& cells = rows.at(row);
        for (int column = 0; column < cells.size(); ++column) {
            grid->addWidget(new QLabel(cells.at(column), this), row + 1, column);
        }
    }
    grid->setColumnMinimumWidth(1, 140);
    grid->setColumnMinimumWidth(2, 140);
    layout->addLayout(grid);

    layout->addSpacing(12);

    auto* consequences =
        new QLabel(tr("Everything on this computer will be replaced by the backup.\n"
                      "The current database is kept as a safety copy.\n"
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
