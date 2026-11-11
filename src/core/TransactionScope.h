#pragma once

#include <QSqlDatabase>

// A database transaction that rolls itself back unless it is committed.
//
// Core, not any one module: "either all of this happened or none of it did"
// is not a certificates idea. Header-only because it is all of six lines of
// behaviour.
//
// It exists for the case where several repository calls have to succeed or
// fail together and no single repository owns all of them — CSV import being
// the first (certificate-import-export-spec §5.2), where one row writes a
// certificate through one repository and its surveys through another. A
// repository that owns every table it touches, like
// CertificateRepository::softDeleteWithChildren(), still handles its own
// transaction internally and needs nothing from here.
//
// Safe to wrap repository calls in only while none of them opens a
// transaction of its own: SQLite has no nested transactions.
class TransactionScope
{
public:
    explicit TransactionScope(QSqlDatabase& database)
        : m_database(database)
        , m_open(database.transaction())
    {
    }

    // The point of the class: every early return between here and commit()
    // rolls back, without each one having to remember to.
    ~TransactionScope()
    {
        if (m_open) {
            m_database.rollback();
        }
    }

    TransactionScope(const TransactionScope&)            = delete;
    TransactionScope& operator=(const TransactionScope&) = delete;

    // False when the transaction could not be started at all, which the
    // caller must treat as a failure rather than carrying on unprotected.
    bool isOpen() const { return m_open; }

    bool commit()
    {
        if (!m_open) {
            return false;
        }

        // The flag is cleared only once the commit has actually succeeded
        // (housekeeping-round-spec §6). Clearing it first left a failed commit
        // with the transaction still open on the connection and the destructor
        // no longer willing to roll it back — so the next statement on that
        // connection would have run inside it. Not seen in practice, because
        // the one caller treats a failed commit as a failed import and stops,
        // but the destructor is the whole point of this class and it has to
        // stay right whichever way commit() goes.
        if (!m_database.commit()) {
            return false;
        }
        m_open = false;
        return true;
    }

private:
    QSqlDatabase& m_database;
    bool          m_open = false;
};
