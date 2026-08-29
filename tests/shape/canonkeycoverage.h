#pragma once

// IP.1 (incidence-parity campaign) — the catalogue/emitter coverage gate.
//
// A canon catalogue (e.g. calendarcanonproperties.cpp) and the emitter(s)
// that produce canon JSON for its domain (e.g. the shared VTODO emitter at
// src/todo/vtodocanonfields.cpp) are two independent sources of truth about
// the same key set, with nothing enforcing agreement between them (O78 —
// see docs/campaign/FINDINGS.md and docs/campaign/incidence-parity/PLAN.md
// IP.1). This header computes the disagreement AT RUNTIME from the two real
// sources — the top-level keys a promoted canon object actually carries,
// and the catalogue's declared PropertyId set — rather than hand-listing
// either side. Do not add a hand-maintained key list next to this helper;
// that is the exact regression IP.1 exists to delete
// (tests/calendar/tst_calendar_kind_dispatch.cpp's now-removed
// catalogueIncludesTodoAndJournalFields() was that regression, pinned live
// for years by nobody updating its four hardcoded keys).
//
// Deliberately domain-neutral (calendar, todo, and contacts all use it) and
// deliberately NOT calendar-specific, even though the first caller lives in
// tests/calendar/ — see the IP.1 return receipt for why this file sits
// under tests/shape/ rather than tests/calendar/calendar_test_helpers.h.

#include "canonenvelope.h"
#include "propertycatalogue.h"

#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

namespace Kalburator::TestSupport {

/// Returns the top-level keys of `obj` that are NOT declared in
/// `catalogueIds`, after excluding the envelope keys (_canon, uid,
/// providerExtras — read from CanonEnvelope, never hardcoded here). Sorted
/// for a deterministic, readable failure message.
inline QStringList undeclaredCanonKeys(
    const QJsonObject& obj,
    const QList<Kalburator::Shape::PropertyId>& catalogueIds)
{
    QSet<QString> declared;
    declared.reserve(catalogueIds.size());
    for (const auto& id : catalogueIds)
        declared.insert(id.toString());

    QSet<QString> envelopeKeys;
    envelopeKeys.insert(Kalburator::Shape::CanonEnvelope::canonKey());
    envelopeKeys.insert(Kalburator::Shape::CanonEnvelope::uidKey());
    envelopeKeys.insert(Kalburator::Shape::CanonEnvelope::providerExtrasKey());

    QStringList offending;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QString& key = it.key();
        if (envelopeKeys.contains(key))
            continue;
        if (!declared.contains(key))
            offending.append(key);
    }
    offending.sort();
    return offending;
}

/// Asserts that every top-level key of `obj` (minus the envelope keys) is
/// declared in `catalogueIds`. On failure, names every offending key by
/// name in the QVERIFY2 message, tagged with `label` (e.g.
/// "(calendar, vtodo)") so a multi-pair test file's failure output says
/// which pair broke.
///
/// May be called from a QTest slot OR from a plain helper function called
/// by a slot — QVERIFY2's `return` only unwinds the function it executes
/// in, and a preceding QEXPECT_FAIL (set by the caller, in the same slot,
/// immediately before calling this) still applies to the very next
/// QTest comparison this function performs, regardless of the call stack.
inline void verifyCanonKeysDeclared(
    const QJsonObject& obj,
    const QList<Kalburator::Shape::PropertyId>& catalogueIds,
    const QString& label)
{
    const QStringList offending = undeclaredCanonKeys(obj, catalogueIds);
    const QString msg = QStringLiteral("%1: catalogue does not declare emitted key(s): %2")
                             .arg(label, offending.join(QStringLiteral(", ")));
    QVERIFY2(offending.isEmpty(), qPrintable(msg));
}

}  // namespace Kalburator::TestSupport
