#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QString>

#include "syncbackend.h"
#include "shape.h"

namespace Kalburator::Sinks {

/// Universal SQLite-backed sink. Accepts any shape; on first push of a
/// new shape creates a table <domain>_<encoding> with:
///   record_id TEXT PRIMARY KEY
///   data BLOB NOT NULL
///   content_hash TEXT NOT NULL
///   last_modified TEXT NOT NULL
/// A _shapes table tracks which shapes have been seen.
/// PropertyCatalogue-derived columns are a G.10 enhancement.
class GenericSqliteBackend : public Kalburator::Sync::SyncBackend {
    Q_OBJECT
public:
    explicit GenericSqliteBackend(QString dbPath, QObject *parent = nullptr);
    ~GenericSqliteBackend() override;

    QString backendType() const override { return QStringLiteral("generic-sqlite"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override
        { return { Kalburator::Shape::Shape::Any() }; }
    QString resourceId() const override
        { return QStringLiteral("generic-sqlite:") + m_dbPath; }

    // ---- IBlobBackend ----
    QString displayName() const override { return QStringLiteral("Generic SQLite Backend"); }
    bool isAvailable() const override;

    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const Kalburator::Sync::CollectionInfo &info) override;
    void deleteCollection(const QString &collectionId);
    void clearCollection(const QString &collectionId);

    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &record) override;
    bool updateRecord(const Kalburator::Sync::BackendRecord &record) override;
    bool deleteRecord(const QString &recordId) override;

    // ---- SyncBackend calendar stubs ----
    void loadCalendars(const QString &collectionId) override;
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}
    void startSync(const QString &, KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}

private:
    bool ensureOpen();
    bool ensureSchema();
    bool ensureTableFor(const QString &collectionId);
    static QString tableNameFor(const QString &collectionId);

    // RecordId format: "<collectionId>/<originalId>"
    static QString encodeRecordId(const QString &collectionId, const QString &id);
    static bool decodeRecordId(const QString &recordId,
                               QString *collectionId, QString *id);

    QString m_dbPath;
    QString m_connectionName;
    QHash<QString, Kalburator::Sync::CollectionInfo> m_collections;
    bool m_open = false;
};

} // namespace Kalburator::Sinks
