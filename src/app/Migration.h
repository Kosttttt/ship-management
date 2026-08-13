#pragma once

#include <QString>

// A migration contributed by a module, as required by IModule::migrations()
// (CLAUDE.md §7).
//
// Nothing produces one yet, deliberately. Restructuring MigrationRunner to
// discover migrations per module is not worth it for a single module
// (certificate-crud-spec §3): `migrations/` stays one flat, globally numbered
// folder, and new files are added to the CMake list the way 001, 002 and 003
// were. This type exists so the interface can be implemented as specified,
// and gains a purpose the day a module genuinely needs to ship its own.
struct Migration {
    int     version = 0;
    QString fileName;
    QString sql;
};
