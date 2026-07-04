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

}  // namespace Kalburator::Calendar
