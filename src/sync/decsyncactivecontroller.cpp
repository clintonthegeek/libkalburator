#include "decsyncactivecontroller.h"

#include "incidencediff.h"

#include <QDebug>
#include <QDateTime>
#include <QJsonValue>

#include <KCalendarCore/ICalFormat>

// ============================================================================
// Static data
// ============================================================================

// Properties that reflect metadata / auto-generated bookkeeping rather than
// user intent.  We skip these when diffing so that clock skew or PRODID
// differences don't manufacture fake conflicts.
const QStringList DecSyncActiveController::s_ignoredProperties = {
    QStringLiteral("PRODID"),
    QStringLiteral("DTSTAMP"),
    QStringLiteral("CREATED"),
    QStringLiteral("LAST-MODIFIED"),
    QStringLiteral("SEQUENCE"),
    QStringLiteral("UID"),
};

// ============================================================================
// Construction
// ============================================================================

DecSyncActiveController::DecSyncActiveController(DecSyncCollection *collection,
                                                 DecSyncControllerStore *store,
                                                 const QString &ownAppId,
                                                 const QString &collectionId,
                                                 QObject *parent)
    : QObject(parent)
    , m_collection(collection)
    , m_store(store)
    , m_ownAppId(ownAppId)
    , m_collectionId(collectionId)
{
}

bool DecSyncActiveController::isInitialLoad() const
{
    return m_store->allBaselines(m_collectionId).isEmpty();
}

// ============================================================================
// preprocessFetch
// ============================================================================

QMap<QString, DecSyncEntry> DecSyncActiveController::preprocessFetch()
{
    QMap<QString, QMap<QString, DecSyncEntry>> perApp = m_collection->readPerAppResources();

    // Update activity timestamps for every app we just read
    updateAppActivity(perApp);

    // Collect the set of all UIDs seen across all apps (excluding own).
    // Also keep own-app entries separately for later inclusion.
    QMap<QString, QMap<QString, DecSyncEntry>> otherApps;
    QMap<QString, DecSyncEntry> ownAppEntries;

    for (auto it = perApp.cbegin(); it != perApp.cend(); ++it) {
        if (it.key() == m_ownAppId) {
            ownAppEntries = it.value();
        } else {
            otherApps.insert(it.key(), it.value());
        }
    }

    // Build uid → { appId → entry } for all other-app entries
    QMap<QString, QMap<QString, DecSyncEntry>> uidToAppEntries;
    for (auto appIt = otherApps.cbegin(); appIt != otherApps.cend(); ++appIt) {
        const QString &appId = appIt.key();
        for (auto entryIt = appIt.value().cbegin(); entryIt != appIt.value().cend(); ++entryIt) {
            uidToAppEntries[entryIt.key()][appId] = entryIt.value();
        }
    }

    // Also look for UIDs in the baseline that no app currently mentions
    // (those should become deletion entries in the result).
    const QMap<QString, BaselineRecord> allBaselines = m_store->allBaselines(m_collectionId);
    for (auto blIt = allBaselines.cbegin(); blIt != allBaselines.cend(); ++blIt) {
        const QString &uid = blIt.key();
        if (!uidToAppEntries.contains(uid)) {
            // UID is in baseline but no other-app has any entry for it.
            // We treat this as "all apps deleted it" — add a placeholder so
            // mergeEntries sees an empty map → Deletion.
            //
            // Note: if only ownApp has an entry for this UID, we pass it through
            // below without adding a deletion placeholder.
            if (!ownAppEntries.contains(uid)) {
                uidToAppEntries.insert(uid, QMap<QString, DecSyncEntry>());
            }
        }
    }

    QMap<QString, DecSyncEntry> result;

    // Process all UIDs seen from other apps (or baseline-only UIDs)
    QSet<QString> processedUids;
    for (auto it = uidToAppEntries.cbegin(); it != uidToAppEntries.cend(); ++it) {
        const QString &uid = it.key();
        const QMap<QString, DecSyncEntry> &appEntries = it.value();
        processedUids.insert(uid);

        if (appEntries.size() == 0) {
            // Baseline-only UID that all apps have dropped → deletion
            DecSyncEntry deletion;
            deletion.path = {QStringLiteral("resources"), uid};
            deletion.datetime = QDateTime::currentDateTimeUtc()
                                    .toString(Qt::ISODate);
            deletion.key = QJsonValue(QJsonValue::Null);
            deletion.value = QJsonValue(QJsonValue::Null);
            result[uid] = deletion;
            m_store->removeBaseline(m_collectionId, uid);
            m_store->logDeletion(m_collectionId, uid, deletion.datetime);
            continue;
        }

        if (appEntries.size() == 1) {
            // Single other-app entry: pass through as-is, update baseline if newer
            const DecSyncEntry &entry = appEntries.cbegin().value();
            result[uid] = entry;

            if (!entry.value.isNull()) {
                const QString ical = entry.value.toString();
                auto bl = m_store->baseline(m_collectionId, uid);
                if (!bl.has_value() || entry.datetime > bl->writtenAt) {
                    m_store->setBaseline(m_collectionId, uid, ical, entry.datetime);
                }
            } else {
                // Deletion from the sole other-app
                m_store->removeBaseline(m_collectionId, uid);
                m_store->logDeletion(m_collectionId, uid, entry.datetime);
            }
            continue;
        }

        // Multiple other-apps: perform property-level merge
        MergeResult mr = mergeEntries(uid, appEntries);

        switch (mr.type) {
        case MergeResult::Type::Deletion: {
            DecSyncEntry deletion;
            deletion.path = {QStringLiteral("resources"), uid};
            deletion.datetime = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            deletion.key = QJsonValue(QJsonValue::Null);
            deletion.value = QJsonValue(QJsonValue::Null);
            result[uid] = deletion;
            m_store->removeBaseline(m_collectionId, uid);
            m_store->logDeletion(m_collectionId, uid, deletion.datetime);
            break;
        }
        case MergeResult::Type::Unchanged: {
            // Treat like single-app pass-through
            const DecSyncEntry &newest = [&]() -> const DecSyncEntry & {
                const DecSyncEntry *n = nullptr;
                for (auto &e : appEntries) {
                    if (!n || e.isNewerThan(*n)) n = &e;
                }
                return *n;
            }();
            result[uid] = newest;
            break;
        }
        case MergeResult::Type::Merged: {
            // Write authoritative merged entry back to collection
            const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            m_collection->setEntry({QStringLiteral("resources"), uid},
                                   QJsonValue(QJsonValue::Null),
                                   QJsonValue(mr.mergedIcalData));
            m_store->setBaseline(m_collectionId, uid, mr.mergedIcalData, now);

            // Build a synthetic entry to return
            DecSyncEntry merged;
            merged.path = {QStringLiteral("resources"), uid};
            merged.datetime = now;
            merged.key = QJsonValue(QJsonValue::Null);
            merged.value = QJsonValue(mr.mergedIcalData);
            result[uid] = merged;
            break;
        }
        case MergeResult::Type::Conflict: {
            // LWW: winner's data wins, write back as authoritative
            const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            m_collection->setEntry({QStringLiteral("resources"), uid},
                                   QJsonValue(QJsonValue::Null),
                                   QJsonValue(mr.winnerIcalData));
            m_store->setBaseline(m_collectionId, uid, mr.winnerIcalData, now);

            DecSyncEntry winner;
            winner.path = {QStringLiteral("resources"), uid};
            winner.datetime = now;
            winner.key = QJsonValue(QJsonValue::Null);
            winner.value = QJsonValue(mr.winnerIcalData);
            result[uid] = winner;
            break;
        }
        }
    }

    // Include own-app entries for UIDs not already processed above
    for (auto it = ownAppEntries.cbegin(); it != ownAppEntries.cend(); ++it) {
        if (!processedUids.contains(it.key())) {
            result[it.key()] = it.value();
        }
    }

    return result;
}

// ============================================================================
// runActiveSync
// ============================================================================

void DecSyncActiveController::runActiveSync(const QMap<QString, QString> &currentItems)
{
    QMap<QString, QMap<QString, DecSyncEntry>> perApp = m_collection->readPerAppResources();
    updateAppActivity(perApp);

    // Separate own vs other apps
    QMap<QString, QMap<QString, DecSyncEntry>> otherApps;
    for (auto it = perApp.cbegin(); it != perApp.cend(); ++it) {
        if (it.key() != m_ownAppId) {
            otherApps.insert(it.key(), it.value());
        }
    }

    // Build uid → {appId → entry}
    QMap<QString, QMap<QString, DecSyncEntry>> uidToAppEntries;
    for (auto appIt = otherApps.cbegin(); appIt != otherApps.cend(); ++appIt) {
        for (auto entryIt = appIt.value().cbegin(); entryIt != appIt.value().cend(); ++entryIt) {
            uidToAppEntries[entryIt.key()][appIt.key()] = entryIt.value();
        }
    }

    // Add baseline-only UIDs as deletion candidates
    const QMap<QString, BaselineRecord> allBaselines = m_store->allBaselines(m_collectionId);
    for (auto blIt = allBaselines.cbegin(); blIt != allBaselines.cend(); ++blIt) {
        if (!uidToAppEntries.contains(blIt.key())) {
            uidToAppEntries.insert(blIt.key(), QMap<QString, DecSyncEntry>());
        }
    }

    const int total = uidToAppEntries.size();
    int current = 0;

    for (auto it = uidToAppEntries.cbegin(); it != uidToAppEntries.cend(); ++it) {
        const QString &uid = it.key();
        const QMap<QString, DecSyncEntry> &appEntries = it.value();

        emit progressChanged(current, total);

        if (appEntries.isEmpty()) {
            emit itemDeleted(m_collectionId, uid);
            m_store->removeBaseline(m_collectionId, uid);
            m_store->logDeletion(m_collectionId, uid,
                                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            ++current;
            continue;
        }

        // Early-return optimisation: skip UIDs that haven't changed
        if (!currentItems.isEmpty()) {
            bool anyNewer = false;
            for (const auto &entry : appEntries) {
                if (!entry.value.isNull()) {
                    auto bl = m_store->baseline(m_collectionId, uid);
                    if (!bl.has_value() || entry.datetime > bl->writtenAt) {
                        anyNewer = true;
                        break;
                    }
                }
            }
            if (!anyNewer) {
                ++current;
                continue;
            }
        }

        MergeResult mr = mergeEntries(uid, appEntries);

        switch (mr.type) {
        case MergeResult::Type::Deletion:
            emit itemDeleted(m_collectionId, uid);
            m_store->removeBaseline(m_collectionId, uid);
            m_store->logDeletion(m_collectionId, uid,
                                 QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
            break;

        case MergeResult::Type::Unchanged: {
            // Find newest entry
            const DecSyncEntry *n = nullptr;
            for (const auto &e : appEntries) {
                if (!n || e.isNewerThan(*n)) n = &e;
            }
            if (n && !n->value.isNull()) {
                const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                m_store->setBaseline(m_collectionId, uid, n->value.toString(), now);
                emit itemUpdated(m_collectionId, uid, n->value.toString());
            }
            break;
        }

        case MergeResult::Type::Merged: {
            const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            m_collection->setEntry({QStringLiteral("resources"), uid},
                                   QJsonValue(QJsonValue::Null),
                                   QJsonValue(mr.mergedIcalData));
            m_store->setBaseline(m_collectionId, uid, mr.mergedIcalData, now);
            emit itemUpdated(m_collectionId, uid, mr.mergedIcalData);
            break;
        }

        case MergeResult::Type::Conflict:
            emit conflictDetected(uid,
                                  mr.winnerIcalData,
                                  mr.loserIcalData,
                                  mr.baselineIcalData);
            break;
        }

        ++current;
    }

    emit progressChanged(total, total);
}

// ============================================================================
// mergeEntries — core logic
// ============================================================================

MergeResult DecSyncActiveController::mergeEntries(
    const QString &uid,
    const QMap<QString, DecSyncEntry> &appEntries)
{
    MergeResult result;

    // --- Deletion check ---
    // All entries null → deletion
    if (!appEntries.isEmpty()) {
        bool anyNonNull = false;
        for (const auto &e : appEntries) {
            if (!e.value.isNull()) {
                anyNonNull = true;
                break;
            }
        }
        if (!anyNonNull) {
            result.type = MergeResult::Type::Deletion;
            return result;
        }
    }

    // Collect non-null (live) entries
    QMap<QString, DecSyncEntry> liveEntries;
    for (auto it = appEntries.cbegin(); it != appEntries.cend(); ++it) {
        if (!it.value().value.isNull()) {
            liveEntries[it.key()] = it.value();
        }
    }

    if (liveEntries.isEmpty()) {
        result.type = MergeResult::Type::Deletion;
        return result;
    }

    // Single live entry — pass through unchanged
    if (liveEntries.size() == 1) {
        result.type = MergeResult::Type::Unchanged;
        result.mergedIcalData = liveEntries.cbegin().value().value.toString();
        return result;
    }

    // Find newest live entry (template for rebuild)
    const DecSyncEntry *newestEntry = nullptr;
    for (const auto &e : liveEntries) {
        if (!newestEntry || e.isNewerThan(*newestEntry)) {
            newestEntry = &e;
        }
    }
    const QString newestIcal = newestEntry->value.toString();

    // Load baseline
    auto blOpt = m_store->baseline(m_collectionId, uid);
    if (!blOpt.has_value()) {
        // No baseline: first encounter. Use newest entry, store it as baseline.
        const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        m_store->setBaseline(m_collectionId, uid, newestIcal, now);

        result.type = MergeResult::Type::Unchanged;
        result.mergedIcalData = newestIcal;
        return result;
    }

    const QString baselineIcal = blOpt->icalData;

    // Parse property maps
    const QMap<QString, QString> baselineProps = IncidenceDiff::parseIcalProperties(baselineIcal);

    // For each app, find which properties it changed vs the baseline
    // Track: propertyName → map of {value → list-of-appIds-that-chose-it}
    QMap<QString, QMap<QString, QStringList>> propChanges;

    for (auto it = liveEntries.cbegin(); it != liveEntries.cend(); ++it) {
        const QString &appId = it.key();
        const QString appIcal = it.value().value.toString();
        const QMap<QString, QString> appProps = IncidenceDiff::parseIcalProperties(appIcal);

        // Find all keys present in either app or baseline
        QSet<QString> allKeys;
        for (const QString &k : appProps.keys())      allKeys.insert(k);
        for (const QString &k : baselineProps.keys()) allKeys.insert(k);

        for (const QString &prop : allKeys) {
            if (s_ignoredProperties.contains(prop)) {
                continue;
            }

            const QString appVal      = appProps.value(prop);
            const QString baselineVal = baselineProps.value(prop);

            if (appVal != baselineVal) {
                // This app changed this property
                propChanges[prop][appVal].append(appId);
            }
        }
    }

    // If no property changes vs baseline at all, everything is Unchanged
    if (propChanges.isEmpty()) {
        result.type = MergeResult::Type::Unchanged;
        result.mergedIcalData = newestIcal;
        return result;
    }

    // Determine winners per property and detect conflicts
    QMap<QString, QString> winningProps;   // prop → winning value
    bool hasConflict = false;
    QString conflictWinnerIcal;
    QString conflictLoserIcal;

    for (auto it = propChanges.cbegin(); it != propChanges.cend(); ++it) {
        const QString &prop = it.key();
        const QMap<QString, QStringList> &valueToApps = it.value();

        if (valueToApps.size() == 1) {
            // All apps that changed this property changed it to the same value
            winningProps[prop] = valueToApps.cbegin().key();
        } else {
            // Multiple different new values — true conflict
            // Find the newest entry among those with conflicting values
            hasConflict = true;

            // Determine LWW winner/loser by picking the two entries with the
            // most recent timestamp among the conflicting apps.
            const DecSyncEntry *winner = nullptr;
            const DecSyncEntry *loser  = nullptr;

            for (const auto &apps : valueToApps) {
                for (const QString &appId : apps) {
                    const DecSyncEntry &e = liveEntries[appId];
                    if (!winner || e.isNewerThan(*winner)) {
                        loser  = winner;
                        winner = &e;
                    } else if (!loser || e.isNewerThan(*loser)) {
                        loser = &e;
                    }
                }
            }

            if (winner) conflictWinnerIcal = winner->value.toString();
            if (loser)  conflictLoserIcal  = loser->value.toString();

            // For LWW merge in preprocessFetch, record the winner's value
            if (winner) {
                const QMap<QString, QString> winnerProps =
                    IncidenceDiff::parseIcalProperties(winner->value.toString());
                winningProps[prop] = winnerProps.value(prop);
            }
        }
    }

    if (hasConflict) {
        // Rebuild merged iCal using LWW winning values for the conflict properties,
        // still applying all non-conflicting silent merges too.
        const QString mergedIcal = rebuildIcal(newestIcal, winningProps);

        result.type            = MergeResult::Type::Conflict;
        result.mergedIcalData  = mergedIcal;
        result.winnerIcalData  = conflictWinnerIcal;
        result.loserIcalData   = conflictLoserIcal;
        result.baselineIcalData = baselineIcal;
        return result;
    }

    // Non-conflicting merge: rebuild iCal with all winning property values
    const QString mergedIcal = rebuildIcal(newestIcal, winningProps);

    result.type           = MergeResult::Type::Merged;
    result.mergedIcalData = mergedIcal;
    result.baselineIcalData = baselineIcal;
    return result;
}

// ============================================================================
// rebuildIcal
// ============================================================================

QString DecSyncActiveController::rebuildIcal(const QString &templateIcal,
                                              const QMap<QString, QString> &winningProps)
{
    if (winningProps.isEmpty()) {
        return templateIcal;
    }

    KCalendarCore::ICalFormat format;
    KCalendarCore::Incidence::Ptr incidence = format.fromString(templateIcal);

    if (!incidence) {
        qWarning() << "DecSyncActiveController::rebuildIcal: failed to parse template iCal, "
                      "falling back to template as-is";
        return templateIcal;
    }

    for (auto it = winningProps.cbegin(); it != winningProps.cend(); ++it) {
        if (!IncidenceDiff::applyPropertyToIncidence(incidence, it.key(), it.value())) {
            qDebug() << "DecSyncActiveController::rebuildIcal: could not apply property"
                     << it.key() << "— skipped";
        }
    }

    const QString rebuilt = format.toICalString(incidence);
    if (rebuilt.isEmpty()) {
        qWarning() << "DecSyncActiveController::rebuildIcal: serialization returned empty, "
                      "falling back to template";
        return templateIcal;
    }

    return rebuilt;
}

// ============================================================================
// updateAppActivity
// ============================================================================

void DecSyncActiveController::updateAppActivity(
    const QMap<QString, QMap<QString, DecSyncEntry>> &perAppData)
{
    for (auto appIt = perAppData.cbegin(); appIt != perAppData.cend(); ++appIt) {
        const QString &appId = appIt.key();
        QString latestTimestamp;

        for (const auto &entry : appIt.value()) {
            if (latestTimestamp.isEmpty() || entry.datetime > latestTimestamp) {
                latestTimestamp = entry.datetime;
            }
        }

        if (!latestTimestamp.isEmpty()) {
            m_store->recordAppActivity(m_collectionId, appId, latestTimestamp);
        }
    }
}
