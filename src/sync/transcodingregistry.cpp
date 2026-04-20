#include "transcodingregistry.h"

#include <QDebug>
#include <algorithm>

// Include built-in transcoders
#include "rruletranscoder.h"

TranscodingRegistry& TranscodingRegistry::instance()
{
    static TranscodingRegistry s_instance;
    return s_instance;
}

TranscodingRegistry::TranscodingRegistry()
    : QObject(nullptr)
{
    registerDefaultTranscoders();
    qDebug() << "TranscodingRegistry: Initialized with"
             << m_transcoders.size() << "default transcoders";
}

void TranscodingRegistry::registerDefaultTranscoders()
{
    // Complex RRULE simplification transcoder
    registerTranscoder(std::make_unique<RRuleTranscoder>());
}

void TranscodingRegistry::registerTranscoder(std::unique_ptr<PropertyTranscoder> transcoder)
{
    if (!transcoder) {
        qWarning() << "TranscodingRegistry: Cannot register null transcoder";
        return;
    }

    qDebug() << "TranscodingRegistry: Registered transcoder for"
             << transcoder->propertyName()
             << "(" << transcoder->sourceBackendType()
             << "->" << transcoder->targetBackendType() << ")";

    m_transcoders.push_back(std::move(transcoder));
}

QList<PropertyTranscoder*> TranscodingRegistry::findTranscoders(
    const QString &sourceType,
    const QString &targetType) const
{
    QList<PropertyTranscoder*> result;

    for (const auto &transcoder : m_transcoders) {
        if (transcoder->appliesTo(sourceType, targetType)) {
            result.append(transcoder.get());
        }
    }

    // Sort by priority (highest first)
    std::sort(result.begin(), result.end(),
              [](PropertyTranscoder *a, PropertyTranscoder *b) {
        return a->priority() > b->priority();
    });

    return result;
}

QStringList TranscodingRegistry::transcodeIncidence(
    const QString &sourceType,
    const QString &targetType,
    KCalendarCore::Incidence::Ptr &incidence) const
{
    QStringList warnings;

    if (!incidence) {
        return warnings;
    }

    // Same backend type, no transcoding needed
    if (sourceType == targetType) {
        return warnings;
    }

    auto transcoders = findTranscoders(sourceType, targetType);

    for (auto *transcoder : transcoders) {
        if (transcoder->transcode(incidence)) {
            // Transcoding was applied, check for warnings
            if (transcoder->fidelity() != TranscodingFidelity::Lossless) {
                warnings.append(transcoder->description());
            }
        }
    }

    return warnings;
}

QStringList TranscodingRegistry::analyzeTranscodingLoss(
    const QString &sourceType,
    const QString &targetType,
    const KCalendarCore::Incidence::Ptr &incidence) const
{
    QStringList warnings;

    if (!incidence) {
        return warnings;
    }

    // Same backend type, no transcoding needed
    if (sourceType == targetType) {
        return warnings;
    }

    // Clone the incidence for analysis (don't modify original)
    auto clone = KCalendarCore::Incidence::Ptr(incidence->clone());

    auto transcoders = findTranscoders(sourceType, targetType);

    for (auto *transcoder : transcoders) {
        // Try transcoding on clone to see if it applies
        if (transcoder->transcode(clone)) {
            if (transcoder->fidelity() != TranscodingFidelity::Lossless) {
                warnings.append(transcoder->description());
            }
        }
    }

    return warnings;
}

bool TranscodingRegistry::hasTranscoders(const QString &sourceType,
                                         const QString &targetType) const
{
    return !findTranscoders(sourceType, targetType).isEmpty();
}

QList<PropertyTranscoder*> TranscodingRegistry::allTranscoders() const
{
    QList<PropertyTranscoder*> result;
    for (const auto &transcoder : m_transcoders) {
        result.append(transcoder.get());
    }
    return result;
}

void TranscodingRegistry::clear()
{
    m_transcoders.clear();
    qDebug() << "TranscodingRegistry: Cleared all transcoders";
}
