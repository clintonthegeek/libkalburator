// Upstream-behaviour probe (2026-09-02) — KCalendarCore/libical only.
//
// Separates OUR defects from the toolkit's. Everything here is measured
// WITHOUT libkalburator in the picture, so a finding it reproduces cannot be
// blamed on (or fixed in) our emitters. Build + run: ./run.sh
//
// Recorded baseline: kcalendarcore 6.29.0-1, Qt 6.11.1 (Manjaro, 2026-09-02).

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Todo>

#include <QCoreApplication>
#include <QTextStream>
#include <QTimeZone>

using namespace KCalendarCore;
static QTextStream out(stdout);

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ICalFormat f;

    out << "=== A. GEO round-trip (O86) — NO libkalburator involved ===\n";
    {
        Todo::Ptr t(new Todo);
        t->setUid(QStringLiteral("g1"));
        t->setSummary(QStringLiteral("s"));
        t->setGeoLatitude(1.5f);
        t->setGeoLongitude(2.5f);
        out << "  set lat=1.5 lon=2.5 -> hasGeo=" << t->hasGeo()
            << " geoLatitude()=" << t->geoLatitude()
            << " geoLongitude()=" << t->geoLongitude() << "   (accessors are fine)\n";
        for (const QString& line : f.toICalString(t).split(QLatin1Char('\n')))
            if (line.startsWith(QLatin1String("GEO:")))
                out << "  serialized: "
                    << QString::fromUtf8(line.trimmed().toUtf8().toPercentEncoding())
                    << "\n  ^ latitude slot carries the LONGITUDE; longitude slot is garbage.\n"
                       "    Percent-encoded because the raw bytes are not valid UTF-8.\n";
        Event::Ptr e(new Event);
        e->setUid(QStringLiteral("g2"));
        e->setGeoLatitude(1.5f);
        e->setGeoLongitude(2.5f);
        for (const QString& line : f.toICalString(e).split(QLatin1Char('\n')))
            if (line.startsWith(QLatin1String("GEO:")))
                out << "  Event is affected identically: "
                    << QString::fromUtf8(line.trimmed().toUtf8().toPercentEncoding()) << "\n";
    }

    out << "\n=== B. ATTENDEE parses fine — the audit's own false alarm ===\n";
    out << "    Recorded so nobody re-files it: libical REJECTS a single-label\n"
           "    mail domain and drops the whole property. Use example.com.\n";
    {
        const auto probe = [&](const char* label, const QByteArray& attendee) {
            const QByteArray s =
                "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//p//EN\r\nBEGIN:VEVENT\r\n"
                "UID:a1\r\nDTSTAMP:20260101T000000Z\r\nSUMMARY:S\r\n"
                "DTSTART:20260201T100000Z\r\n" + attendee + "\r\n"
                "END:VEVENT\r\nEND:VCALENDAR\r\n";
            const auto inc = f.fromString(QString::fromUtf8(s));
            out << "  " << label << " -> attendees=" << (inc ? inc->attendees().size() : -1) << "\n";
        };
        probe("ATTENDEE:mailto:a@x           (invalid domain)", "ATTENDEE:mailto:a@x");
        probe("ATTENDEE:mailto:a@example.com (valid)         ", "ATTENDEE:mailto:a@example.com");
    }

    out << "\n=== C. VALARM trigger forms + the enabled flag (O79 / O85) ===\n";
    {
        const QByteArray src =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//p//EN\r\nBEGIN:VEVENT\r\n"
            "UID:al1\r\nDTSTAMP:20260101T000000Z\r\nSUMMARY:S\r\n"
            "DTSTART:20260601T090000Z\r\nDTEND:20260601T100000Z\r\n"
            "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:rel-start\r\n"
            "TRIGGER:-PT15M\r\nEND:VALARM\r\n"
            "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:rel-end\r\n"
            "TRIGGER;RELATED=END:-PT5M\r\nEND:VALARM\r\n"
            "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:absolute\r\n"
            "TRIGGER;VALUE=DATE-TIME:20260531T080000Z\r\nEND:VALARM\r\n"
            "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:snooze\r\n"
            "TRIGGER:-PT30M\r\nREPEAT:3\r\nDURATION:PT5M\r\nEND:VALARM\r\n"
            "END:VEVENT\r\nEND:VCALENDAR\r\n";
        const auto inc = f.fromString(QString::fromUtf8(src));
        out << "  KCalendarCore parses all four forms correctly:\n";
        for (const auto& a : inc->alarms())
            out << "    '" << a->text() << "' enabled=" << a->enabled()
                << " hasStartOffset=" << a->hasStartOffset()
                << " hasEndOffset="   << a->hasEndOffset()
                << " hasTime="        << a->hasTime()
                << " repeat="         << a->repeatCount() << "\n";
        out << "  => O79 is OURS (eventcanonfields reads startOffset() unconditionally).\n";

        Alarm::Ptr fresh(new Alarm(inc.data()));
        out << "  Default-constructed Alarm::enabled() = " << fresh->enabled()
            << "   => O85 is OURS: no promote records enabled(), no demote calls\n"
               "     setEnabled(true), so every round-tripped alarm lands DISABLED\n"
               "     and serializes X-KDE-KCALCORE-ENABLED:FALSE.\n";
    }

    out << "\n=== D. Attendee X-UID is heap-address derived (O90) ===\n";
    {
        Event::Ptr e(new Event);
        e->setUid(QStringLiteral("r1"));
        e->setSummary(QStringLiteral("s"));
        e->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(9, 0), QTimeZone::utc()));
        e->addAttendee(Attendee(QStringLiteral("A"), QStringLiteral("a@example.com")));
        for (const QString& line : f.toICalString(e).split(QLatin1Char('\n')))
            if (line.contains(QLatin1String("X-UID")))
                out << "  " << line.trimmed() << "\n";
        out << "  ^ re-run this binary: the value changes per PROCESS.\n";
    }

    out.flush();
    return 0;
}
