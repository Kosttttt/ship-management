#include "modules/documents/domain/DocumentSpace.h"

QString kindToCode(SpaceKind kind)
{
    switch (kind) {
    case SpaceKind::Manual:
        return QStringLiteral("MANUAL");
    case SpaceKind::Collection:
        return QStringLiteral("COLLECTION");
    case SpaceKind::Unset:
        break;
    }
    // Unset has no code on purpose: migration 014's CHECK would refuse one,
    // and an empty string makes the repository's own validation refuse it
    // first, with a sentence a person can act on.
    return QString();
}

SpaceKind kindFromCode(const QString& code)
{
    if (code == QLatin1String("MANUAL")) {
        return SpaceKind::Manual;
    }
    if (code == QLatin1String("COLLECTION")) {
        return SpaceKind::Collection;
    }
    // Anything else — including a code from a newer version of the program
    // than this one — reads back as Unset rather than being guessed at.
    return SpaceKind::Unset;
}
