#pragma once

#include "core/InstallationContext.h"
#include "core/Vessel.h"

#include <QList>
#include <QString>

#include <optional>

class QSqlDatabase;

// Reads and writes the vessel table. The only file containing SQL for it
// (CLAUDE.md §4 rule 2).
//
// This is the first consumer of InstallationContext::vesselScope(). The
// VESSEL-mode filter is applied here, on every query, in one place — never in
// a screen (CLAUDE.md §3). A screen that forgot would leak another vessel's
// data to a crew member.
class VesselRepository
{
public:
    VesselRepository(QSqlDatabase& database, const InstallationContext& installation);

    // Both reads apply the scope filter and, by default, exclude archived
    // rows — so every caller that existed before archiving did keeps its
    // exact behaviour without touching a line of its own code
    // (vessel-archive-spec §6). Only the vessel list's "Show archived
    // vessels" toggle, and archive()/restore()'s own pre-read, pass true.
    bool list(QList<Vessel>* vessels, bool includeArchived = false);
    bool findById(const QString& id, std::optional<Vessel>* vessel,
                  bool includeArchived = false);

    // OFFICE only: a VESSEL installation manages one ship and never adds
    // another. Fills newId with the generated UUID on success.
    bool create(const Vessel& vessel, QString* newId = nullptr);

    // Bumps revision and refreshes updated_at/updated_by, leaving created_at
    // and created_by as they were (vessel-crud-spec §6).
    //
    // Deliberately does NOT write attachment_root_path, even though the column
    // is read into Vessel. VesselEditForm::vessel() builds a fresh Vessel out
    // of its own fields, so a path included here would be silently blanked
    // every time somebody corrected a call sign. The one place that path is
    // written is setAttachmentRootPath() below.
    bool update(const Vessel& vessel);

    // certificate-attachment-addendum-spec §2.3/§2.5: one column, on its own.
    //
    // Separate from update() because of the order §2.5 insists on — the files
    // are moved first, and this is called only once that has actually
    // succeeded. An empty path stores SQL NULL, which reads back as "not
    // configured".
    //
    // This does not move anything itself. VesselAttachmentRoots::changeRoot()
    // is what sequences the move and this write; calling this directly would
    // leave the database claiming a folder the files are not in.
    bool setAttachmentRootPath(const QString& vesselId, const QString& absolutePath);

    // Archive / Restore (vessel-archive-spec §3, §6). OFFICE only, refused
    // outright for a VESSEL installation — including against its own vessel,
    // because this is fleet management, not work for the crew on board. The
    // same bare isScopedToOneVessel() check create() uses, deliberately not
    // update()'s looser "scoped and not my own id" form.
    //
    // Both read the row first rather than inferring failure from
    // numRowsAffected(), the way CertificateRepository::softDeleteWithChildren()
    // already does. update() can collapse its zero-row case into one "no
    // longer exists" message because it only has one; these each have two —
    // an id that isn't there at all, and a vessel already in the state being
    // asked for — and one message would be wrong half the time.
    //
    // archive() sets is_deleted = 1; restore() sets it back to 0, after
    // re-running the same IMO-number uniqueness check create() and update()
    // use, because archiving frees that number for reuse (see §6). Both bump
    // revision and refresh updated_at/updated_by exactly as update() does.
    bool archive(const QString& vesselId);
    bool restore(const QString& vesselId);

    QString errorString() const;

private:
    bool validate(const Vessel& vessel);
    // Whichever vessel currently holds an IMO number, archived or not.
    struct ImoNumberHolder {
        QString name;
        bool    isArchived = false;
    };

    // Deliberately does NOT filter on is_deleted: migrations/002 declares
    // imo_number UNIQUE with no such condition, so an archived vessel still
    // holds its number and the database will refuse to issue it again. A
    // check that disagreed with the constraint would wave the write through
    // and let it fail as raw SQLite text instead (vessel-archive-spec §10.1).
    //
    // Reports who holds it rather than a bare yes/no, so the caller can name
    // the archived vessel in the message and say what to do about it.
    bool findImoNumberHolder(const QString& imoNumber, const QString& excludeId,
                             std::optional<ImoNumberHolder>* holder);

    // The wording for a conflict. suggestRestore is false when editing: a
    // person correcting a vessel is not trying to add one, so pointing them
    // at Restore would be answering a question they did not ask.
    static QString imoNumberConflictMessage(const ImoNumberHolder& holder,
                                            bool                   suggestRestore);

    // archive() and restore() are the same operation in opposite directions,
    // so the parts that do not differ live here once (vessel-archive-spec
    // §10.2): the empty-id guard, the OFFICE-only refusal, the pre-read, and
    // the "is it already in that state" check. Every message is derived from
    // wantArchived rather than passed in, so the two callers cannot drift
    // apart in their wording.
    //
    // They are two calls rather than one because restore() has a step in
    // between — the IMO backstop — which archive() does not.
    bool checkArchiveChangeAllowed(const QString& vesselId, bool wantArchived,
                                   std::optional<Vessel>* existing);
    bool writeArchivedFlag(const QString& vesselId, bool archived);
    bool isScopedToOneVessel() const;

    QSqlDatabase&       m_database;
    // By value, never by reference — see InstallationContext.h.
    InstallationContext m_installation;
    QString             m_errorString;
};
