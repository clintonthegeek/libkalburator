#include "syncdiff.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Event>
#include <KCalendarCore/Todo>
#include <KCalendarCore/RecurrenceRule>
#include <QCryptographicHash>
#include <QSet>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

static QString syncRecordKey(const SyncRecord &rec)
{
    if (rec.recurrenceId.isValid())
        return rec.uid + QChar(0) + rec.recurrenceId.toString(Qt::ISODate);
    return rec.uid;
}

// ============================================================================
// SyncRecord
// ============================================================================

QString SyncRecord::computeHash(const QString &icalData)
{
    if (icalData.isEmpty()) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(icalData.toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

QString SyncRecord::computeSemanticHash(const KCalendarCore::Incidence::Ptr &inc)
{
    if (!inc) {
        return QString();
    }

    // Build a canonical string from meaningful properties only.
    // This ignores: PRODID, DTSTAMP, CREATED, LAST-MODIFIED, SEQUENCE,
    // property ordering, line folding, and implementation-specific X-properties.
    QStringList parts;

    // Core identity
    parts << QStringLiteral("UID:") + inc->uid();
    parts << QStringLiteral("TYPE:") + QString::number(static_cast<int>(inc->type()));

    // Summary and description
    parts << QStringLiteral("SUMMARY:") + inc->summary();
    parts << QStringLiteral("DESCRIPTION:") + inc->description();
    parts << QStringLiteral("LOCATION:") + inc->location();

    // Date/time properties
    if (inc->dtStart().isValid()) {
        parts << QStringLiteral("DTSTART:") + inc->dtStart().toString(Qt::ISODate);
    }
    if (inc->type() == KCalendarCore::Incidence::TypeEvent) {
        auto event = inc.staticCast<KCalendarCore::Event>();
        if (event->dtEnd().isValid()) {
            parts << QStringLiteral("DTEND:") + event->dtEnd().toString(Qt::ISODate);
        }
        parts << QStringLiteral("TRANSP:") + QString::number(static_cast<int>(event->transparency()));
    }
    if (inc->type() == KCalendarCore::Incidence::TypeTodo) {
        auto todo = inc.staticCast<KCalendarCore::Todo>();
        if (todo->dtDue().isValid()) {
            parts << QStringLiteral("DUE:") + todo->dtDue().toString(Qt::ISODate);
        }
        if (todo->hasCompletedDate()) {
            parts << QStringLiteral("COMPLETED:") + todo->completed().toString(Qt::ISODate);
        }
        parts << QStringLiteral("PERCENT:") + QString::number(todo->percentComplete());
    }
    if (inc->allDay()) {
        parts << QStringLiteral("ALLDAY:1");
    }

    // Status and classification
    parts << QStringLiteral("STATUS:") + QString::number(static_cast<int>(inc->status()));
    parts << QStringLiteral("SECRECY:") + QString::number(static_cast<int>(inc->secrecy()));
    parts << QStringLiteral("PRIORITY:") + QString::number(inc->priority());

    // Categories (sorted for consistency)
    QStringList cats = inc->categories();
    cats.sort();
    parts << QStringLiteral("CATEGORIES:") + cats.join(QLatin1Char(','));

    // Recurrence (serialize the rule)
    if (inc->recurs()) {
        KCalendarCore::ICalFormat format;
        // Get recurrence as string - consistent across implementations
        QString rruleStr;
        for (const auto &rule : inc->recurrence()->rRules()) {
            rruleStr += format.toString(rule);
        }
        parts << QStringLiteral("RRULE:") + rruleStr;

        // Exception dates
        QStringList exdates;
        for (const QDate &d : inc->recurrence()->exDates()) {
            exdates << d.toString(Qt::ISODate);
        }
        exdates.sort();
        parts << QStringLiteral("EXDATE:") + exdates.join(QLatin1Char(','));

        // Exception date-times
        QStringList exdatetimes;
        for (const QDateTime &dt : inc->recurrence()->exDateTimes()) {
            exdatetimes << dt.toString(Qt::ISODate);
        }
        exdatetimes.sort();
        parts << QStringLiteral("EXDATETIME:") + exdatetimes.join(QLatin1Char(','));
    }

    // Alarms (count and trigger times)
    if (!inc->alarms().isEmpty()) {
        QStringList alarmParts;
        for (const auto &alarm : inc->alarms()) {
            if (alarm->enabled()) {
                alarmParts << QStringLiteral("%1:%2")
                    .arg(QString::number(static_cast<int>(alarm->type())))
                    .arg(alarm->startOffset().asSeconds());
            }
        }
        alarmParts.sort();
        parts << QStringLiteral("ALARMS:") + alarmParts.join(QLatin1Char(';'));
    }

    // Attendees (sorted by email for consistency)
    if (!inc->attendees().isEmpty()) {
        QStringList attendeeParts;
        for (const auto &att : inc->attendees()) {
            attendeeParts << QStringLiteral("%1:%2:%3")
                .arg(att.email())
                .arg(QString::number(static_cast<int>(att.status())))
                .arg(QString::number(static_cast<int>(att.role())));
        }
        attendeeParts.sort();
        parts << QStringLiteral("ATTENDEES:") + attendeeParts.join(QLatin1Char(';'));
    }

    // Organizer
    if (!inc->organizer().isEmpty()) {
        parts << QStringLiteral("ORGANIZER:") + inc->organizer().email();
    }

    // Related-to
    if (!inc->relatedTo().isEmpty()) {
        parts << QStringLiteral("RELATEDTO:") + inc->relatedTo();
    }

    // Recurrence exception identity
    if (inc->hasRecurrenceId()) {
        parts << QStringLiteral("RECURRENCE-ID:") + inc->recurrenceId().toString(Qt::ISODate);
    }

    // Sort all parts for consistent ordering
    parts.sort();

    // Compute hash of the canonical representation
    QString canonical = parts.join(QLatin1Char('\n'));
    return computeHash(canonical);
}

SyncRecord SyncRecord::fromIncidence(const KCalendarCore::Incidence::Ptr &inc,
                                      const QString &calendarId,
                                      const QString &backendId)
{
    SyncRecord record;
    if (!inc) {
        return record;
    }

    record.uid = inc->uid();
    record.calendarId = calendarId;
    record.backendId = backendId;
    record.incidence = inc;
    record.lastModified = inc->lastModified();
    record.recurrenceId = inc->hasRecurrenceId() ? inc->recurrenceId() : QDateTime();

    // Serialize to iCal for baseline storage (full representation)
    KCalendarCore::ICalFormat format;
    record.icalData = format.toICalString(inc);

    // Use semantic hash for change detection (ignores non-meaningful differences)
    // This ensures that the same incidence from different backends (local, CalDAV)
    // produces the same hash despite serialization differences like PRODID, DTSTAMP, etc.
    record.versionHash = computeSemanticHash(inc);

    return record;
}

// ============================================================================
// SyncDiff
// ============================================================================

SyncStats SyncDiff::targetStats() const
{
    SyncStats stats;
    for (const auto &change : toTarget) {
        switch (change.type) {
            case SyncChangeType::Created:  stats.created++; break;
            case SyncChangeType::Modified: stats.updated++; break;
            case SyncChangeType::Deleted:  stats.deleted++; break;
            case SyncChangeType::Unchanged: stats.unchanged++; break;
        }
        if (change.isConflict) {
            stats.conflicts++;
        }
    }
    stats.unchanged += unchangedUids.size();
    return stats;
}

SyncStats SyncDiff::sourceStats() const
{
    SyncStats stats;
    for (const auto &change : toSource) {
        switch (change.type) {
            case SyncChangeType::Created:  stats.created++; break;
            case SyncChangeType::Modified: stats.updated++; break;
            case SyncChangeType::Deleted:  stats.deleted++; break;
            case SyncChangeType::Unchanged: stats.unchanged++; break;
        }
        if (change.isConflict) {
            stats.conflicts++;
        }
    }
    stats.unchanged += unchangedUids.size();
    return stats;
}

// ============================================================================
// computeSyncDiff - Core 3-way merge algorithm
// ============================================================================

SyncDiff computeSyncDiff(const QList<SyncRecord> &sourceRecords,
                         const QList<SyncRecord> &targetRecords,
                         const QMap<QString, QString> &baselines,
                         SyncMode mode)
{
    SyncDiff diff;

    if (mode == SyncMode::Disabled) {
        return diff;
    }

    // Build maps for quick lookup (compound key: uid + recurrenceId)
    QMap<QString, SyncRecord> sourceMap;
    for (const auto &rec : sourceRecords) {
        if (rec.isValid()) {
            sourceMap[syncRecordKey(rec)] = rec;
        }
    }

    QMap<QString, SyncRecord> targetMap;
    for (const auto &rec : targetRecords) {
        if (rec.isValid()) {
            targetMap[syncRecordKey(rec)] = rec;
        }
    }

    // Collect all keys from all three sources
    QSet<QString> allKeys;
    for (const auto &rec : sourceRecords) {
        if (rec.isValid()) allKeys.insert(syncRecordKey(rec));
    }
    for (const auto &rec : targetRecords) {
        if (rec.isValid()) allKeys.insert(syncRecordKey(rec));
    }
    for (auto it = baselines.begin(); it != baselines.end(); ++it) {
        allKeys.insert(it.key());
    }

    // Process each key (uid or uid+recurrenceId)
    for (const QString &key : allKeys) {
        bool inSource = sourceMap.contains(key);
        bool inTarget = targetMap.contains(key);
        bool inBaseline = baselines.contains(key);

        SyncRecord sourceRec = inSource ? sourceMap[key] : SyncRecord();
        SyncRecord targetRec = inTarget ? targetMap[key] : SyncRecord();
        QString baselineIcal = inBaseline ? baselines[key] : QString();

        // Extract the actual UID for SyncChange/ConflictInfo fields
        QString uid = inSource ? sourceRec.uid : (inTarget ? targetRec.uid : key);

        // Compute baseline hash using semantic hash to match how current records
        // compute their hashes (ignores PRODID, DTSTAMP, ordering differences)
        QString baselineHash;
        if (!baselineIcal.isEmpty()) {
            KCalendarCore::ICalFormat format;
            KCalendarCore::MemoryCalendar::Ptr cal(
                new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
            if (format.fromString(cal, baselineIcal)) {
                auto incidences = cal->incidences();
                if (!incidences.isEmpty()) {
                    baselineHash = SyncRecord::computeSemanticHash(incidences.first());
                }
            }
        }

        // Determine change status for each side
        SyncChangeType sourceChange = SyncChangeType::Unchanged;
        SyncChangeType targetChange = SyncChangeType::Unchanged;

        if (!inBaseline) {
            // New item (not in baseline)
            if (inSource) sourceChange = SyncChangeType::Created;
            if (inTarget) targetChange = SyncChangeType::Created;
        } else {
            // Was in baseline - check for modifications or deletions
            if (!inSource) {
                sourceChange = SyncChangeType::Deleted;
            } else if (sourceRec.versionHash != baselineHash) {
                sourceChange = SyncChangeType::Modified;
            }

            if (!inTarget) {
                targetChange = SyncChangeType::Deleted;
            } else if (targetRec.versionHash != baselineHash) {
                targetChange = SyncChangeType::Modified;
            }
        }

        // Skip if nothing changed on either side
        if (sourceChange == SyncChangeType::Unchanged &&
            targetChange == SyncChangeType::Unchanged) {
            diff.unchangedUids.append(uid);
            continue;
        }

        // Detect conflicts: both sides changed (and not just both created the same thing)
        bool isConflict = false;
        if (sourceChange != SyncChangeType::Unchanged &&
            targetChange != SyncChangeType::Unchanged) {
            // Both sides changed - check if they're identical changes
            if (sourceChange == targetChange &&
                sourceRec.versionHash == targetRec.versionHash) {
                // Same change on both sides - no conflict, just mark as unchanged
                diff.unchangedUids.append(uid);
                continue;
            }
            isConflict = true;
        }

        // Build conflict info if needed
        ConflictInfo conflictInfo;
        if (isConflict) {
            conflictInfo.sourceId = uid;
            conflictInfo.targetId = uid;
            conflictInfo.sourceDescription = sourceRec.incidence ?
                sourceRec.incidence->summary() : QStringLiteral("(deleted)");
            conflictInfo.targetDescription = targetRec.incidence ?
                targetRec.incidence->summary() : QStringLiteral("(deleted)");
            conflictInfo.sourceModified = sourceRec.lastModified;
            conflictInfo.targetModified = targetRec.lastModified;
            conflictInfo.calendarId = sourceRec.calendarId.isEmpty() ?
                targetRec.calendarId : sourceRec.calendarId;
            conflictInfo.sourceBackendId = sourceRec.backendId;
            conflictInfo.targetBackendId = targetRec.backendId;
            conflictInfo.detectedAt = QDateTime::currentDateTime();

            // Populate iCal data for diff display in resolution dialog
            conflictInfo.sourceIcalData = sourceRec.icalData;
            conflictInfo.targetIcalData = targetRec.icalData;
            conflictInfo.baselineIcalData = baselineIcal;

            // Determine conflict type
            if (!inBaseline) {
                conflictInfo.type = ConflictType::BothCreated;
            } else if (sourceChange == SyncChangeType::Deleted ||
                       targetChange == SyncChangeType::Deleted) {
                conflictInfo.type = ConflictType::ModifyDelete;
            } else {
                conflictInfo.type = ConflictType::BothModified;
            }

            diff.conflicts.append(conflictInfo);
        }

        // Build baseline record for context
        SyncRecord baselineRec;
        if (inBaseline) {
            baselineRec.uid = uid;
            baselineRec.icalData = baselineIcal;
            baselineRec.versionHash = baselineHash;
        }

        // Determine what changes to apply based on sync mode
        switch (mode) {
            case SyncMode::OneWayUpload:
                // Source -> Target only (ignore target changes)
                if (sourceChange != SyncChangeType::Unchanged && !isConflict) {
                    SyncChange change;
                    change.type = sourceChange;
                    change.uid = uid;
                    change.sourceRecord = sourceRec;
                    change.targetRecord = targetRec;
                    change.baselineRecord = baselineRec;
                    change.isConflict = false;
                    diff.toTarget.append(change);
                } else if (isConflict) {
                    // In one-way upload, source wins conflicts
                    SyncChange change;
                    change.type = sourceChange;
                    change.uid = uid;
                    change.sourceRecord = sourceRec;
                    change.targetRecord = targetRec;
                    change.baselineRecord = baselineRec;
                    change.isConflict = true;
                    change.conflictInfo = conflictInfo;
                    diff.toTarget.append(change);
                }
                break;

            case SyncMode::OneWayDownload:
                // Target -> Source only (ignore source changes)
                if (targetChange != SyncChangeType::Unchanged && !isConflict) {
                    SyncChange change;
                    change.type = targetChange;
                    change.uid = uid;
                    change.sourceRecord = sourceRec;
                    change.targetRecord = targetRec;
                    change.baselineRecord = baselineRec;
                    change.isConflict = false;
                    diff.toSource.append(change);
                } else if (isConflict) {
                    // In one-way download, target wins conflicts
                    SyncChange change;
                    change.type = targetChange;
                    change.uid = uid;
                    change.sourceRecord = sourceRec;
                    change.targetRecord = targetRec;
                    change.baselineRecord = baselineRec;
                    change.isConflict = true;
                    change.conflictInfo = conflictInfo;
                    diff.toSource.append(change);
                }
                break;

            case SyncMode::TwoWay:
                // Bidirectional sync
                if (isConflict) {
                    // Both sides changed - record conflict, don't apply yet
                    SyncChange toTargetChange;
                    toTargetChange.type = sourceChange;
                    toTargetChange.uid = uid;
                    toTargetChange.sourceRecord = sourceRec;
                    toTargetChange.targetRecord = targetRec;
                    toTargetChange.baselineRecord = baselineRec;
                    toTargetChange.isConflict = true;
                    toTargetChange.conflictInfo = conflictInfo;
                    diff.toTarget.append(toTargetChange);
                } else {
                    // No conflict - apply changes in appropriate direction
                    if (sourceChange != SyncChangeType::Unchanged) {
                        SyncChange change;
                        change.type = sourceChange;
                        change.uid = uid;
                        change.sourceRecord = sourceRec;
                        change.targetRecord = targetRec;
                        change.baselineRecord = baselineRec;
                        change.isConflict = false;
                        diff.toTarget.append(change);
                    }
                    if (targetChange != SyncChangeType::Unchanged) {
                        SyncChange change;
                        change.type = targetChange;
                        change.uid = uid;
                        change.sourceRecord = sourceRec;
                        change.targetRecord = targetRec;
                        change.baselineRecord = baselineRec;
                        change.isConflict = false;
                        diff.toSource.append(change);
                    }
                }
                break;

            case SyncMode::Disabled:
                // Already handled above
                break;
        }
    }

    // Debug log removed - SyncWorker shows timing summary

    return diff;
}

// ============================================================================
// computeQuickDiff - Fast 2-way algorithm without baselines
// ============================================================================

SyncDiff computeQuickDiff(const QList<SyncRecord> &sourceRecords,
                          const QList<SyncRecord> &targetRecords,
                          SyncMode mode)
{
    SyncDiff diff;

    if (mode == SyncMode::Disabled) {
        return diff;
    }

    // Build maps for quick lookup (compound key: uid + recurrenceId)
    QMap<QString, SyncRecord> sourceMap;
    for (const auto &rec : sourceRecords) {
        if (rec.isValid()) {
            sourceMap[syncRecordKey(rec)] = rec;
        }
    }

    QMap<QString, SyncRecord> targetMap;
    for (const auto &rec : targetRecords) {
        if (rec.isValid()) {
            targetMap[syncRecordKey(rec)] = rec;
        }
    }

    // Collect all keys
    QSet<QString> allKeys;
    for (const auto &rec : sourceRecords) {
        if (rec.isValid()) allKeys.insert(syncRecordKey(rec));
    }
    for (const auto &rec : targetRecords) {
        if (rec.isValid()) allKeys.insert(syncRecordKey(rec));
    }

    // Process each key (uid or uid+recurrenceId)
    for (const QString &key : allKeys) {
        bool inSource = sourceMap.contains(key);
        bool inTarget = targetMap.contains(key);

        SyncRecord sourceRec = inSource ? sourceMap[key] : SyncRecord();
        SyncRecord targetRec = inTarget ? targetMap[key] : SyncRecord();
        QString uid = inSource ? sourceRec.uid : targetRec.uid;

        if (inSource && inTarget) {
            // On both sides - check if same
            if (sourceRec.versionHash == targetRec.versionHash) {
                // Identical - unchanged
                diff.unchangedUids.append(uid);
            } else {
                // Both sides have the same UID with different content.
                // This is a BothCreated conflict — fall through to the full
                // 3-way diff path by treating it as a conflict so the user's
                // configured conflict policy is respected instead of silently
                // applying LastWriteWins.
                ConflictInfo conflictInfo;
                conflictInfo.sourceId = uid;
                conflictInfo.targetId = uid;
                conflictInfo.sourceDescription = sourceRec.incidence ?
                    sourceRec.incidence->summary() : QStringLiteral("(unknown)");
                conflictInfo.targetDescription = targetRec.incidence ?
                    targetRec.incidence->summary() : QStringLiteral("(unknown)");
                conflictInfo.sourceModified = sourceRec.lastModified;
                conflictInfo.targetModified = targetRec.lastModified;
                conflictInfo.calendarId = sourceRec.calendarId.isEmpty() ?
                    targetRec.calendarId : sourceRec.calendarId;
                conflictInfo.sourceBackendId = sourceRec.backendId;
                conflictInfo.targetBackendId = targetRec.backendId;
                conflictInfo.detectedAt = QDateTime::currentDateTime();
                conflictInfo.sourceIcalData = sourceRec.icalData;
                conflictInfo.targetIcalData = targetRec.icalData;
                conflictInfo.type = ConflictType::BothCreated;

                diff.conflicts.append(conflictInfo);

                SyncChange change;
                change.type = SyncChangeType::Modified;
                change.uid = uid;
                change.sourceRecord = sourceRec;
                change.targetRecord = targetRec;
                change.isConflict = true;
                change.conflictInfo = conflictInfo;

                // Route the conflict change based on sync mode
                switch (mode) {
                    case SyncMode::TwoWay:
                        diff.toTarget.append(change);
                        break;
                    case SyncMode::OneWayUpload:
                        diff.toTarget.append(change);
                        break;
                    case SyncMode::OneWayDownload:
                        diff.toSource.append(change);
                        break;
                    case SyncMode::Disabled:
                        break;
                }
            }
        } else if (inSource && !inTarget) {
            // Only on source - create on target
            SyncChange change;
            change.type = SyncChangeType::Created;
            change.uid = uid;
            change.sourceRecord = sourceRec;
            change.isConflict = false;

            if (mode == SyncMode::OneWayUpload || mode == SyncMode::TwoWay) {
                diff.toTarget.append(change);
            }
        } else if (!inSource && inTarget) {
            // Only on target - create on source (if two-way) or ignore
            SyncChange change;
            change.type = SyncChangeType::Created;
            change.uid = uid;
            change.targetRecord = targetRec;
            change.isConflict = false;

            if (mode == SyncMode::OneWayDownload || mode == SyncMode::TwoWay) {
                diff.toSource.append(change);
            }
        }
    }

    qDebug() << "computeQuickDiff: processed" << allKeys.size() << "UIDs"
             << "- toTarget:" << diff.toTarget.size()
             << "toSource:" << diff.toSource.size()
             << "unchanged:" << diff.unchangedUids.size();

    return diff;
}

// ============================================================================
// Helper functions
// ============================================================================

QList<SyncRecord> incidencesToSyncRecords(
    const QList<KCalendarCore::Incidence::Ptr> &incidences,
    const QString &calendarId,
    const QString &backendId)
{
    QList<SyncRecord> records;
    records.reserve(incidences.size());

    for (const auto &inc : incidences) {
        if (inc) {
            records.append(SyncRecord::fromIncidence(inc, calendarId, backendId));
        }
    }

    return records;
}

// ============================================================================
// CalendarPropertyRecord
// ============================================================================

QString CalendarPropertyRecord::computeHash(const CalendarPropertyRecord &record)
{
    // Build canonical string from properties
    QStringList parts;

    if (record.color.isValid()) {
        parts << QStringLiteral("COLOR:") + record.color.name(QColor::HexArgb);
    }
    if (!record.description.isEmpty()) {
        parts << QStringLiteral("DESCRIPTION:") + record.description;
    }

    // Sort for consistent ordering
    parts.sort();
    QString canonical = parts.join(QLatin1Char('\n'));

    // Compute hash
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(canonical.toUtf8());
    return QString::fromLatin1(hash.result().toHex());
}

QString CalendarPropertyRecord::toJson() const
{
    QJsonObject obj;

    if (color.isValid()) {
        obj[QStringLiteral("color")] = color.name(QColor::HexArgb);
    }
    if (!description.isEmpty()) {
        obj[QStringLiteral("description")] = description;
    }

    QJsonDocument doc(obj);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

CalendarPropertyRecord CalendarPropertyRecord::fromJson(const QString &json,
                                                         const QString &backendId,
                                                         const QString &calendarId)
{
    CalendarPropertyRecord record;
    record.backendId = backendId;
    record.calendarId = calendarId;

    if (json.isEmpty()) {
        return record;
    }

    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        return record;
    }

    QJsonObject obj = doc.object();

    if (obj.contains(QStringLiteral("color"))) {
        QString colorStr = obj[QStringLiteral("color")].toString();
        record.color = QColor(colorStr);
    }
    if (obj.contains(QStringLiteral("description"))) {
        record.description = obj[QStringLiteral("description")].toString();
    }

    record.versionHash = computeHash(record);

    return record;
}
