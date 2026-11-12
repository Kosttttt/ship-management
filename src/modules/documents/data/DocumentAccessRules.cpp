#include "modules/documents/data/DocumentAccessRules.h"

DocumentAccessRules::DocumentAccessRules(const InstallationContext& installation)
    : m_installation(installation)
{
}

bool DocumentAccessRules::isScopedToOneVessel() const
{
    return m_installation.isConfigured() && m_installation.mode() == InstallationMode::Vessel;
}

QString DocumentAccessRules::scopedVesselId(const QString& requested) const
{
    return isScopedToOneVessel() ? m_installation.vesselScope() : requested;
}

QString DocumentAccessRules::scopeForQueries() const
{
    return isScopedToOneVessel() ? m_installation.vesselScope() : QString();
}

bool DocumentAccessRules::mayWrite(const QString& vesselId, QString* why) const
{
    if (!isScopedToOneVessel()) {
        return true; // the office edits everything
    }

    if (vesselId.isEmpty()) {
        // Fleet-wide. Worded as what to do about it rather than as a refusal:
        // the person reading it has done nothing wrong, and the document will
        // arrive from the office once step 4 of this module exists.
        *why = QStringLiteral(
            "Fleet-wide documents are managed at the office and cannot be changed here.");
        return false;
    }
    if (vesselId != m_installation.vesselScope()) {
        *why = QStringLiteral("This installation may only change its own vessel's documents.");
        return false;
    }
    return true;
}

bool DocumentAccessRules::validate(const Document& document, QString* why) const
{
    if (document.title.trimmed().isEmpty()) {
        *why = QStringLiteral("A document title is required.");
        return false;
    }
    if (categoryToCode(document.category).isEmpty()) {
        // Unset, which migration 012's CHECK would refuse anyway — but with
        // a message about a constraint rather than about a form.
        *why = QStringLiteral("A category must be chosen.");
        return false;
    }
    return true;
}
