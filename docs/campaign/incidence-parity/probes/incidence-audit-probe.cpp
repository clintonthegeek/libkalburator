// Incidence-parity pre-flight audit probe (2026-09-02).
//
// Reproduces, against the REAL library, every claim in
// docs/campaign/incidence-parity/2026-09-02-preflight-audit.md.
// Build + run: ./run.sh  (from this directory).
//
// This file exists so no future agent has to re-derive the evidence. If you
// change an emitter, a catalogue, or the merger, re-run this and diff the
// output against the audit doc's recorded baseline. Deliberately NOT a
// QTest slot: it is an evidence instrument, not a gate. The gates it argues
// for are IP.8's (see PLAN.md Amendment 1).

#include "icalcanonstages.h"
#include "calendarcanonproperties.h"
#include "todocanonproperties.h"
#include "canonenvelope.h"
#include "canonjsonmerger.h"
#include "canonicalrecord.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QTextStream>

using namespace Kalburator;
using namespace Kalburator::Shape;

static QTextStream out(stdout);

// --- fixtures: maximal, RFC 5545-conformant components ---------------------
// Emails MUST use a multi-label domain: libical rejects "a@x" and drops the
// whole ATTENDEE property, which will make a naive probe report a phantom
// attendee bug. (Cost one round of this audit; recorded so it costs nobody
// else one.)

static const char* kEvent =
"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//probe//EN\r\nBEGIN:VEVENT\r\n"
"UID:ev-1\r\nDTSTAMP:20260101T000000Z\r\nCREATED:20260101T000000Z\r\n"
"LAST-MODIFIED:20260102T000000Z\r\nSEQUENCE:3\r\nSUMMARY:Ev\r\nDESCRIPTION:D\r\n"
"LOCATION:L\r\nSTATUS:CONFIRMED\r\nCLASS:PRIVATE\r\nTRANSP:OPAQUE\r\n"
"DTSTART:20260201T100000Z\r\nDTEND:20260201T110000Z\r\nPRIORITY:5\r\n"
"RRULE:FREQ=DAILY;COUNT=3\r\nCATEGORIES:a,b\r\nURL:http://example.com/e\r\n"
"COLOR:red\r\nGEO:1.5;2.5\r\nRELATED-TO:parent-1\r\n"
"ORGANIZER:mailto:o@example.com\r\nATTENDEE;CN=A:mailto:a@example.com\r\n"
"ATTACH:http://example.com/f.pdf\r\n"
"BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT15M\r\nDESCRIPTION:r\r\nEND:VALARM\r\n"
"X-CUSTOM-THING:zz\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

static const char* kTodo =
"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//probe//EN\r\nBEGIN:VTODO\r\n"
"UID:td-1\r\nDTSTAMP:20260101T000000Z\r\nCREATED:20260101T000000Z\r\n"
"LAST-MODIFIED:20260102T000000Z\r\nSEQUENCE:4\r\nSUMMARY:Td\r\nDESCRIPTION:D\r\n"
"LOCATION:L\r\nSTATUS:IN-PROCESS\r\nCLASS:CONFIDENTIAL\r\nPERCENT-COMPLETE:40\r\n"
"DTSTART:20260201T100000Z\r\nDUE:20260205T100000Z\r\nPRIORITY:2\r\n"
"RRULE:FREQ=WEEKLY;COUNT=5\r\nCATEGORIES:a,b\r\nURL:http://example.com/t\r\n"
"COLOR:blue\r\nGEO:1.5;2.5\r\nRELATED-TO:parent-1\r\n"
"ORGANIZER:mailto:o@example.com\r\nATTENDEE;CN=B:mailto:b@example.com\r\n"
"ATTACH:http://example.com/f.pdf\r\n"
"BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT30M\r\nDESCRIPTION:r\r\nEND:VALARM\r\n"
"X-CANON-SERIES-SPLIT-OF:old-uid\r\nX-CUSTOM-THING:qq\r\n"
"END:VTODO\r\nEND:VCALENDAR\r\n";

static const char* kJournal =
"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//probe//EN\r\nBEGIN:VJOURNAL\r\n"
"UID:jr-1\r\nDTSTAMP:20260101T000000Z\r\nCREATED:20260101T000000Z\r\n"
"LAST-MODIFIED:20260102T000000Z\r\nSEQUENCE:2\r\nSUMMARY:Jr\r\nDESCRIPTION:D\r\n"
"STATUS:FINAL\r\nCLASS:PRIVATE\r\nDTSTART:20260201T100000Z\r\n"
"RRULE:FREQ=WEEKLY;BYDAY=MO\r\nEXDATE:20260615T090000Z\r\n"
"RECURRENCE-ID:20260608T090000Z\r\n"
"CATEGORIES:a,b\r\nURL:http://example.com/j\r\nCOLOR:green\r\n"
"ORGANIZER:mailto:o@example.com\r\nATTENDEE;CN=C:mailto:c@example.com\r\n"
"ATTACH:http://example.com/f.pdf\r\nRELATED-TO:other-uid\r\n"
"X-CUSTOM-THING:jj\r\nEND:VJOURNAL\r\nEND:VCALENDAR\r\n";

// --- helpers ---------------------------------------------------------------

/// iCal property names present in `bytes`, ignoring the VCALENDAR wrapper and
/// KCalendarCore's own X-KDE-* bookkeeping. Compared source-vs-demoted, this
/// is the "did we honour RFC 5545" measurement that IP.8 turns into a gate.
static QSet<QString> icalPropertyNames(const QByteArray& bytes)
{
    // RFC 5545 §3.1 line unfolding MUST happen first: KCalendarCore folds at
    // 75 octets, and an ATTENDEE line routinely folds BEFORE its colon — so a
    // naive per-line parse reports ATTENDEE as absent and the continuation
    // fragment as a bogus property. (That mis-parse survived one revision of
    // this probe; it is exactly the kind of false positive the audit doc warns
    // against, so it is fixed here and called out rather than quietly amended.)
    QList<QByteArray> logical;
    for (const QByteArray& raw : bytes.split('\n')) {
        QByteArray line = raw;
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;
        if ((line.startsWith(' ') || line.startsWith('\t')) && !logical.isEmpty()) {
            logical.last() += line.mid(1);      // continuation of the previous line
            continue;
        }
        logical.append(line);
    }

    QSet<QString> names;
    for (const QByteArray& line : logical) {
        const int cut = line.indexOf(':');
        if (cut < 0) continue;
        QByteArray name = line.left(cut);
        const int semi = name.indexOf(';');
        if (semi >= 0) name = name.left(semi);   // strip parameters
        const QString n = QString::fromLatin1(name).trimmed().toUpper();
        if (n == "BEGIN" || n == "END" || n == "VERSION" || n == "PRODID"
            || n == "DTSTAMP" || n.startsWith("X-KDE"))
            continue;
        names.insert(n);
    }
    return names;
}

static QString joinSorted(const QSet<QString>& s)
{
    QStringList l(s.begin(), s.end());
    l.sort();
    return l.isEmpty() ? QStringLiteral("(none)") : l.join(QStringLiteral(", "));
}

static void reportCatalogue(const QString& label, const QJsonObject& obj,
                            const QList<PropertyId>& ids)
{
    QSet<QString> declared;
    for (const auto& i : ids) declared.insert(i.toString());
    const QSet<QString> env{ CanonEnvelope::canonKey(), CanonEnvelope::uidKey(),
                             CanonEnvelope::providerExtrasKey() };
    QStringList emitted, undeclared;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (env.contains(it.key())) continue;
        emitted << it.key();
        if (!declared.contains(it.key())) undeclared << it.key();
    }
    emitted.sort(); undeclared.sort();
    out << "\n  " << label << "\n";
    out << "    emitted(" << emitted.size() << "): " << emitted.join(", ") << "\n";
    out << "    UNDECLARED(" << undeclared.size() << "): "
        << (undeclared.isEmpty() ? QStringLiteral("(none)") : undeclared.join(", ")) << "\n";
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Calendar::ICalToCanonStage promote;
    Calendar::CanonToICalStage demote;
    const auto calIds  = Calendar::calendarCanonPropertyIds();
    const auto todoIds = Todo::todoCanonPropertyIds();

    struct Case { const char* name; const char* ical; };
    const Case cases[] = { {"vevent", kEvent}, {"vtodo", kTodo}, {"vjournal", kJournal} };
    QJsonObject canonByKind[3];

    out << "=== 1. Catalogue vs emitter (IP.1's measurement — expected clean) ===\n";
    for (int i = 0; i < 3; ++i) {
        const QJsonObject o = CanonEnvelope::parse(promote.transform(QByteArray(cases[i].ical)));
        canonByKind[i] = o;
        reportCatalogue(QStringLiteral("{calendar,canon} %1  [_canon.kind='%2']")
                            .arg(QString::fromLatin1(cases[i].name), CanonEnvelope::kind(o)),
                        o, calIds);
    }
    reportCatalogue(QStringLiteral("{todo,canon} view of the same VTODO"), canonByKind[1], todoIds);

    out << "\n=== 2. RFC 5545 round-trip fidelity (the measurement NOTHING makes today) ===\n";
    out << "    O79 / O83 / O85 / O86 / O87 all live in this section.\n";
    for (int i = 0; i < 3; ++i) {
        const QByteArray c1    = promote.transform(QByteArray(cases[i].ical));
        const QByteArray ical2 = demote.transform(c1);
        const QByteArray c2    = promote.transform(ical2);
        const QSet<QString> before = icalPropertyNames(QByteArray(cases[i].ical));
        const QSet<QString> after  = icalPropertyNames(ical2);
        out << "\n  --- " << cases[i].name
            << "   canon-stable=" << (c1 == c2 ? "YES" : "NO") << "\n";
        out << "    source properties (" << before.size() << "): " << joinSorted(before) << "\n";
        out << "    LOST on demote   (" << (before - after).size() << "): "
            << joinSorted(before - after) << "\n";
        out << "    gained on demote (" << (after - before).size() << "): "
            << joinSorted(after - before) << "\n";
        for (const QByteArray& raw : ical2.split('\n')) {
            const QByteArray t = raw.trimmed();
            if (t.startsWith("GEO:"))
                out << "    GEO line emitted: " << QString::fromUtf8(t.toPercentEncoding())
                    << "   <- O86 (percent-encoded: the raw bytes are not valid UTF-8)\n";
        }
    }

    out << "\n=== 3. O84 — CanonJsonMerger erases _canon.kind ===\n";
    for (int i = 0; i < 3; ++i) {
        const QByteArray c = promote.transform(QByteArray(cases[i].ical));
        CanonicalRecord r; r.data = c;
        CanonJsonMerger m(QStringLiteral("calendar"), calIds);
        const CanonicalRecord merged =
            m.merge(r, r, r, AutoResolveStrategy::SourceAlwaysWins);
        const QByteArray reIcal = demote.transform(merged.data);
        const QString comp = reIcal.contains("BEGIN:VEVENT")   ? QStringLiteral("VEVENT")
                           : reIcal.contains("BEGIN:VTODO")    ? QStringLiteral("VTODO")
                           : reIcal.contains("BEGIN:VJOURNAL") ? QStringLiteral("VJOURNAL")
                                                               : QStringLiteral("??");
        out << "  " << cases[i].name
            << ": kind before='" << CanonEnvelope::kind(CanonEnvelope::parse(c))
            << "' after merge='" << CanonEnvelope::kind(CanonEnvelope::parse(merged.data))
            << "'  -> demotes as " << comp
            << (comp.toLower() != QLatin1String("v") + QString::fromLatin1(cases[i].name).mid(1)
                    ? "   <- CORRUPTION" : "")
            << "\n";
    }

    out << "\n=== 4. Merger silently takes TARGET for uncatalogued keys ===\n";
    {
        QJsonObject s = canonByKind[1], t = canonByKind[1], b = canonByKind[1];
        s.insert(QStringLiteral("zzUncatalogued"), QStringLiteral("SOURCE"));
        t.insert(QStringLiteral("zzUncatalogued"), QStringLiteral("TARGET"));
        b.insert(QStringLiteral("zzUncatalogued"), QStringLiteral("TARGET"));
        CanonicalRecord rs, rt, rb;
        rs.data = CanonEnvelope::serialize(s);
        rt.data = CanonEnvelope::serialize(t);
        rb.data = CanonEnvelope::serialize(b);
        CanonJsonMerger m(QStringLiteral("calendar"), calIds);
        const auto mr = m.merge(rs, rt, rb, AutoResolveStrategy::SourceAlwaysWins);
        out << "  source-wins merge of an uncatalogued key yields: "
            << CanonEnvelope::parse(mr.data).value(QStringLiteral("zzUncatalogued")).toString()
            << "   (SOURCE would mean honoured)\n";
    }

    out << "\n=== 5. Catalogue divergence: todo keys absent from the calendar catalogue ===\n";
    {
        QSet<QString> cal, td;
        for (const auto& i : calIds)  cal.insert(i.toString());
        for (const auto& i : todoIds) td.insert(i.toString());
        out << "  todo-only (" << (td - cal).size() << "): " << joinSorted(td - cal) << "\n";
        out << "  calendar catalogue=" << cal.size() << "  todo catalogue=" << td.size() << "\n";
    }

    out << "\n=== 6. O90 — demote is not a pure function of canon (attendee X-UID) ===\n";
    {
        const QByteArray canon = promote.transform(QByteArray(kEvent));
        const QByteArray a = demote.transform(canon);
        const QByteArray b = demote.transform(canon);
        out << "  same process, two demotes identical: " << (a == b ? "YES" : "NO") << "\n";
        out << "  sha256(demoted) = "
            << QString::fromLatin1(QCryptographicHash::hash(a, QCryptographicHash::Sha256)
                                       .toHex().left(16))
            << "   <- re-run this binary; it differs per PROCESS (heap-address X-UID)\n";
        for (const QByteArray& raw : a.split('\n'))
            if (raw.contains("X-UID"))
                out << "  " << QString::fromUtf8(raw.trimmed()) << "\n";
    }

    out.flush();
    return 0;
}
