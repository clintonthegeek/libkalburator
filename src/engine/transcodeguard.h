#pragma once

#include <QByteArray>

namespace Kalburator::Sync {

/// True when a non-empty record transcoded to empty bytes — the silent
/// data-loss signature. A legitimate transform never empties a record
/// (deletes are handled out-of-band), so callers must fail the mapping
/// loudly when this returns true (handoff req #6).
inline bool transcodeEmptiedRecord(const QByteArray& before, const QByteArray& after)
{
    return !before.isEmpty() && after.isEmpty();
}

}  // namespace Kalburator::Sync
