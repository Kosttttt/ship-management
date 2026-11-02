#include "core/VesselAttachmentRoots.h"

#include "core/ManagedFileStore.h"
#include "core/Vessel.h"
#include "core/VesselRepository.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryFile>

#include <optional>

const QString VesselAttachmentRoots::kMarkerFileName =
    QStringLiteral(".ship-management-vessel");

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

// The vessel id a folder's marker file names, or an empty string when there is
// no marker — a genuinely fresh folder (§3.3).
QString markedVesselId(const QString& absoluteDir)
{
    QFile marker(QDir(absoluteDir).filePath(VesselAttachmentRoots::kMarkerFileName));
    if (!marker.exists() || !marker.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(marker.readAll()).trimmed();
}

// Gives up a folder's claim. Best-effort: a marker left in a folder the vessel
// has moved away from would make that folder look occupied to the next vessel
// offered it, which is a nuisance rather than a danger.
void removeMarker(const QString& absoluteDir)
{
    const QString path = QDir(absoluteDir).filePath(VesselAttachmentRoots::kMarkerFileName);
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        qWarning() << "Could not remove the vessel marker file at" << nativePath(path);
    }
}

// Claims a folder for one vessel. Called only from changeRoot(), and only
// after the move and the database write have both succeeded: a marker left in
// a folder the vessel was never actually pointed at would lock the next vessel
// out of it for no reason.
void writeMarker(const QString& absoluteDir, const QString& vesselId)
{
    const QString path = QDir(absoluteDir).filePath(VesselAttachmentRoots::kMarkerFileName);

    QFile marker(path);
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)
        || marker.write(vesselId.toUtf8()) < 0) {
        // Not a failure to report: the database is the record of which folder
        // belongs to which vessel, and this file only guards against pointing
        // a second vessel at it. Writing documents here has already been
        // proved possible by the writability probe, so reaching this is odd
        // enough to be worth a line in the log.
        qWarning() << "Could not write the vessel marker file at" << nativePath(path);
    }
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

QString VesselAttachmentRoots::rootHolding(const QString& vesselId, const QString& relativePath)
{
    if (relativePath.isEmpty()) {
        return QString(); // "nothing attached" must never resolve to a folder
    }

    // The vessel's own folder first, and only if the file is genuinely there.
    // Checking existence rather than assuming it is what makes the fallback
    // below safe to offer at all.
    QString stored;
    QString ignored;
    if (storedRoot(vesselId, &stored, &ignored) && !stored.isEmpty()
        && QFileInfo::exists(ManagedFileStore::resolveAbsolute(stored, relativePath))) {
        return stored;
    }

    // The pre-addendum location (§2.2). An attachment made before vessels had
    // folders of their own is here, and must still open.
    const QString legacy = ManagedFileStore::rootPath();
    if (QFileInfo::exists(ManagedFileStore::resolveAbsolute(legacy, relativePath))) {
        return legacy;
    }

    return QString();
}

QString VesselAttachmentRoots::resolveForOpening(const QString& vesselId,
                                                 const QString& relativePath)
{
    if (relativePath.isEmpty()) {
        return QString();
    }

    const QString holding = rootHolding(vesselId, relativePath);
    if (!holding.isEmpty()) {
        return ManagedFileStore::resolveAbsolute(holding, relativePath);
    }

    // Nowhere to be found. The vessel's own folder is the honest answer to
    // report, so whoever is told "could not open this" is told the place the
    // file is supposed to be rather than a historical one.
    QString stored;
    QString ignored;
    if (storedRoot(vesselId, &stored, &ignored) && !stored.isEmpty()) {
        return ManagedFileStore::resolveAbsolute(stored, relativePath);
    }
    return ManagedFileStore::resolveAbsolute(ManagedFileStore::rootPath(), relativePath);
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

    // ---- Step 1: validate (§2.5, §3.3) -------------------------------------
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
    // emphatically not a move: the marker check below would otherwise refuse
    // the vessel its own documents, and there is nowhere to move them to.
    if (!oldRoot.isEmpty() && QFileInfo(oldRoot).absoluteFilePath() == newRoot) {
        return true;
    }

    if (!isWritable(newRoot)) {
        setError(errorString, QStringLiteral("This folder cannot be written to:\n%1")
                                  .arg(nativePath(newRoot)));
        return false;
    }

    // §3.3: the marker file in place of the old "already contains an
    // attachments folder" test. A folder carrying another vessel's id is
    // refused rather than merged into — quietly pooling two vessels' documents
    // is not something that could be untangled afterwards. A folder carrying
    // this vessel's own id is one it used before and is free to use again.
    const QString marked = markedVesselId(newRoot);
    if (!marked.isEmpty() && marked != vesselId) {
        setError(errorString,
                 QStringLiteral("This folder is already in use by another vessel:\n\n%1\n\n"
                                "Choose a different folder.")
                     .arg(nativePath(newRoot)));
        return false;
    }

    // ---- Step 2: move what is there (§2.5 under §3.1's flat layout) --------
    //
    // The whole contents of the old folder, because the flattened layout puts
    // a vessel's documents directly in it — there is no longer a subtree to
    // single out. The folder was designated as this vessel's attachment
    // folder, so everything in it is this vessel's.
    //
    // The names are taken first and kept, because undoing this below must move
    // back exactly what moved across and not whatever else the new folder
    // happened to be holding already.
    //
    // The marker is not one of them. It is this class's own bookkeeping rather
    // than a document, and carrying it would collide with the marker already
    // sitting in a folder this vessel used before and is moving back into —
    // the one case §3.3 is explicit must be allowed. It is written and removed
    // below instead.
    QStringList carried;
    if (!oldRoot.isEmpty() && QFileInfo::exists(oldRoot)) {
        carried = ManagedFileStore::entriesIn(oldRoot);
        carried.removeAll(kMarkerFileName);

        if (!ManagedFileStore::moveNamedEntries(oldRoot, newRoot, carried, errorString)) {
            // Nothing stored, nothing changed: the files are still wholly
            // under the folder the database still names.
            return false;
        }
    }

    // ---- Step 3: and only now, store it ------------------------------------
    if (!m_vessels.setAttachmentRootPath(vesselId, newRoot)) {
        const QString storeError = m_vessels.errorString();

        // The files have been moved but the database would still name the old
        // folder, which is precisely the disagreement §2.5 exists to prevent.
        // So they go back. This is the one path where a move is undone rather
        // than simply not attempted.
        if (!carried.isEmpty()) {
            QString moveBackError;
            if (!ManagedFileStore::moveNamedEntries(newRoot, oldRoot, carried, &moveBackError)) {
                // Rare enough to be worth saying out loud rather than
                // summarising: the documents are at the new folder, and the
                // vessel is still recorded against the old one.
                setError(errorString,
                         QStringLiteral("The documents were moved to\n%1\nbut the change could "
                                        "not be recorded, and they could not be moved back.\n\n"
                                        "%2\n\n%3")
                             .arg(nativePath(newRoot), storeError, moveBackError));
                return false;
            }
        }

        setError(errorString, storeError);
        return false;
    }

    // ---- Step 4: claim the folder, release the old one (§3.3) --------------
    //
    // Last, so that a folder is only ever marked once it really is the one the
    // database names. Neither of these can fail in a way worth refusing the
    // change over: the database is the record of which folder belongs to which
    // vessel, and the marker only guards against a second vessel being pointed
    // at a folder in use.
    writeMarker(newRoot, vesselId);
    if (!oldRoot.isEmpty()) {
        removeMarker(QFileInfo(oldRoot).absoluteFilePath());
    }

    return true;
}
