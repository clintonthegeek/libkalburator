#include "calendarbaselinestore.h"
// Use relative path to avoid resolving to src/journal/baselinestore.h
// (src/journal appears before src/storage in the include path ordering).
#include "../storage/baselinestore.h"
#include "../shape/canonicalrecord.h"
#include "../shape/shape.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {
Kalburator::Shape::CanonicalRecord makeCalRec(const QString &uid, const QString &ical) {
    Kalburator::Shape::CanonicalRecord rec;
    rec.recordId = uid;
    rec.shape    = Kalburator::Shape::Shape{
        Kalburator::Shape::DomainId{QStringLiteral("calendar")},
        Kalburator::Shape::EncodingId{QStringLiteral("ical")}};
    rec.data     = ical.toUtf8();
    return rec;
}
} // namespace

namespace Kalburator::Sync {

CalendarBaselineStore::CalendarBaselineStore(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_store(std::make_unique<Kalburator::Storage::BaselineStore>(dbPath))
{}

CalendarBaselineStore::~CalendarBaselineStore() = default;

bool CalendarBaselineStore::isValid() const { return m_store->isOpen(); }

// ---- iCal-text baselines ----

QString CalendarBaselineStore::baseline(const QString &mappingId, const QString &uid) const {
    auto rec = m_store->baselineV3(mappingId, uid);
    if (!rec) return {};
    return QString::fromUtf8(rec->data);
}

bool CalendarBaselineStore::setBaseline(const QString &mappingId, const QString &uid,
                                        const QString &icalText) {
    m_seenMappings.insert(mappingId);
    return m_store->setBaselineV3(mappingId, makeCalRec(uid, icalText));
}

bool CalendarBaselineStore::setBaselines(const QString &mappingId,
                                         const QHash<QString, QString> &uidToIcal) {
    m_seenMappings.insert(mappingId);
    bool ok = true;
    for (auto it = uidToIcal.begin(); it != uidToIcal.end(); ++it) {
        ok = m_store->setBaselineV3(mappingId, makeCalRec(it.key(), it.value())) && ok;
    }
    return ok;
}

bool CalendarBaselineStore::removeBaseline(const QString &mappingId, const QString &uid) {
    return m_store->removeBaselineV3(mappingId, uid);
}

bool CalendarBaselineStore::removeBaselines(const QString &mappingId) {
    return m_store->clearMappingV3(mappingId);
}

QHash<QString, QString> CalendarBaselineStore::allBaselines(const QString &mappingId) const {
    QHash<QString, QString> out;
    for (const auto &rec : m_store->baselinesForMappingV3(mappingId)) {
        out.insert(rec.recordId, QString::fromUtf8(rec.data));
    }
    return out;
}

bool CalendarBaselineStore::clearBaselines() {
    bool ok = true;
    for (const auto &m : m_seenMappings) {
        ok = m_store->clearMappingV3(m) && ok;
    }
    m_seenMappings.clear();
    return ok;
}

bool CalendarBaselineStore::hasBaselines(const QString &mappingId) const {
    return !m_store->baselinesForMappingV3(mappingId).isEmpty();
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
