#pragma once

#include "lossprofile.h"

#include <KCalendarCore/Journal>
#include <QByteArray>
#include <QJsonObject>

namespace Kalburator::Calendar {

/// Map a parsed VJOURNAL to canon JSON fields (NO envelope). `originalBytes` is
/// the source iCal, used to preserve unmapped X- properties verbatim.
QJsonObject journalFieldsToCanon(const KCalendarCore::Journal::Ptr& journal,
                                 const QByteArray& originalBytes);

/// Build full VJOURNAL iCal bytes from canon JSON (reads "uid" etc.).
QByteArray canonObjectToJournalBytes(const QJsonObject& obj);

/// LossProfile for the canon → vjournal demote direction.
Kalburator::Shape::LossProfile canonToVjournalLoss();

/// IP.3 — the top-level canon PropertyIds `journalFieldsToCanon` can
/// produce (excludes envelope keys `_canon`/`uid`/`providerExtras`, per
/// CanonEnvelope). Declared next to the emitter it describes — see
/// docs/campaign/incidence-parity/PLAN.md IP.3.
QList<Kalburator::Shape::PropertyId> journalCanonContributedIds();

}  // namespace Kalburator::Calendar
