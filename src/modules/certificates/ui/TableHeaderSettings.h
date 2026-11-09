#pragma once

#include <QObject>
#include <QString>

class QHeaderView;

// Remembers one table header's own layout between runs of the application:
// the column widths the user dragged, their order, and the column the table
// was left sorted on.
//
// This is a user-interface preference, not fleet data
// (certificate-list-usability-fixes.md §1), so it belongs in QSettings rather
// than in app_setting or anywhere else in the SQLite database. Nothing about
// "how wide I like the Name column" should replicate to the other side in a
// ship-to-shore sync, or mean anything at all on a different installation.
//
// QHeaderView::saveState()/restoreState() do the actual work; all this adds
// is where the bytes live and when they are written. A plain
// default-constructed QSettings already lands in the right per-user place,
// because main() sets the organisation and application names.
//
// It lives in this module's ui/ folder because this module is the only one
// with a table so far. There is nothing certificate-specific in it, so when a
// second module wants the same thing it should move somewhere shared rather
// than be copied - it cannot move to core/, which must stay usable without a
// window (CLAUDE.md §4) and links no widgets at all.
class TableHeaderSettings : public QObject
{
    Q_OBJECT

public:
    // key is the QSettings key the layout is stored under, so two tables can
    // each remember their own.
    TableHeaderSettings(QHeaderView* header, const QString& key, QObject* parent = nullptr);

    // Applies a saved layout over whatever defaults the caller has already
    // set, so a returning user's own sizing wins over the hardcoded widths
    // (§1). Returns false when there is nothing saved yet, which is not an
    // error - it is every user's first run.
    bool restore();

    // Begins saving on every change the user makes. Deliberately separate
    // from restore(), and called after it: connecting first would have
    // restoring fire the signals and write a half-applied layout straight
    // back.
    void startSaving();

    // §1: saved on every change rather than only on a clean shutdown, so a
    // force-quit does not lose today's sizing.
    void save();

    // Tests point this at a throwaway .ini file so they never write into the
    // developer's own settings - the same reason AttachmentFixture points
    // ManagedFileStore at a QTemporaryDir (CLAUDE.md §10.1). An empty path
    // clears it.
    static void setSettingsFileOverride(const QString& filePath);

private:
    QHeaderView* m_header = nullptr;
    QString      m_key;
};
