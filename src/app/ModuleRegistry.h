#pragma once

#include <QList>

#include <memory>
#include <vector>

class IModule;
class InstallationContext;
class QSqlDatabase;

// The fixed, compile-time list of feature modules (CLAUDE.md §7).
//
// Every module is constructed with the same two things — the database
// connection and the installation context — so registering another one is a
// single line in the constructor rather than a new parameter here. Each module
// owns its own repositories from those.
class ModuleRegistry
{
public:
    ModuleRegistry(QSqlDatabase& database, const InstallationContext& installation);
    ~ModuleRegistry();

    // In sidebar order. The registry keeps ownership.
    QList<IModule*> modules() const;

private:
    ModuleRegistry(const ModuleRegistry&)            = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    std::vector<std::unique_ptr<IModule>> m_modules;
};
