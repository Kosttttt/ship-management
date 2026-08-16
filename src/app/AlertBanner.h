#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class IModule;
class QVBoxLayout;

// alerts-spec.md §7. One row per (vessel, module) pair with something
// outstanding. A startup snapshot: populate() is called once, and the banner
// does not re-poll after that — see the module's own AlertProvider for what
// "outstanding" means.
struct AlertBannerEntry {
    IModule* module = nullptr;
    QString  vesselId;
    QString  vesselName;
    int      count = 0;
};

class AlertBanner : public QWidget
{
    Q_OBJECT

public:
    explicit AlertBanner(QWidget* parent = nullptr);

    // Rebuilds the row list from scratch and makes the banner visible.
    // Calling this with an empty list is a no-op — MainWindow only calls it
    // when there is something to show.
    void populate(const QList<AlertBannerEntry>& entries);

signals:
    void viewRequested(IModule* module, const QString& vesselId);
    void dismissed();

private:
    void clearRows();

    QVBoxLayout* m_rowsLayout = nullptr;
};
