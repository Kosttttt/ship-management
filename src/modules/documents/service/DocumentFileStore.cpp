#include "modules/documents/service/DocumentFileStore.h"

#include "core/ManagedFileStore.h"
#include "core/Vessel.h"
#include "core/VesselRepository.h"

#include <QFileInfo>

#include <optional>

namespace {

const QString kFleetFolder = QStringLiteral("Fleet");

// A name that is safe on every filesystem this project targets
// (CLAUDE.md §2: Windows, macOS and Linux from one codebase).
//
// Windows refuses \ / : * ? " < > | outright; the others are more permissive,
// but a file named differently on two machines is worse than one named
// plainly on all three, and these files are copied between computers.
QString forFileName(const QString& text)
{
    static const QString forbidden = QStringLiteral("\\/:*?\"<>|");

    QString safe;
    safe.reserve(text.size());
    for (const QChar character : text) {
        safe.append(forbidden.contains(character) || character < QChar(' ') ? QLatin1Char('_')
                                                                           : character);
    }
    // A trailing dot or space is legal to create on Windows but not to open
    // again, which is the sort of thing that only shows up on a ship.
    while (safe.endsWith(QLatin1Char('.')) || safe.endsWith(QLatin1Char(' '))) {
        safe.chop(1);
    }
    return safe.trimmed();
}

} // namespace

DocumentFileStore::DocumentFileStore(VesselRepository& vessels)
    : m_vessels(vessels)
{
}

QString DocumentFileStore::relativeFolderFor(const Document& document, QString* errorString)
{
    errorString->clear();

    const QString category = categoryToCode(document.category);
    if (category.isEmpty()) {
        *errorString = QStringLiteral("This document has no category, so there is nowhere to "
                                      "file it.");
        return QString();
    }

    if (document.isFleetWide()) {
        return kFleetFolder + QLatin1Char('/') + category;
    }

    std::optional<Vessel> vessel;
    if (!m_vessels.findById(document.vesselId, &vessel)) {
        *errorString = m_vessels.errorString();
        return QString();
    }
    if (!vessel.has_value()) {
        *errorString = QStringLiteral("This document's vessel is no longer in the database.");
        return QString();
    }
    if (vessel->imoNumber.trimmed().isEmpty()) {
        // Refused rather than filed under an empty name, which would collect
        // every such vessel's documents into one folder.
        *errorString = QStringLiteral("Vessel '%1' has no IMO number, so its documents have "
                                      "nowhere to go.")
                           .arg(vessel->name);
        return QString();
    }

    return forFileName(vessel->imoNumber.trimmed()) + QLatin1Char('/') + category;
}

QString DocumentFileStore::fileNameFor(const Document& document, int sequence,
                                       const QString& sourcePath)
{
    // The reference is what people call the document — "SMS-03" — so it comes
    // first. A document without one falls back to its title, which is
    // required and so always there.
    QString stem = forFileName(document.reference.trimmed());
    if (stem.isEmpty()) {
        stem = forFileName(document.title.trimmed());
    }
    if (stem.isEmpty()) {
        stem = QStringLiteral("document");
    }

    // The original extension is kept so the computer's normal viewer still
    // knows what the file is — which is how every uploaded document is opened
    // (§1 of the spec).
    const QString suffix = QFileInfo(sourcePath).completeSuffix();

    const QString name = QStringLiteral("%1__v%2").arg(stem).arg(sequence);
    return suffix.isEmpty() ? name : name + QLatin1Char('.') + suffix;
}

bool DocumentFileStore::store(const QString& documentsRoot, const Document& document, int sequence,
                              const QString& sourcePath, QString* relativePathOut,
                              QString* errorString)
{
    relativePathOut->clear();
    errorString->clear();

    if (documentsRoot.isEmpty()) {
        *errorString = QStringLiteral(
            "No documents folder has been chosen yet. Choose one in Settings first.");
        return false;
    }

    const QString folder = relativeFolderFor(document, errorString);
    if (folder.isEmpty()) {
        return false;
    }

    // ManagedFileStore adds a numeric suffix if something is already at that
    // exact name, so even a sequence somehow reused cannot overwrite a file
    // that is already there. Nothing in this module overwrites anything.
    return ManagedFileStore::copyInto(documentsRoot, sourcePath, folder,
                                      fileNameFor(document, sequence, sourcePath),
                                      relativePathOut, errorString);
}

QString DocumentFileStore::absolutePathFor(const QString& documentsRoot,
                                           const QString& relativePath)
{
    return ManagedFileStore::resolveAbsolute(documentsRoot, relativePath);
}
