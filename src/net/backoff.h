#pragma once

// B2C P0 — transient-failure retry policy shared by transports.
//
// Idempotent reads may be retried on network errors and 502/503/504.
// Writes are NEVER auto-retried by the transport layer (non-idempotent;
// the backend layer owns write semantics).

#include <QDateTime>

namespace Kalburator::Net {

/// True when a request with this outcome may be retried.
inline bool isTransientFailure(int httpStatus, bool networkError)
{
    return networkError
        || httpStatus == 502 || httpStatus == 503 || httpStatus == 504;
}

/// Delay before retry `attempt` (1-based). Honors a server-provided
/// Retry-After hint (milliseconds; pass <= 0 when absent). Exponential
/// base 1s doubling, capped at 30s.
inline int retryDelayMsecs(int attempt, int retryAfterMs = -1)
{
    if (retryAfterMs > 0)
        return qMin(retryAfterMs, 30000);
    qint64 delay = 1000ll << qMax(0, qMin(attempt - 1, 5));
    return static_cast<int>(qMin(delay, 30000ll));
}

} // namespace Kalburator::Net
