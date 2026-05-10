#pragma once

#include <QObject>

#include "domainplugin.h"

namespace Kalburator::Calendar {

/// Phase K.4: CalendarDomainPlugin gains a QObject base (in
/// addition to the abstract DomainPlugin) so that K.5+ can route
/// calendar-typed signals through the plugin instead of through the
/// SyncBackend. No K.4 user yet — this just establishes the
/// foundation. The plugin is still stored as
/// `std::shared_ptr<DomainPlugin>` in the DomainRegistry; the QObject
/// half is dormant until K.5 wires it up.
class CalendarDomainPlugin : public QObject,
                                 public Kalburator::Shape::DomainPlugin {
    Q_OBJECT

public:
    explicit CalendarDomainPlugin(QObject *parent = nullptr);

    Kalburator::Shape::DomainId domain() const override;
    Kalburator::Shape::Shape canonicalShape() const override;
    QList<Kalburator::Shape::Shape> peerShapes() const override;
    Kalburator::Shape::PropertyCatalogue canonicalCatalogue() const override;
    Kalburator::Shape::PropertyCatalogue catalogueFor(const Kalburator::Shape::Shape&) const override;
    std::unique_ptr<Kalburator::Shape::RecordDiffer> createCanonicalDiffer() const override;
    std::unique_ptr<Kalburator::Shape::RecordMerger> createCanonicalMerger() const override;
    void registerEdges(Kalburator::Shape::TransformationRegistry& registry) override;
    int richnessRank(const Kalburator::Shape::Shape&) const override;

    /// Phase Ia.5 Task 5: calendar gets its own IRecordWriter that
    /// drives the existing SyncTransaction machinery. The engine
    /// (Task 13) is responsible for calling
    /// `CalendarPluginWriter::setCollection()` on the returned writer
    /// before invoking `apply()`.
    std::unique_ptr<Kalburator::Shape::RecordWriter> createWriter(
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

    /// Phase K.5 Task 6: keys the engine should persist via
    /// Storage::BaselineStore::setCollectionBaseline.
    QStringList baselineProperties() const override;
};

} // namespace Kalburator::Calendar
