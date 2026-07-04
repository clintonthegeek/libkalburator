#ifndef KALBURATOR_ENGINE_PERRECORDDIFF_H
#define KALBURATOR_ENGINE_PERRECORDDIFF_H

#include "backendrecord.h"
#include "baselineentry.h"
#include "enginediff.h"
#include "shape.h"

#include <QList>

namespace Kalburator::Shape { class RecordDiffer; }

namespace Kalburator::Engine {

/// Phase N.1: per-record diff over canonical records. Consults the
/// caller-provided RecordDiffer for equality. Produces an EngineDiff
/// with toSource/toTarget op lists; conflicts land in toTarget.
///
/// Replaces the Phase Ia.5 transitional helper `blobBatchDiff`.
///
/// Phase B4 (N2 fix): `baseline` carries a per-side hash pair per record
/// (see BaselineEntry) instead of one hash shared by both sides — a
/// source record is compared against its own `sourceHash`, a target
/// record against its own `targetHash`, never against the other side's
/// native bytes. The differ is borrowed; the caller retains ownership.
EngineDiff perRecordDiff(const QList<Kalburator::Sync::BackendRecord>& source,
                         const QList<Kalburator::Sync::BackendRecord>& target,
                         const QList<BaselineEntry>& baseline,
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
