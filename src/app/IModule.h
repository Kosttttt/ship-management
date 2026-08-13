#pragma once

#include "app/Migration.h"

#include <QIcon>
#include <QList>
#include <QString>

class QWidget;

// Feeds the sidebar alert badge from step 8. Only ever held as a pointer here,
// so a forward declaration is enough — the interface itself belongs to the
// step that first has something to report, not to this one.
class AlertProvider;

// A feature module, exactly as specified in CLAUDE.md §7. Registered at
// compile time in ModuleRegistry; there is no dynamic plugin loading.
//
// Adding a module must require zero changes to existing modules.
class IModule
{
public:
    virtual ~IModule() = default;

    virtual QString id() const          = 0;   // "certificates"
    virtual QString displayName() const = 0;   // shown in the sidebar
    virtual QIcon   icon() const        = 0;

    // The module's screen. The returned widget is owned by `parent`, through
    // Qt's usual parent-child ownership.
    virtual QWidget* createMainWidget(QWidget* parent) = 0;

    virtual QList<Migration>      migrations() const = 0;
    virtual QList<AlertProvider*> alertProviders()   = 0;
};
