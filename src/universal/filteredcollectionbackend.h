#pragma once

#include <QString>

#include "syncbackend.h"          // Kalburator::Sync::SyncBackend (calendar-typed base)
#include "recordfilter.h"         // Kalburator::Shape::RecordFilter
#include "collectioninfo.h"       // Kalburator::Sync::CollectionInfo

namespace Kalburator::Sync { class BackendRegistry; }

namespace Kalburator::Sinks {

/// Borrowed view of one collection on a parent SyncBackend, projected
/// through a `RecordFilter` as if it were its own backend with one
/// collection. Reads filter; writes stamp the filter property (`Contains`
/// additive, `Equals` filter-authoritative); delete + writability delegate.
///
/// The parent pointer is borrowed — not owned. If a `BackendRegistry` is
/// passed to the constructor, the FCB listens for
/// `backendInstanceUnregistered(parentBackendId)` and nulls its parent
/// pointer on receipt so subsequent reads/writes return clean failure
/// values rather than crash.
///
/// Composition layer only: the FCB is NOT a `BackendContribution` and has
/// no add-account UI. Consuming apps build instances at runtime and
/// register them via `BackendRegistry::registerBackendInstance` like any
/// other backend.
class FilteredCollectionBackend : public Kalburator::Sync::SyncBackend {
    Q_OBJECT
public:
    FilteredCollectionBackend(Kalburator::Sync::SyncBackend* parentBackend,
                              QString parentCollectionId,
                              QString virtualCollectionId,
                              Kalburator::Shape::RecordFilter filter,
                              Kalburator::Sync::BackendRegistry* registry = nullptr,
                              QString displayNameOverride = QString(),
                              QObject* parent = nullptr);

    QString backendType()  const override { return QStringLiteral("filtered-view"); }
    QString displayName()  const override;
    QString resourceId()   const override;
    bool    isAvailable()  const override;

    QList<Kalburator::Shape::Shape> nativeShapes() const override;
    Kalburator::Shape::Shape shapeFor(const QString& collectionId) const override;

    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo        collectionInfo(const QString& collectionId) override;

    bool    discoveredWritable(const QString& calendarId) const override;

    QList<Kalburator::Sync::BackendRecord>            loadRecords(const QString& collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord>    loadRecord(const QString& recordId)      override;
    QString createRecord(const QString& collectionId,
                         const Kalburator::Sync::BackendRecord& record) override;
    bool    updateRecord(const Kalburator::Sync::BackendRecord& record) override;
    bool    deleteRecord(const QString& recordId)            override;

    // Accessors for tests / consumers.
    QString parentCollectionId() const { return m_parentColId; }
    QString virtualCollectionId() const { return m_virtualColId; }
    const Kalburator::Shape::RecordFilter& filter() const { return m_filter; }

private:
    /// Compose the parent's `CollectionInfo` for `m_parentColId` into a
    /// CollectionInfo for the virtual collection: rewrites `id`, applies
    /// `displayName` override (or composes a default), inherits `color`
    /// (via CollectionInfo defaults) and `readOnly`.
    Kalburator::Sync::CollectionInfo composeCollectionInfo() const;

    /// Apply the filter's stamp semantics to a canon-JSON payload and
    /// return the rewritten bytes. Contains => append filter value to
    /// the property's array if absent (preserve order); Equals =>
    /// overwrite the property to the filter value (always).
    /// Returns the original bytes unchanged if the payload is not a
    /// JSON object (caller decides what to do with that).
    QByteArray stampFilterValue(const QByteArray& payload) const;

    /// Canonical JSON serialization of the filter value, suitable for
    /// embedding in resourceId(). Strings come back as JSON (`"Work"`);
    /// objects/arrays come back with sorted keys and no whitespace.
    static QByteArray canonJsonOfValue(const QVariant& value);

    /// Lowercase token for the filter op ("contains" / "equals").
    static QString opToken(Kalburator::Shape::RecordFilter::Op op);

    QString defaultComposedDisplayName(const QString& parentName) const;
    QString filterDescription() const;

    Kalburator::Sync::SyncBackend*   m_parent = nullptr;
    /// Captured at construction from `parentBackend->backendId()`. Compared
    /// against the id carried by `BackendRegistry::backendInstanceUnregistered`
    /// in the Task 7 hook so the FCB only reacts when its own parent goes
    /// away (not any other backend).
    QString                          m_parentBackendId;
    QString                          m_parentColId;
    QString                          m_virtualColId;
    Kalburator::Shape::RecordFilter  m_filter;
    QString                          m_displayNameOverride;
};

} // namespace Kalburator::Sinks
