#pragma once

#include "propertycatalogue.h"

#include <KCalendarCore/Event>
#include <QByteArray>
#include <QJsonObject>
#include <QList>

namespace Kalburator::Calendar {

/// Map a parsed VEVENT to canon JSON fields (NO envelope). `originalBytes` is the
/// source iCal, used to extract RRULE/RDATE/EXDATE verbatim (invariant 3).
QJsonObject eventFieldsToCanon(const KCalendarCore::Event::Ptr& event,
                               const QByteArray& originalBytes);

/// Build full VEVENT iCal bytes from canon JSON (reads "uid" etc.).
QByteArray canonObjectToEventBytes(const QJsonObject& obj);

/// IP.3 — the top-level canon PropertyIds `eventFieldsToCanon` can produce
/// (excludes envelope keys `_canon`/`uid`/`providerExtras`, per
/// CanonEnvelope). Declared next to the emitter it describes so the two
/// move together under one editor's eye. Consumed by
/// `makeCalendarCanonCatalogue()` (calendarcanonproperties.cpp) to build
/// its catalogue's id set structurally instead of by hand-transcription —
/// see docs/campaign/incidence-parity/PLAN.md IP.3.
QList<Kalburator::Shape::PropertyId> eventCanonContributedIds();

}  // namespace Kalburator::Calendar
