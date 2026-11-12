#include "modules/documents/domain/Document.h"

QString categoryToCode(DocumentCategory category)
{
    switch (category) {
    case DocumentCategory::Sms:
        return QStringLiteral("SMS");
    case DocumentCategory::Circular:
        return QStringLiteral("CIRCULAR");
    case DocumentCategory::PlanManual:
        return QStringLiteral("PLAN_MANUAL");
    case DocumentCategory::Report:
        return QStringLiteral("REPORT");
    case DocumentCategory::Unset:
        break;
    }
    // Unset has no code on purpose: migration 012's CHECK would refuse one,
    // and an empty string makes the repository's own validation refuse it
    // first, with a sentence a person can act on.
    return QString();
}

DocumentCategory categoryFromCode(const QString& code)
{
    if (code == QLatin1String("SMS")) {
        return DocumentCategory::Sms;
    }
    if (code == QLatin1String("CIRCULAR")) {
        return DocumentCategory::Circular;
    }
    if (code == QLatin1String("PLAN_MANUAL")) {
        return DocumentCategory::PlanManual;
    }
    if (code == QLatin1String("REPORT")) {
        return DocumentCategory::Report;
    }
    // Anything else — including a code from a newer version of the program
    // that this one does not know — reads back as Unset rather than guessing.
    return DocumentCategory::Unset;
}

QString DocumentVersion::displayLabel() const
{
    if (!revisionLabel.trimmed().isEmpty()) {
        return revisionLabel.trimmed();
    }
    // No label: the sequence is the only thing that always exists, and it is
    // what orders the history anyway.
    return QStringLiteral("v%1").arg(sequence);
}
