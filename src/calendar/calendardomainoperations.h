#pragma once
#include <kalburator/shape/domainoperations.h>

namespace Kalburator::Calendar {

class CalendarDomainOperations : public Kalburator::Shape::DomainOperations {
public:
    Kalburator::Shape::DomainId targetDomain() const override;

    std::unique_ptr<Kalburator::Shape::RecordWriter> createWriter(
        Kalburator::Sync::SyncBackendBase *backend) const override;

    QVariantMap collectionProperties(
        Kalburator::Sync::SyncBackendBase *backend,
        const QString &collectionId) const override;

    void applyCollectionProperties(
        Kalburator::Sync::SyncBackendBase *backend,
        const QString &collectionId,
        const QVariantMap &props,
        std::function<void(bool, const QString &)> completed = {}) const override;
};

} // namespace Kalburator::Calendar
