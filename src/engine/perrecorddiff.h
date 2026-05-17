#ifndef KALBURATOR_ENGINE_PERRECORDDIFF_H
#define KALBURATOR_ENGINE_PERRECORDDIFF_H

#include "backendrecord.h"
#include "enginediff.h"
#include "shape.h"

#include <QList>

namespace Kalburator::Shape { class RecordDiffer; }

namespace Kalburator::Engine {

/// Phase N.1: per-record diff over canonical records. Consults the
/// caller-provided RecordDiffer for equality. Produces an EngineDiff
/// with toSource/toTarget op lists; conflicts land in toTarget.
///
/// Replaces the Phase Ia.5 transitional helper `blobBatchDiff`. The
/// differ is borrowed; the caller retains ownership.
EngineDiff perRecordDiff(const QList<Kalburator::Sync::BackendRecord>& source,
                         const QList<Kalburator::Sync::BackendRecord>& target,
                         const QList<Kalburator::Sync::BackendRecord>& baseline,
                         const Kalburator::Shape::Shape& canonical,
                         const Kalburator::Shape::RecordDiffer& differ);

/// Phase N.1: lifted from `blobbatchdiff.cpp`'s anonymous namespace.
/// Mirror semantics for unidirectional sync — push source records to
/// target, delete target-only records.
EngineMerge mergeMirrorAToB(const EngineDiff& diff);

/// Mirror semantics for unidirectional sync (reverse direction).
EngineMerge mergeMirrorBToA(const EngineDiff& diff);

} // namespace Kalburator::Engine

#endif // KALBURATOR_ENGINE_PERRECORDDIFF_H
