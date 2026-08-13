#include "app/ModuleRegistry.h"

#include "app/IModule.h"
#include "modules/certificates/CertificatesModule.h"

ModuleRegistry::ModuleRegistry(QSqlDatabase& database, const InstallationContext& installation)
{
    // Registering a module is one line. This is the only place in the
    // application that names a module type.
    m_modules.push_back(std::make_unique<CertificatesModule>(database, installation));
}

// Declared out of line because the header only forward-declares IModule, and
// unique_ptr needs the complete type to destroy it.
ModuleRegistry::~ModuleRegistry() = default;

QList<IModule*> ModuleRegistry::modules() const
{
    QList<IModule*> result;
    result.reserve(static_cast<int>(m_modules.size()));
    for (const std::unique_ptr<IModule>& module : m_modules) {
        result.append(module.get());
    }
    return result;
}
