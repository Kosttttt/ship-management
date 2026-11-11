#pragma once

#include <QString>

#include <optional>

class QSqlDatabase;

// Who currently holds an IMO number, and what to say when it is taken.
//
// Split out of VesselRepository for its size (housekeeping-round-spec §2.1).
// It stays in the repository layer and holds SQL for the vessel table, which
// CLAUDE.md §9 allows for a helper used only by its own repository — and
// VesselRepository is the only thing that may use this one.
//
// It is a class rather than free functions because it needs the database
// connection, and keeping its own error string is what lets the SQL failure
// text reach the user unchanged.
class VesselImoNumberCheck
{
public:
    explicit VesselImoNumberCheck(QSqlDatabase& database);

    // Whichever vessel holds the number, archived or not.
    struct Holder {
        QString name;
        bool    isArchived = false;
    };

    // Deliberately does NOT filter on is_deleted: migrations/002 declares
    // imo_number UNIQUE with no such condition, so an archived vessel still
    // holds its number and the database will refuse to issue it again. A check
    // that disagreed with the constraint would wave the write through and let
    // it fail as raw SQLite text instead (vessel-archive-spec §10.1).
    //
    // Reports who holds it rather than a bare yes/no, so the caller can name
    // the archived vessel in the message and say what to do about it.
    //
    // excludeId leaves one row out — the row being saved, which must not
    // conflict with itself. Empty means exclude nothing.
    bool findHolder(const QString& imoNumber, const QString& excludeId,
                    std::optional<Holder>* holder);

    // The wording for a conflict. suggestRestore is false when editing: a
    // person correcting a vessel is not trying to add one, so pointing them at
    // Restore would be answering a question they did not ask.
    //
    // Static because it reads nothing but its arguments — the message is a
    // pure function of who holds the number and why we are asking.
    static QString conflictMessage(const Holder& holder, bool suggestRestore);

    QString errorString() const;

private:
    QSqlDatabase& m_database;
    QString       m_errorString;
};
