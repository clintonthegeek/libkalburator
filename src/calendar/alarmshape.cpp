#include "alarmshape.h"

#include <QDateTime>
#include <QString>

namespace Kalburator::Calendar {

QJsonObject alarmToJson(const KCalendarCore::Alarm::Ptr& alarm)
{
    // Verbatim from vtodocanonfields.cpp's W5 promote block (moved here by
    // IP.4 so VEVENT and VTODO share one implementation). See the header
    // comment for the row shape and the mutual-exclusivity rationale.
    QJsonObject a;
    a.insert(QStringLiteral("type"), int(alarm->type()));
    if (alarm->hasTime()) {
        a.insert(QStringLiteral("at"), alarm->time().toUTC().toString(Qt::ISODate));
    } else if (alarm->hasEndOffset()) {
        a.insert(QStringLiteral("offset"), alarm->endOffset().asSeconds());
        a.insert(QStringLiteral("related"), QStringLiteral("end"));
    } else {
        // default / hasStartOffset() — unchanged pre-W5 shape.
        a.insert(QStringLiteral("offset"), alarm->startOffset().asSeconds());
    }
    if (!alarm->text().isEmpty())
        a.insert(QStringLiteral("text"), alarm->text());
    // REPEAT/DURATION pairing (Open decision 3, probe-confirmed 2026-08-28):
    // KCalendarCore::Alarm::snoozeTime() has a nonzero CLASS DEFAULT
    // (5 seconds) even when no DURATION property was present in the source
    // at all — it is NOT zero, so "snoozeTime() != 0" cannot distinguish
    // "explicit DURATION" from "never set". There is no public API to
    // detect literal DURATION presence short of a raw-bytes VALARM scanner
    // (out of this item's scope — pre-existing, deliberately-scoped-out,
    // not reopened by IP.4). Promote therefore emits the pair whenever
    // repeatCount() > 0, trusting whatever snoozeTime() KCalendarCore
    // parsed (falling back to its 5s class default for an already-
    // malformed REPEAT-without-DURATION source) — the same "trust the
    // parsed accessor" posture this file already takes for the offset
    // field, with no raw-bytes cross-check.
    if (alarm->repeatCount() > 0) {
        a.insert(QStringLiteral("repeatCount"), alarm->repeatCount());
        a.insert(QStringLiteral("repeatIntervalSecs"), alarm->snoozeTime().asSeconds());
    }
    return a;
}

KCalendarCore::Alarm::Ptr alarmFromJson(const QJsonObject& row, KCalendarCore::Incidence* parent)
{
    // Verbatim from vtodocanonfields.cpp's W5 demote block, plus the O85
    // fix (see header comment for the decision + rationale): a
    // default-constructed KCalendarCore::Alarm has enabled()==false, and
    // RFC 5545 has no "disabled alarm" concept for a demoted alarm to lose
    // by always coming back enabled — so this always calls
    // setEnabled(true), unconditionally, for every row.
    KCalendarCore::Alarm::Ptr alarm(new KCalendarCore::Alarm(parent));
    const int typeInt = row.value(QStringLiteral("type")).toInt();
    alarm->setType(static_cast<KCalendarCore::Alarm::Type>(typeInt));

    if (row.contains(QStringLiteral("at"))) {
        const QDateTime dt = QDateTime::fromString(
            row.value(QStringLiteral("at")).toString(), Qt::ISODate);
        if (dt.isValid())
            alarm->setTime(dt);
    } else {
        const int offsetSecs = row.value(QStringLiteral("offset")).toInt();
        if (row.value(QStringLiteral("related")).toString() == QStringLiteral("end"))
            alarm->setEndOffset(KCalendarCore::Duration(offsetSecs));
        else
            alarm->setStartOffset(KCalendarCore::Duration(offsetSecs));
    }

    const QString text = row.value(QStringLiteral("text")).toString();
    if (!text.isEmpty())
        alarm->setText(text);

    // REPEAT/DURATION: only ever synthesize the pair when BOTH canon keys
    // are present — an unpaired REPEAT or DURATION is itself malformed per
    // RFC 5545 and must never be manufactured here.
    if (row.contains(QStringLiteral("repeatCount"))
        && row.contains(QStringLiteral("repeatIntervalSecs"))) {
        alarm->setRepeatCount(row.value(QStringLiteral("repeatCount")).toInt());
        alarm->setSnoozeTime(KCalendarCore::Duration(
            row.value(QStringLiteral("repeatIntervalSecs")).toInt()));
    }

    // O85: RFC 5545 has no "disabled alarm" wire representation; see the
    // header comment for the full argument.
    alarm->setEnabled(true);

    return alarm;
}

AlarmRowForm describeAlarmRow(const QJsonObject& row)
{
    if (row.contains(QStringLiteral("at")))
        return AlarmRowForm::Absolute;
    if (row.contains(QStringLiteral("offset"))) {
        return row.value(QStringLiteral("related")).toString() == QStringLiteral("end")
                   ? AlarmRowForm::EndRelative
                   : AlarmRowForm::StartRelative;
    }
    return AlarmRowForm::Malformed;
}

}  // namespace Kalburator::Calendar
