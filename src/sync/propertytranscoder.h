#ifndef PROPERTYTRANSCODER_H
#define PROPERTYTRANSCODER_H

#include <QString>
#include <KCalendarCore/Incidence>

namespace Kalburator::Sync {

/**
 * @brief Fidelity level for property transcoding.
 *
 * Describes how much information is preserved when transcoding
 * a property from one backend format to another.
 */
enum class TranscodingFidelity {
    Lossless,    ///< No data loss, fully reversible
    Reversible,  ///< Can be reversed with X-property preservation
    Lossy,       ///< Some data lost, cannot be fully recovered
    Unsupported  ///< Property cannot be transcoded, will be dropped
};

/**
 * @brief Abstract base class for property transcoders.
 *
 * PropertyTranscoder handles conversion of a specific property between
 * backend formats. Each transcoder is registered with the TranscodingRegistry
 * and invoked automatically when syncing between backends with different
 * capabilities.
 *
 * Key design principles:
 * - Single responsibility: one transcoder per property/direction
 * - Composition: multiple transcoders can chain for complex conversions
 * - Transparency: users are warned about lossy conversions
 *
 * Example: RRULE transcoder for complex recurrence rules
 * @code
 * class RRuleTranscoder : public PropertyTranscoder {
 * public:
 *     QString propertyName() const override { return "RRULE"; }
 *     QString sourceBackendType() const override { return "*"; }
 *     QString targetBackendType() const override { return "*"; }
 *     TranscodingFidelity fidelity() const override { return TranscodingFidelity::Lossy; }
 *
 *     bool transcode(KCalendarCore::Incidence::Ptr &incidence) const override {
 *         // Simplify complex RRULE for backends with limited support
 *         // ...
 *         return true;
 *     }
 * };
 * @endcode
 */
class PropertyTranscoder
{
public:
    virtual ~PropertyTranscoder() = default;

    /**
     * @brief Property name this transcoder handles.
     *
     * This is used for matching and description purposes.
     * Examples: "EFFORT", "RRULE", "PRIORITY"
     */
    virtual QString propertyName() const = 0;

    /**
     * @brief Source backend type this transcoder applies to.
     *
     * Use "*" for any source backend.
     * Examples: "orgmode", "local", "caldav", "*"
     */
    virtual QString sourceBackendType() const = 0;

    /**
     * @brief Target backend type this transcoder applies to.
     *
     * Use "*" for any target backend.
     * Examples: "orgmode", "local", "caldav", "*"
     */
    virtual QString targetBackendType() const = 0;

    /**
     * @brief Fidelity level of this transcoding operation.
     *
     * - Lossless: No data loss
     * - Reversible: Can be undone (data preserved in X-properties)
     * - Lossy: Some data lost permanently
     * - Unsupported: Property will be dropped
     */
    virtual TranscodingFidelity fidelity() const = 0;

    /**
     * @brief Transcode the property on the given incidence.
     *
     * The incidence is modified in place. Returns true if any
     * changes were made, false if the transcoder didn't apply.
     *
     * @param incidence The incidence to modify
     * @return true if transcoding was applied
     */
    virtual bool transcode(KCalendarCore::Incidence::Ptr &incidence) const = 0;

    /**
     * @brief Human-readable description of what this transcoder does.
     *
     * Used for warning messages shown to users before lossy operations.
     * Example: "Preserves org-mode EFFORT as X-ORG-EFFORT custom property"
     */
    virtual QString description() const = 0;

    /**
     * @brief Check if this transcoder applies to a specific source->target pair.
     *
     * Default implementation checks sourceBackendType() and targetBackendType()
     * with "*" wildcard support.
     */
    virtual bool appliesTo(const QString &source, const QString &target) const;

    /**
     * @brief Priority for ordering transcoders (higher = runs first).
     *
     * Default is 0. Use higher values for transcoders that must run
     * before others (e.g., a transcoder that normalizes data before
     * other transcoders process it).
     */
    virtual int priority() const { return 0; }
};

// Qt metatype declaration

} // namespace Kalburator::Sync

Q_DECLARE_METATYPE(Kalburator::Sync::TranscodingFidelity)

#endif // PROPERTYTRANSCODER_H
