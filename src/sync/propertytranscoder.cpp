#include "propertytranscoder.h"

bool PropertyTranscoder::appliesTo(const QString &source, const QString &target) const
{
    bool sourceMatches = (sourceBackendType() == QLatin1String("*") ||
                          sourceBackendType() == source);
    bool targetMatches = (targetBackendType() == QLatin1String("*") ||
                          targetBackendType() == target);
    return sourceMatches && targetMatches;
}
