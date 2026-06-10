#ifndef KALBURATOR_ICALCODEC_H
#define KALBURATOR_ICALCODEC_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QTimeZone>

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Incidence>
#include <KCalendarCore/MemoryCalendar>

namespace Kalburator::Sync {

// Incidence <-> iCal codec via a throwaway in-memory calendar.
//
// Extracted from remotecalendarbackend.cpp's anonymous namespace (Plan 7 T4)
// and shared with LocalBackend in Plan 7b T4 — every calendar backend used to
// hand-roll this temp-calendar dance (several still do; see FINDINGS). Free
// functions with no captures, safe to call from async job callbacks.

inline QByteArray icalFromIncidence(const KCalendarCore::Incidence::Ptr &inc)
{
    KCalendarCore::Calendar::Ptr tmpCal(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
    tmpCal->addIncidence(inc);
    KCalendarCore::ICalFormat format;
    return format.toString(tmpCal).toUtf8();
}

/// Empty on parse failure (or on a VCALENDAR with no incidences). The
/// returned shared pointers stay valid after the temporary calendar dies.
inline QList<KCalendarCore::Incidence::Ptr> incidencesFromIcal(const QString &ical)
{
    KCalendarCore::Calendar::Ptr tmpCal(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
    KCalendarCore::ICalFormat format;
    if (!format.fromString(tmpCal, ical)) {
        return {};
    }
    return tmpCal->incidences();
}

/// Raw-bytes variant (ICalFormat::fromRawString handles encoding sniffing).
inline QList<KCalendarCore::Incidence::Ptr> incidencesFromIcal(const QByteArray &raw)
{
    KCalendarCore::Calendar::Ptr tmpCal(
        new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
    KCalendarCore::ICalFormat format;
    if (!format.fromRawString(tmpCal, raw)) {
        return {};
    }
    return tmpCal->incidences();
}

} // namespace Kalburator::Sync

#endif // KALBURATOR_ICALCODEC_H
