#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

// The three things every repository in this module writes into the audit and
// sync columns (CLAUDE.md §6.5), in one place instead of four copies.
//
// Header-only and inline: it is three expressions, and a .cpp for them would
// be a file to open for no reason. In the data layer because that is who
// uses it — nothing above the repositories knows these columns exist.
namespace DocumentSql {

// No user system exists yet (CLAUDE.md §11), so writes are attributed to
// SYSTEM — the precedent set by every other repository in this project.
inline QString systemUser()
{
    return QStringLiteral("SYSTEM");
}

inline QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); // CLAUDE.md §6.2
}

// Reads an audit timestamp back. Every one is written by nowUtc() above,
// whose ISO-8601 form ends in "Z", so this comes back as UTC without being
// told.
inline QDateTime fromUtcText(const QString& text)
{
    return QDateTime::fromString(text, Qt::ISODate);
}

// An optional field left blank is stored as NULL rather than as an empty
// string, so "not entered" and "deliberately blank" cannot drift apart.
inline QVariant nullableText(const QString& value)
{
    return value.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(value);
}

// "The children of this parent", where an empty parentId means the top of
// the tree and is stored as NULL.
//
// Written as a number rather than the obvious `(? = '' AND parent_id IS
// NULL)`, because **binding an empty QString sends SQL NULL**, and `NULL =
// ''` is *unknown* rather than true — so that condition matches nothing at
// all, silently. It is the same trap that cost this project a round in the
// certificates module and that VesselRepository carries a comment about; it
// cost this one the page order, where every top-level page quietly ended up
// at position 0 and moving a page up appeared to work and did nothing. An
// integer parameter cannot arrive as NULL, so the question is asked as one.
inline QString siblingCondition()
{
    return QStringLiteral("((? = 1 AND parent_id IS NULL) OR parent_id = ?)");
}

// Binds siblingCondition()'s two parameters, in order, so the pair can never
// be got the wrong way round in one place and not another.
inline void bindSiblingCondition(QSqlQuery& query, const QString& parentId)
{
    query.addBindValue(parentId.isEmpty() ? 1 : 0);
    query.addBindValue(parentId);
}

} // namespace DocumentSql
