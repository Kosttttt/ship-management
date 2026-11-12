#pragma once

#include "core/InstallationContext.h"
#include "modules/documents/domain/Document.h"

#include <QString>

// Who may see and change which documents
// (document-management-spec.md §4.3).
//
//   * OFFICE sees and edits everything.
//   * VESSEL sees fleet-wide documents plus its own vessel's, and may add or
//     change only its own vessel's. Fleet-wide documents are read-only on a
//     ship — they arrive from the office, and step 4 of this module is what
//     will bring them.
//
// Its own class because it is the rule, not a detail of any one query: it is
// consulted by every write DocumentRepository makes and by both of its reads,
// and a rule spread through eight methods is a rule that will eventually be
// applied in seven of them. Used only by DocumentRepository, in the same
// layer, as CLAUDE.md §9 asks.
//
// No SQL and no database here — only the decision. What to do with the answer
// is the repository's business.
class DocumentAccessRules
{
public:
    explicit DocumentAccessRules(const InstallationContext& installation);

    // Whether this installation is pinned to one vessel.
    bool isScopedToOneVessel() const;

    // The vessel whose documents a call may touch: the caller's, at the
    // office, and this installation's own on a ship **whatever the caller
    // asked for**. That last part is the whole point — a screen cannot reach
    // another ship's paperwork by passing its id (CLAUDE.md §3).
    QString scopedVesselId(const QString& requested) const;

    // The vessel id to compare rows against, and whether to compare at all.
    // Together these are what a WHERE clause needs without having to test an
    // empty string — which would be wrong, because an empty QString binds as
    // SQL NULL and `NULL = ''` is unknown rather than true.
    QString scopeForQueries() const;

    // §4.3's write rule. `why` comes back with a sentence for the user,
    // written for somebody who does not know what "scope" means.
    bool mayWrite(const QString& vesselId, QString* why) const;

    // The fields a document cannot be saved without. Here rather than in the
    // repository because it is the same kind of question: what the module
    // will accept, as opposed to how it is stored.
    bool validate(const Document& document, QString* why) const;

private:
    // By value, never by reference — see InstallationContext.h.
    InstallationContext m_installation;
};
