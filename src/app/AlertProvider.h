#pragma once

#include <QList>
#include <QString>

// One vessel's count from one module's AlertProvider (alerts-spec.md §3).
struct VesselAttentionCount {
    QString vesselId;
    QString vesselName;
    int     count = 0;
};

// What a module reports for the sidebar badge and the daily banner. A module
// owns what "needing attention" means for its own domain — MainWindow only
// ever asks "how many, for which vessels."
//
// Forward-declared in app/IModule.h since step 5; implemented for the first
// time here.
class AlertProvider
{
public:
    virtual ~AlertProvider() = default;

    // Only vessels with count > 0 belong in the result. Fleet-wide in OFFICE
    // mode, this installation's one vessel in VESSEL mode — never scoped to
    // whatever a toolbar selector happens to show right now.
    virtual QList<VesselAttentionCount> attentionByVessel() const = 0;
};
