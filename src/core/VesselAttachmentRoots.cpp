#include "core/VesselAttachmentRoots.h"

#include "core/ManagedFileStore.h"
#include "core/Vessel.h"
#include "core/VesselRepository.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>

#include <optional>

const QString VesselAttachmentRoots::kManagedSubfolder = QStringLiteral("attachments");

namespace {

void setError(QString* out, const QString& text)
{
    if (out != nullptr) {
        *out = text;
    }
}

QString nativePath(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

// The message §2.2 asks for, in one place so that all four screens that can
// attach something say the same thing.
QString notConfiguredMessage()
{
    return QStringLiteral(
        "This vessel's attachment folder has not been set yet — choose one in Settings.");
}

// Asked by writing, not by reading a permission bit: a folder on a network
// share or inside a synchronised folder can look writable and refuse, and it
// is better to find that out here than half way through copying a survey pack.
bool isWritable(const QString& absoluteDir)
{
    QTemporaryFile probe(QDir(absoluteDir).filePath(QStringLiteral(".shipmgmt-XXXXXX")));
    return probe.open();
}

} // namespace

VesselAttachmentRoots::VesselAttachmentRoots(VesselRepository& vessels)
    : m_vessels(vessels)
{
}

bool VesselAttachmentRoots::storedRoot(const QString& vesselId,
                                       QString*       rootOut,
                                       QString*       errorString)
{
    rootOut->clear();

    if (vesselId.isEmpty()) {
        setError(errorString, QStringLiteral("No vessel was given."));
        return false;
    }

    std::optional<Vessel> vessel;
    if (!m_vessels.findById(vesselId, &vessel)) {
        setError(errorString, m_vessels.errorString());
        return false;
    }
    if (!vessel.has_value()) {
        // Also what a VESSEL installation gets for somebody else's vessel: the
        // scope filter lives in the repository, so another ship's folder is
        // not merely hidden from the screen, it cannot be reached from here
        // either (CLAUDE.md §3).
        setError(errorString, QStringLiteral("This vessel is no longer in the database."));
        return false;
    }

    *rootOut = vessel->attachmentRootPath;
    return true;
}

QString VesselAttachmentRoots::rootForWriting(const QString& vesselId, QString* errorString)
{
    setError(errorString, QString());

    // §2.4: ahead of any vessel lookup, exactly as it was before this
    // addendum. In the application this is always empty; a test sets it so the
    // whole attachment workflow runs inside a QTemporaryDir.
    const QString override = ManagedFileStore::rootPathOverride();
    if (!override.isEmpty()) {
        return override;
    }

    QString stored;
    if (!storedRoot(vesselId, &stored, errorString)) {
        return QString();
    }

    if (stored.isEmpty()) {
        setError(errorString, notConfiguredMessage());
        return QString();
    }

    return stored;
}

QString VesselAttachmentRoots::resolveForOpening(const QString& vesselId,
                                                 const QString& relativePath)
{
    if (relativePath.isEmpty()) {
        return QString(); // "nothing attached" must never resolve to a folder
    }

    // The vessel's own folder first, and only if the file is genuinely there.
    // Checking existence rather than assuming it is what makes the fallback
    // below safe to offer at all.
    QString stored;
    QString ignored;
    if (storedRoot(vesselId, &stored, &ignored) && !stored.isEmpty()) {
        const QString candidate = ManagedFileStore::resolveAbsolute(stored, relativePath);
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    // The pre-addendum location (§2.2). An attachment made before vessels had
    // folders of their own is here, and must still open.
    const QString legacy = ManagedFileStore::resolveAbsolute(ManagedFileStore::rootPath(),
                                                             relativePath);
    if (QFileInfo::exists(legacy)) {
        return legacy;
    }

    // Nowhere to be found. The vessel's own folder is the honest answer to
    // report, so whoever is told "could not open this" is told the place the
    // file is supposed to be rather than a historical one.
    if (!stored.isEmpty()) {
        return ManagedFileStore::resolveAbsolute(stored, relativePath);
    }
    return legacy;
}

bool VesselAttachmentRoots::changeRoot(const QString& vesselId,
                                       const QString& newRootPath,
                                       QString*       errorString)
{
    setError(errorString, QString());

    QString oldRoot;
    if (!storedRoot(vesselId, &oldRoot, errorString)) {
        return false;
    }

    if (newRootPath.trimmed().isEmpty()) {
        setError(errorString, QStringLiteral("No folder was chosen."));
        return false;
    }

    // Canonicalised before anything is compared or stored, so that the same
    // folder reached by two spellings is recognised as the same folder.
    const QFileInfo newInfo(newRootPath.trimmed());
    const QString   newRoot = newInfo.absoluteFilePath();

    // ---- Step 1: validate (§2.5) -------------------------------------------
    if (!newInfo.exists()) {
        setError(errorString, QStringLiteral("This folder does not exist:\n%1")
                                  .arg(nativePath(newRoot)));
        return false;
    }
    if (!newInfo.isDir()) {
        setError(errorString, QStringLiteral("This is a file, not a folder:\n%1")
                                  .arg(nativePath(newRoot)));
        return false;
    }

    // Choosing the folder the vessel is already using. Not an error, and
    // emphatically not a move: the existence check below would otherwise
    // refuse the vessel its own documents.
    if (!oldRoot.isEmpty() && QFileInfo(oldRoot).absoluteFilePath() == newRoot) {
        return true;
    }

    if (!isWritable(newRoot)) {
        setError(errorString, QStringLiteral("This folder cannot be written to:\n%1")
                                  .arg(nativePath(newRoot)));
        return false;
    }

    const QString newSubtree = QDir(newRoot).filePath(kManagedSubfolder);
    if (QFileInfo::exists(newSubtree)) {
        // Refused rather than merged. A folder already holding an
        // "attachments" folder almost certainly belongs to another vessel, and
        // quietly pooling two vessels' documents is not something that could
        // be untangled afterwards (§2.5).
        setError(errorString,
                 QStringLiteral("This folder already contains an \"%1\" folder, so it is "
                                "probably in use by another vessel:\n\n%2\n\nChoose a "
                                "different folder.")
                     .arg(kManagedSubfolder, nativePath(newRoot)));
        return false;
    }

    // ---- Step 2: move what is there (§2.5) ---------------------------------
    bool moved = false;
    if (!oldRoot.isEmpty()) {
        const QString oldSubtree = QDir(oldRoot).filePath(kManagedSubfolder);
        if (QFileInfo::exists(oldSubtree)) {
            if (!ManagedFileStore::moveTree(oldSubtree, newSubtree, errorString)) {
                // Nothing stored, nothing changed: the files are still wholly
                // under the folder the database still names.
                return false;
            }
            moved = true;
        }
    }

    // ---- Step 3: and only now, store it ------------------------------------
    if (!m_vessels.setAttachmentRootPath(vesselId, newRoot)) {
        const QString storeError = m_vessels.errorString();

        // The files have been moved but the database would still name the old
        // folder, which is precisely the disagreement §2.5 exists to prevent.
        // So they go back. This is the one path where a move is undone rather
        // than simply not attempted.
        if (moved) {
            QString moveBackError;
            const QString oldSubtree = QDir(oldRoot).filePath(kManagedSubfolder);
            if (!ManagedFileStore::moveTree(newSubtree, oldSubtree, &moveBackError)) {
                // Rare enough to be worth saying out loud rather than
                // summarising: the documents are at the new folder, and the
                // vessel is still recorded against the old one.
                setError(errorString,
                         QStringLiteral("The documents were moved to\n%1\nbut the change could "
                                        "not be recorded, and they could not be moved back.\n\n"
                                        "%2\n\n%3")
                             .arg(nativePath(newSubtree), storeError, moveBackError));
                return false;
            }
        }

        setError(errorString, storeError);
        return false;
    }

    return true;
}
