#pragma once

#include "domainplugin.h"

namespace Kalburator::Calendar {

class KalburatorDomainCalendar : public Kalburator::Shape::DomainPlugin {
public:
    Kalburator::Shape::DomainId domain() const override;
    Kalburator::Shape::Shape canonicalShape() const override;
    QList<Kalburator::Shape::Shape> peerShapes() const override;
    Kalburator::Shape::PropertyCatalogue canonicalCatalogue() const override;
    Kalburator::Shape::PropertyCatalogue catalogueFor(const Kalburator::Shape::Shape&) const override;
    std::unique_ptr<Kalburator::Shape::IRecordDiffer> createCanonicalDiffer() const override;
    std::unique_ptr<Kalburator::Shape::IRecordMerger> createCanonicalMerger() const override;
    void registerEdges(Kalburator::Shape::TransformationRegistry& registry) override;
    int richnessRank(const Kalburator::Shape::Shape&) const override;

    /// Phase Ia.5 Task 5: calendar gets its own IRecordWriter that
    /// drives the existing SyncTransaction machinery. The engine
    /// (Task 13) is responsible for calling
    /// `CalendarPluginWriter::setCollection()` on the returned writer
    /// before invoking `apply()`.
    std::unique_ptr<Kalburator::Shape::IRecordWriter> createWriter(
        Kalburator::Sync::SyncBackend *backend) const override;

    /// Phase Ia.5 Task 6: read calendar color + description from the
    /// backend. Returns a map with "color" (QColor) and/or
    /// "description" (QString) when set; omits keys whose values are
    /// unset (invalid color, empty description).
    QVariantMap collectionProperties(
        Kalburator::Sync::SyncBackend *backend,
        const QString &collectionId) const override;

    /// Phase Ia.5 Task 6: apply calendar color + description back to
    /// the backend via SyncBackend::updateCalendar.
    void applyCollectionProperties(
        Kalburator::Sync::SyncBackend *backend,
        const QString &collectionId,
        const QVariantMap &props) const override;
};

} // namespace Kalburator::Calendar
