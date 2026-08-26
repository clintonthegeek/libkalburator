#include "kinddemuxbackend.h"

#include <QDebug>

namespace Kalburator::Sinks {

using Kalburator::Shape::Shape;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::ChangeDetection;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Sync::SyncBackendBase;

KindDemuxBackend::KindDemuxBackend(
        const QList<QPair<QString, SyncBackendBase*>>& routes,
        std::shared_ptr<SyncBackendBase> underlyingLifetime,
        QObject* parent)
    : SyncBackendBase(parent)
    , m_underlying(std::move(underlyingLifetime))
{
    for (const auto& r : routes) {
        Q_ASSERT(r.second != nullptr);
        m_routes.append(Route{r.first, r.second});
        // Borrowed routes still need an owner: adopt every child that isn't
        // the shared underlying transport (that one's lifetime is managed by
        // m_underlying and must not be reparented into one demux).
        if (r.second != m_underlying.get())
            r.second->setParent(this);
        // Re-emit child telemetry on this backend so consumers connected to
        // the registered (demux) instance still observe fetch/write lifecycle.
        connect(r.second, &SyncBackendBase::fetchStarted,
                this, &SyncBackendBase::fetchStarted);
        connect(r.second, &SyncBackendBase::fetchProgressChanged,
                this, &SyncBackendBase::fetchProgressChanged);
        connect(r.second, &SyncBackendBase::fetchFinished,
                this, &SyncBackendBase::fetchFinished);
        connect(r.second, &SyncBackendBase::writeStarted,
                this, &SyncBackendBase::writeStarted);
        connect(r.second, &SyncBackendBase::writeProgressChanged,
                this, &SyncBackendBase::writeProgressChanged);
        connect(r.second, &SyncBackendBase::syncCompleted,
                this, &SyncBackendBase::syncCompleted);
        connect(r.second, &SyncBackendBase::transcodingWarning,
                this, &SyncBackendBase::transcodingWarning);
    }
}

QString KindDemuxBackend::backendType() const
{
    return QStringLiteral("kind-demux");
}

QString KindDemuxBackend::displayName() const
{
    return QStringLiteral("kind-demux");
}

bool KindDemuxBackend::isAvailable() const
{
    if (m_underlying && !m_underlying->isAvailable())
        return false;
    for (const Route& r : m_routes) {
        if (!r.child->isAvailable())
            return false;
    }
    return !m_routes.isEmpty();
}

QList<Shape> KindDemuxBackend::nativeShapes() const
{
    return m_routes.isEmpty() ? QList<Shape>{}
                              : m_routes.first().child->nativeShapes();
}

Shape KindDemuxBackend::shapeFor(const QString& collectionId) const
{
    if (SyncBackendBase* c = childFor(collectionId))
        return c->shapeFor(collectionId);
    return Shape::Any();
}

bool KindDemuxBackend::discoveredWritable(const QString& collectionId) const
{
    if (SyncBackendBase* c = childFor(collectionId))
        return c->discoveredWritable(collectionId);
    return false;
}

int KindDemuxBackend::maxConcurrentOperations() const
{
    // The binding cap is the smallest non-zero cap among the distinct
    // children (they share one transport; the strictest view wins).
    int cap = 0;
    for (SyncBackendBase* c : distinctChildren()) {
        const int childCap = c->maxConcurrentOperations();
        if (childCap > 0 && (cap == 0 || childCap < cap))
            cap = childCap;
    }
    return cap;
}

SyncBackendBase* KindDemuxBackend::childFor(const QString& collectionId) const
{
    for (const Route& r : m_routes) {
        if (r.collectionId == collectionId)
            return r.child;
    }
    return nullptr;
}

QList<SyncBackendBase*> KindDemuxBackend::distinctChildren() const
{
    QList<SyncBackendBase*> out;
    for (const Route& r : m_routes) {
        if (!out.contains(r.child))
            out.append(r.child);
    }
    return out;
}

QList<CollectionInfo> KindDemuxBackend::availableCollections()
{
    // Ask each CHILD about its own routed collection id — never concat raw
    // availableCollections(): a direct route into a multi-collection parent
    // would leak sibling collections across the domain boundary.
    QList<CollectionInfo> out;
    for (const Route& r : m_routes) {
        const CollectionInfo info = r.child->collectionInfo(r.collectionId);
        if (!info.id.isEmpty())
            out.append(info);
    }
    return out;
}

CollectionInfo KindDemuxBackend::collectionInfo(const QString& collectionId)
{
    if (SyncBackendBase* c = childFor(collectionId))
        return c->collectionInfo(collectionId);
    return {};
}

QList<BackendRecord> KindDemuxBackend::loadRecords(const QString& collectionId)
{
    if (SyncBackendBase* c = childFor(collectionId))
        return c->loadRecords(collectionId);
    qWarning() << "KindDemuxBackend::loadRecords: no route for" << collectionId;
    return {};
}

std::optional<BackendRecord> KindDemuxBackend::loadRecord(const QString& recordId)
{
    // All children project the same underlying store, so first-hit trial in
    // route order is deterministic and sound: only the child whose filter
    // admits the record returns it.
    for (SyncBackendBase* c : distinctChildren()) {
        auto rec = c->loadRecord(recordId);
        if (rec.has_value())
            return rec;
    }
    return std::nullopt;
}

QString KindDemuxBackend::createRecord(const QString& collectionId,
                                       const BackendRecord& record)
{
    if (SyncBackendBase* c = childFor(collectionId))
        return c->createRecord(collectionId, record);
    qWarning() << "KindDemuxBackend::createRecord: no route for" << collectionId;
    return {};
}

bool KindDemuxBackend::updateRecord(const BackendRecord& record)
{
    for (SyncBackendBase* c : distinctChildren()) {
        if (c->updateRecord(record))
            return true;
    }
    return false;
}

bool KindDemuxBackend::deleteRecord(const QString& recordId)
{
    for (SyncBackendBase* c : distinctChildren()) {
        if (c->deleteRecord(recordId))
            return true;
    }
    return false;
}

QList<BackendRecord> KindDemuxBackend::modifiedSince(
        const QString& collectionId, const QDateTime& since)
{
    if (SyncBackendBase* c = childFor(collectionId))
        return c->modifiedSince(collectionId, since);
    return {};
}

QStringList KindDemuxBackend::deletedSince(
        const QString& collectionId, const QDateTime& since)
{
    if (SyncBackendBase* c = childFor(collectionId))
        return c->deletedSince(collectionId, since);
    return {};
}

// ---- Sync::ChangeDetection ----

ChangeDetection* KindDemuxBackend::childChangeDetection(SyncBackendBase* child) const
{
    return dynamic_cast<ChangeDetection*>(child);
}

QString KindDemuxBackend::collectionRevision(const QString& collectionId)
{
    SyncBackendBase* c = childFor(collectionId);
    ChangeDetection* cd = c ? childChangeDetection(c) : nullptr;
    return cd ? cd->collectionRevision(collectionId) : QString();
}

void KindDemuxBackend::collectionRevisionsAsync(
        const QStringList& collectionIds,
        std::function<void(QMap<QString, QString>)> done)
{
    // Group requested ids by child, ask each child once, merge. Every child
    // here projects the same transport collection per id, so answers cannot
    // disagree; ids are identity-mapped (views keep the parent's id).
    struct Pending {
        int remaining = 0;
        QMap<QString, QString> merged;
    };
    auto pending = std::make_shared<Pending>();
    auto finishIfDone = [pending, done = std::move(done)]() {
        if (pending->remaining == 0)
            done(pending->merged);
    };

    QHash<SyncBackendBase*, QStringList> byChild;
    for (const QString& id : collectionIds) {
        if (SyncBackendBase* c = childFor(id))
            byChild[c].append(id);
        // Unrouted ids: simply absent from the answer ("can't answer" →
        // engine treats as changed), matching ChangeDetection semantics.
    }

    pending->remaining = byChild.size();
    if (byChild.isEmpty()) {
        done({});
        return;
    }
    for (auto it = byChild.constBegin(); it != byChild.constEnd(); ++it) {
        ChangeDetection* cd = childChangeDetection(it.key());
        if (!cd) {
            // Child can't answer cheaply — its ids stay absent.
            --pending->remaining;
            continue;
        }
        cd->collectionRevisionsAsync(it.value(),
            [pending, finishIfDone](QMap<QString, QString> revs) {
                for (auto rit = revs.constBegin(); rit != revs.constEnd(); ++rit)
                    pending->merged.insert(rit.key(), rit.value());
                --pending->remaining;
                finishIfDone();
            });
    }
    finishIfDone();
}

QString KindDemuxBackend::cachedCollectionRevision(const QString& collectionId) const
{
    SyncBackendBase* c = childFor(collectionId);
    ChangeDetection* cd = c ? childChangeDetection(c) : nullptr;
    return cd ? cd->cachedCollectionRevision(collectionId) : QString();
}

bool KindDemuxBackend::persistsCollectionRevisions() const
{
    // Conservative AND: if any routed child can't persist revisions, cached
    // tokens from the demux as a whole must not be trusted across restarts.
    const QList<SyncBackendBase*> children = distinctChildren();
    if (children.isEmpty())
        return true;
    for (SyncBackendBase* c : children) {
        ChangeDetection* cd = childChangeDetection(c);
        if (cd && !cd->persistsCollectionRevisions())
            return false;
    }
    return true;
}

} // namespace Kalburator::Sinks
