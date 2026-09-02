#pragma once

#include "propertycatalogue.h"

#include <KCalendarCore/Todo>
#include <QByteArray>
#include <QJsonObject>
#include <QList>

namespace Kalburator::Todo {

/// Map a parsed VTODO to canon JSON fields (NO envelope). `originalBytes` is the
/// source iCal, used to extract RRULE/RDATE/EXDATE verbatim (invariant 3).
QJsonObject todoFieldsToCanon(const KCalendarCore::Todo::Ptr& todo,
                              const QByteArray& originalBytes);

/// Build full VTODO iCal bytes from canon JSON (reads "uid" etc.).
QByteArray canonObjectToVtodoBytes(const QJsonObject& obj);

/// IP.3 — the top-level canon PropertyIds `todoFieldsToCanon` can produce
/// (excludes envelope keys `_canon`/`uid`/`providerExtras`, per
/// CanonEnvelope). Declared next to the emitter it describes. This emitter
/// is shared by two domains — `{todo,canon}` (Google Tasks / MS To-Do) and
/// `{calendar,canon}` VTODO (icalcanonstages.cpp calls todoFieldsToCanon
/// directly) — so both `makeTodoCanonCatalogue()` and
/// `makeCalendarCanonCatalogue()` consume this same list. See
/// docs/campaign/incidence-parity/PLAN.md IP.3.
QList<Kalburator::Shape::PropertyId> vtodoCanonContributedIds();

}  // namespace Kalburator::Todo
