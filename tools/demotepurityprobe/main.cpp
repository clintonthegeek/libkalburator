// IP.12 (incidence-parity campaign) — demote-purity cross-process probe.
//
// Demotes a fixed, hand-built canon fixture through all three incidence
// kinds' demote paths (VEVENT, VTODO, VJOURNAL) and prints the resulting
// iCal bytes to stdout, one section per kind. Exists so a QTest slot
// (tests/calendar/tst_demote_purity.cpp) can launch this binary TWICE, as
// two genuinely separate OS processes, and diff their stdout byte-for-byte
// — the direct proof O90/IP.12 requires: demote(canon) must be a pure
// function of canon, not of which process happens to be running it.
//
// KCalendarCore::ICalFormat::toICalString() unconditionally regenerates
// DTSTAMP to wall-clock "now" on every call (this is correct RFC 5545
// semantics for DTSTAMP — "date/time that the instance of the iCalendar
// object was created" — not a bug, and out of IP.12's scope). That alone
// would make ANY two invocations of this probe differ, regardless of the
// X-UID fix, so DTSTAMP is stripped from the printed output the same way
// CREATED/LAST-MODIFIED already are on the demote side (stripICalPropertyLine)
// — a test-only normalization of a known-and-correct source of per-call
// non-determinism that has nothing to do with O90.
//
// Deliberately NOT a CMake-gated tool (no vendor credentials/network
// involved, unlike graphcli/googlecli) — built unconditionally, same as
// tools/matrixgen.

#include "eventcanonfields.h"
#include "icaltimestamp.h"
#include "journalcanonfields.h"
#include "vtodocanonfields.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>

using namespace Kalburator;

namespace {

/// One canon fixture shared by all three kinds: a summary, a start time, and
/// two ATTENDEEs (the O90 property). Deliberately minimal — this probe
/// exists to pin ATTENDEE/X-UID purity, not to be a maximal RFC 5545
/// fixture (tst_incidence_rfc5545_fidelity.cpp already owns that).
QJsonObject buildFixture(const QString& uid)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("uid"), uid);
    obj.insert(QStringLiteral("summary"), QStringLiteral("Demote purity fixture"));

    QJsonObject start;
    start.insert(QStringLiteral("dateTime"), QStringLiteral("2026-06-01T09:00:00Z"));
    obj.insert(QStringLiteral("start"), start);

    QJsonArray attendees;
    QJsonObject a1;
    a1.insert(QStringLiteral("email"), QStringLiteral("a@example.com"));
    a1.insert(QStringLiteral("name"), QStringLiteral("A"));
    attendees.append(a1);
    QJsonObject a2;
    a2.insert(QStringLiteral("email"), QStringLiteral("b@example.com"));
    a2.insert(QStringLiteral("name"), QStringLiteral("B"));
    attendees.append(a2);
    obj.insert(QStringLiteral("attendees"), attendees);

    return obj;
}

/// Strip the one source of legitimate, out-of-scope per-call non-determinism
/// (DTSTAMP) so this probe's two invocations can be compared byte-for-byte
/// for exactly the property IP.12 owns (ATTENDEE's X-UID).
QByteArray normalized(QByteArray icalBytes)
{
    return Calendar::stripICalPropertyLine(icalBytes, QStringLiteral("DTSTAMP"));
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);

    out << "=== VEVENT ===\n"
        << normalized(Calendar::canonObjectToEventBytes(buildFixture(QStringLiteral("demote-purity-vevent"))))
        << "=== VTODO ===\n"
        << normalized(Todo::canonObjectToVtodoBytes(buildFixture(QStringLiteral("demote-purity-vtodo"))))
        << "=== VJOURNAL ===\n"
        << normalized(Calendar::canonObjectToJournalBytes(buildFixture(QStringLiteral("demote-purity-vjournal"))))
        << "=== END ===\n";
    out.flush();
    return 0;
}
