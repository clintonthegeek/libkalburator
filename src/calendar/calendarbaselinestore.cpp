#include "calendarbaselinestore.h"
// Use relative path to avoid resolving to src/journal/baselinestore.h
// (src/journal appears before src/storage in the include path ordering).
#include "../storage/baselinestore.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace Kalburator::Sync {

CalendarBaselineStore::CalendarBaselineStore(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_store(std::make_unique<Kalburator::Storage::BaselineStore>(dbPath))
{}

CalendarBaselineStore::~CalendarBaselineStore() = default;

bool CalendarBaselineStore::isValid() const { return m_store->isOpen(); }

// ---- iCal-text baselines ----

QString CalendarBaselineStore::baseline(const QString &mappingId, const QString &uid) const {
    return m_store->calendarIcalBaseline(mappingId, uid);
}

bool CalendarBaselineStore::setBaseline(const QString &mappingId, const QString &uid,
                                        const QString &icalText) {
    return m_store->setCalendarIcalBaseline(mappingId, uid, icalText);
}

bool CalendarBaselineStore::setBaselines(const QString &mappingId,
                                         const QHash<QString, QString> &uidToIcal) {
    bool ok = true;
    for (auto it = uidToIcal.begin(); it != uidToIcal.end(); ++it) {
        ok = m_store->setCalendarIcalBaseline(mappingId, it.key(), it.value()) && ok;
    }
    return ok;
}

bool CalendarBaselineStore::removeBaseline(const QString &mappingId, const QString &uid) {
    return m_store->removeCalendarIcalBaseline(mappingId, uid);
}

bool CalendarBaselineStore::removeBaselines(const QString &mappingId) {
    return m_store->clearCalendarIcalBaselinesForMapping(mappingId);
}

QHash<QString, QString> CalendarBaselineStore::allBaselines(const QString &mappingId) const {
    return m_store->calendarIcalBaselinesForMapping(mappingId);
}

bool CalendarBaselineStore::clearBaselines() {
    return m_store->clearAllCalendarIcalBaselines();
}

bool CalendarBaselineStore::hasBaselines(const QString &mappingId) const {
    return m_store->hasCalendarIcalBaselines(mappingId);
}

// ---- property-JSON baselines ----

QString CalendarBaselineStore::propertyBaseline(const QString &mappingId,
                                                const QString &calendarId) const {
    const auto map = m_store->collectionBaseline(mappingId, calendarId);
    if (map.isEmpty()) return {};
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map))
                                 .toJson(QJsonDocument::Compact));
}

bool CalendarBaselineStore::setPropertyBaseline(const QString &mappingId,
                                                const QString &calendarId,
                                                const QString &propertyJson) {
    const auto doc = QJsonDocument::fromJson(propertyJson.toUtf8());
    const auto map = doc.isObject() ? doc.object().toVariantMap() : QVariantMap{};
    return m_store->setCollectionBaseline(mappingId, calendarId, map);
}

bool CalendarBaselineStore::removePropertyBaseline(const QString &mappingId,
                                                   const QString &calendarId) {
    return m_store->removeCollectionBaseline(mappingId, calendarId);
}

QHash<QString, QString> CalendarBaselineStore::allPropertyBaselines(const QString &mappingId) const {
    // The unified store does not enumerate collection baselines by mapping;
    // this method had no call sites (verified by grep at Task 7 time).
    // Asserts in debug builds catch latent callers.
    Q_UNUSED(mappingId);
    Q_ASSERT_X(false, "CalendarBaselineStore::allPropertyBaselines",
               "Method has no call sites and is not implemented in the facade. "
               "If you hit this, migrate the caller to Storage::BaselineStore directly.");
    return {};
}

// ---- last-sync timestamp ----

QDateTime CalendarBaselineStore::lastSyncTime(const QString &mappingId) const {
    return m_store->lastSyncTime(mappingId);
}

bool CalendarBaselineStore::setLastSyncTime(const QString &mappingId, const QDateTime &when) {
    return m_store->setLastSyncTime(mappingId, when);
}

} // namespace Kalburator::Sync
