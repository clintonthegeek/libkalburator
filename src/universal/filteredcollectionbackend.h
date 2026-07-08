#pragma once

#include <QString>

#include "syncbackendbase.h"       // Kalburator::Sync::SyncBackendBase (neutral base)
#include "changedetection.h"      // Kalburator::Sync::ChangeDetection
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
///
/// Caller MUST pass the same `parentBackendId` that was used to register
/// the parent in `BackendRegistry`; the default `backendId()` for
/// production `SyncBackend`s is not unique.
class FilteredCollectionBackend : public Kalburator::Sync::SyncBackendBase,
                                  public Kalburator::Sync::ChangeDetection {
    Q_OBJECT
public:
    FilteredCollectionBackend(Kalburator::Sync::SyncBackendBase* parentBackend,
                              QString parentBackendId,
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

    bool    discoveredWritable(const QString& collectionId) const override;

    QList<Kalburator::Sync::BackendRecord>            loadRecords(const QString& collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord>    loadRecord(const QString& recordId)      override;
    QString createRecord(const QString& collectionId,
                         const Kalburator::Sync::BackendRecord& record) override;
    bool    updateRecord(const Kalburator::Sync::BackendRecord& record) override;
    bool    deleteRecord(const QString& recordId)            override;

    // ---- Sync::ChangeDetection ----
    // A filtered view changes iff its parent collection changes, so the
    // conservative + correct token is the parent's revision for
    // m_parentColId. Requires the parent to implement ChangeDetection
    // (satisfied when the parent is the sqlite hub); otherwise returns empty
    // ("can't answer" → engine treats as changed, current behavior).
    QString collectionRevision(const QString& collectionId) override;
    // E5.2 / audit B7 (amendment A6): forward the async fresh-revision query to
    // the parent's ChangeDetection so a filtered view over an async backend
    // (e.g. CalDAV) never funnels through the default plural loop's synchronous,
    // nested-loop singular query on the backend thread.
    void    collectionRevisionsAsync(
        const QStringList& collectionIds,
        std::function<void(QMap<QString, QString>)> done) override;
    QString cachedCollectionRevision(const QString& collectionId) const override;
    bool    persistsCollectionRevisions() const override;

    // Accessors for tests / consumers.
    QString parentCollectionId() const { return m_parentColId; }
    QString virtualCollectionId() const { return m_virtualColId; }
    const Kalburator::Shape::RecordFilter& filter() const { return m_filter; }

private:
    /// The parent's Sync::ChangeDetection interface, or null if the parent is
    /// gone (post-unregister) or doesn't implement it. Centralizes the cross-
    /// cast the four ChangeDetection overrides share.
    Kalburator::Sync::ChangeDetection* parentChangeDetection() const
    { return dynamic_cast<Kalburator::Sync::ChangeDetection*>(m_parent); }

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

    Kalburator::Sync::SyncBackendBase*   m_parent = nullptr;
    /// The registry key under which the parent backend was registered via
    /// `BackendRegistry::registerBackendInstance(id, ...)`. Compared against
    /// the id emitted by `BackendRegistry::backendInstanceUnregistered` so
    /// the FCB only nulls its parent pointer when its own parent goes away.
    /// MUST be the same string the consumer passes to
    /// `registerBackendInstance` — `parentBackend->backendId()` is NOT a
    /// reliable substitute (the default impl returns `backendType()`).
    QString                          m_parentBackendId;
    QString                          m_parentColId;
    QString                          m_virtualColId;
    Kalburator::Shape::RecordFilter  m_filter;
    QString                          m_displayNameOverride;
};

} // namespace Kalburator::Sinks
