#ifndef TRANSCODINGREGISTRY_H
#define TRANSCODINGREGISTRY_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <memory>
#include <vector>
#include <KCalendarCore/Incidence>

#include "propertytranscoder.h"

namespace Kalburator::Sync {

/**
 * @brief Central registry for property transcoders.
 *
 * TranscodingRegistry manages all registered PropertyTranscoder instances
 * and provides methods to transcode incidences between backend types.
 * It is a singleton that should be created early in application startup.
 *
 * Key design principles:
 * - Centralized: All transcoders registered in one place
 * - Automatic: CalendarManager uses registry transparently
 * - Extensible: New transcoders can be added without modifying core code
 *
 * Usage:
 * @code
 * // During app initialization
 * TranscodingRegistry::instance().registerTranscoder(
 *     std::make_unique<EffortTranscoder>());
 *
 * // During sync (handled by CalendarManager)
 * QStringList warnings = TranscodingRegistry::instance().transcodeIncidence(
 *     "orgmode", "caldav", incidence);
 * @endcode
 */
class TranscodingRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Get the singleton instance.
     */
    static TranscodingRegistry& instance();

    /**
     * @brief Register a property transcoder.
     *
     * The registry takes ownership of the transcoder.
     * @param transcoder Unique pointer to the transcoder
     */
    void registerTranscoder(std::unique_ptr<PropertyTranscoder> transcoder);

    /**
     * @brief Find transcoders applicable to a source->target pair.
     *
     * Returns pointers to registered transcoders that apply to this
     * backend conversion, sorted by priority (highest first).
     *
     * @param sourceType Source backend type
     * @param targetType Target backend type
     * @return List of applicable transcoders (not owned by caller)
     */
    QList<PropertyTranscoder*> findTranscoders(const QString &sourceType,
                                                const QString &targetType) const;

    /**
     * @brief Transcode an incidence from source to target backend type.
     *
     * Applies all applicable transcoders in priority order.
     * Returns a list of warning messages for any non-lossless conversions.
     *
     * @param sourceType Source backend type
     * @param targetType Target backend type
     * @param incidence The incidence to transcode (modified in place)
     * @return List of warning messages (empty if no issues)
     */
    QStringList transcodeIncidence(const QString &sourceType,
                                   const QString &targetType,
                                   KCalendarCore::Incidence::Ptr &incidence) const;

    /**
     * @brief Analyze potential data loss without transcoding.
     *
     * Returns warnings that would be generated if the incidence
     * were transcoded, without actually modifying it.
     *
     * @param sourceType Source backend type
     * @param targetType Target backend type
     * @param incidence The incidence to analyze
     * @return List of warning messages (empty if no issues)
     */
    QStringList analyzeTranscodingLoss(const QString &sourceType,
                                       const QString &targetType,
                                       const KCalendarCore::Incidence::Ptr &incidence) const;

    /**
     * @brief Check if any transcoders exist for a source->target pair.
     */
    bool hasTranscoders(const QString &sourceType, const QString &targetType) const;

    /**
     * @brief Get all registered transcoders.
     */
    QList<PropertyTranscoder*> allTranscoders() const;

    /**
     * @brief Clear all registered transcoders (mainly for testing).
     */
    void clear();

private:
    TranscodingRegistry();
    ~TranscodingRegistry() override = default;

    TranscodingRegistry(const TranscodingRegistry&) = delete;
    TranscodingRegistry& operator=(const TranscodingRegistry&) = delete;

    /**
     * @brief Register built-in default transcoders.
     */
    void registerDefaultTranscoders();

    /// All registered transcoders
    std::vector<std::unique_ptr<PropertyTranscoder>> m_transcoders;
};

} // namespace Kalburator::Sync

#endif // TRANSCODINGREGISTRY_H
