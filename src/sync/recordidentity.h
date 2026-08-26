#pragma once

#include <QDateTime>
#include <QString>

namespace Kalburator::Sync {

/// Detached-exception identity (vtodo-parity VP.c-step-1a).
///
/// Blob-pipeline composite record id for exception records:
///   uid + '\x01' + recurrenceId.toUTC().toString(Qt::ISODate)
/// Masters stay bare uid.
///
/// Separator precedent: GenericSqliteBackend::encodeRecordId already uses
/// '\x01' as its record-id component separator. '\x00' (the incidence-path
/// twin choice in syncdiff/syncengine) is deliberately NOT reused here —
/// an embedded NUL is a SQL/log/filesystem hazard; '\x01' cannot appear in
/// a legal RFC 5545 UID or ISO-8601 timestamp.
///
/// Incidence-path twins (syncdiff.cpp / syncengine.cpp) are intentionally
/// untouched at this stage; backend wiring is a later stage.

inline constexpr char16_t kRecordIdentitySeparator = u'\x01';

/// Result of decomposeRecordIdentity(). For a bare master id, `recurrenceId`
/// is invalid. For malformed input BOTH fields come back empty/invalid —
/// fail-loud-ish: callers can detect and reject without crashing.
struct DecomposedRecordIdentity {
    QString uid;
    QDateTime recurrenceId;
};

/// Compose the blob-pipeline record identity. An invalid `recurrenceId`
/// composes the bare master uid (passthrough). The timestamp half is always
/// normalized to UTC ISO-8601, so the same instant expressed in different
/// timezones composes the same identity.
inline QString composeRecordIdentity(const QString &uid,
                                     const QDateTime &recurrenceId)
{
    if (!recurrenceId.isValid())
        return uid;
    return uid + QChar(kRecordIdentitySeparator)
               + recurrenceId.toUTC().toString(Qt::ISODate);
}

/// Decompose a record identity. Bare uid (no separator) passes through with
/// an invalid recurrenceId. Malformed input — empty uid half, or a
/// non-parsable timestamp half — returns an empty uid and an invalid
/// recurrenceId rather than crashing or silently masquerading as a master.
inline DecomposedRecordIdentity decomposeRecordIdentity(const QString &id)
{
    const qsizetype sep = id.indexOf(QChar(kRecordIdentitySeparator));
    if (sep < 0)
        return { id, QDateTime{} };   // bare master uid
    const QString uid = id.left(sep);
    if (uid.isEmpty())
        return { QString(), QDateTime{} };   // malformed: empty uid half
    const QDateTime recId =
        QDateTime::fromString(id.mid(sep + 1), Qt::ISODate);
    if (!recId.isValid())
        return { QString(), QDateTime{} };   // malformed: bad timestamp half
    return { uid, recId };
}

/// True when `id` carries an exception (recurrence-id) component.
inline bool isExceptionRecordId(const QString &id)
{
    return id.contains(QChar(kRecordIdentitySeparator));
}

}  // namespace Kalburator::Sync
