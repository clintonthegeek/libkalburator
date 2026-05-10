#pragma once

#include <QHash>
#include <QString>

#include "syncbackend.h"
#include "shape.h"

namespace Kalburator::Sinks {

/// Universal file-backed sink. Accepts any shape; stores each record as
///   <rootPath>/<sanitized-record-id>.<encoding>.<domain>
/// A manifest at <rootPath>/_shapes.json lists the shapes for which
/// createCollection() has been called, enabling fast re-discovery.
///
/// Phase K.4: still inherits `SyncBackend` (so that BackendRegistry
/// can store it alongside calendar backends), but no longer overrides
/// any of the calendar-typed virtuals. Those are now non-pure on
/// SyncBackend with empty default implementations, so this class
/// inherits the no-op behaviour automatically.
class RawFilesBackend : public Kalburator::Sync::SyncBackend {
    Q_OBJECT
public:
    explicit RawFilesBackend(QString rootPath, QObject *parent = nullptr);

    QString backendType() const override { return QStringLiteral("raw-files"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override
        { return { Kalburator::Shape::Shape::Any() }; }
    QString resourceId() const override
        { return QStringLiteral("raw-files:") + m_rootPath; }

    // ---- IBlobBackend ----
    QString displayName() const override { return QStringLiteral("Raw Files Backend"); }
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

private:
    QString filePathFor(const QString &collectionId, const QString &recordId) const;
    Kalburator::Sync::BackendRecord readFile(const QString &absPath) const;

    void loadManifest();
    void saveManifest() const;

    static QString sanitize(const QString &id);
    static QString suffixFor(const QString &collectionId);

    QString m_rootPath;
    QHash<QString, Kalburator::Sync::CollectionInfo> m_collections;
};

} // namespace Kalburator::Sinks
