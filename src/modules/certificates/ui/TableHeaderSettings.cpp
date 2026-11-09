#include "modules/certificates/ui/TableHeaderSettings.h"

#include <QHeaderView>
#include <QSettings>

#include <memory>

namespace {

// Set only by tests. Process-wide, like ManagedFileStore's own override, and
// for the same reason: there is no other way to keep a test out of the real
// per-user settings.
QString g_settingsFileOverride;

std::unique_ptr<QSettings> openSettings()
{
    if (!g_settingsFileOverride.isEmpty()) {
        return std::make_unique<QSettings>(g_settingsFileOverride, QSettings::IniFormat);
    }
    // Lands under the organisation and application names main() sets.
    return std::make_unique<QSettings>();
}

} // namespace

void TableHeaderSettings::setSettingsFileOverride(const QString& filePath)
{
    g_settingsFileOverride = filePath;
}

TableHeaderSettings::TableHeaderSettings(QHeaderView* header, const QString& key, QObject* parent)
    : QObject(parent)
    , m_header(header)
    , m_key(key)
{
}

bool TableHeaderSettings::restore()
{
    if (m_header == nullptr) {
        return false;
    }

    const std::unique_ptr<QSettings> settings = openSettings();
    const QByteArray state = settings->value(m_key).toByteArray();
    if (state.isEmpty()) {
        return false;
    }
    return m_header->restoreState(state);
}

void TableHeaderSettings::startSaving()
{
    if (m_header == nullptr) {
        return;
    }

    // sectionResized is the one that matters - dragging a border is what this
    // whole class exists for. The other two are connected because
    // saveState() captures order and the sort indicator as well, so without
    // them a reorder or a re-sort would only be remembered if the user also
    // happened to resize something afterwards.
    connect(m_header, &QHeaderView::sectionResized, this,
            [this](int, int, int) { save(); });
    connect(m_header, &QHeaderView::sectionMoved, this, [this](int, int, int) { save(); });
    connect(m_header, &QHeaderView::sortIndicatorChanged, this, [this](int, Qt::SortOrder) {
        save();
    });
}

void TableHeaderSettings::save()
{
    if (m_header == nullptr) {
        return;
    }

    const std::unique_ptr<QSettings> settings = openSettings();
    settings->setValue(m_key, m_header->saveState());
}
