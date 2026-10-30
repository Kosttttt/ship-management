#include "modules/certificates/ui/AttachmentChooser.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QStandardPaths>

namespace {

// Where the chooser opens. Documents rather than the last-used folder, which
// nothing stores yet, and rather than the managed data folder, which is the
// one place the user must never pick a source from — copying a file onto
// itself is the sort of thing that only ever goes wrong.
QString startingFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

} // namespace

QString AttachmentChooser::chooseFile(QWidget* parent)
{
    return QFileDialog::getOpenFileName(
        parent, QCoreApplication::translate("AttachmentChooser", "Choose the document"),
        startingFolder());
}

QString AttachmentChooser::chooseFolder(QWidget* parent)
{
    // ShowDirsOnly is the default, spelled out because the dialog is otherwise
    // easy to mistake for the file one.
    return QFileDialog::getExistingDirectory(
        parent, QCoreApplication::translate("AttachmentChooser", "Choose the folder of documents"),
        startingFolder(), QFileDialog::ShowDirsOnly);
}
