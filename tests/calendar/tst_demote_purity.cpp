// tst_demote_purity.cpp
//
// IP.12 (incidence-parity campaign) — O90: demote(canon) must be a pure
// function of canon alone, not of which process happens to run it.
// KCalendarCore::ICalFormat stamps a heap-address-derived "X-UID" parameter
// into every serialized ATTENDEE line; two demotes of BYTE-IDENTICAL canon
// in two different processes used to produce different bytes (stable within
// one process, different across processes — see FINDINGS.md O90).
//
// A same-process comparison is vacuous — it already trivially passed before
// any fix (O90's own text says so). This test proves the real claim by
// launching tools/demotepurityprobe TWICE as two genuinely separate OS
// processes (via QProcess) and diffing their stdout byte-for-byte — more
// direct evidence of "different processes" than a committed golden file
// (which only proves "this run matches whatever a PAST, unverifiable run
// produced"), and safer/more portable than a raw POSIX fork() inside a
// QTest binary: this codebase has no existing fork-based test
// infrastructure (verified by grep), and fork() inside a Qt process is
// well-documented as unsafe beyond an immediate exec() — the classic
// multi-threaded-fork hazard, which a Qt/QTest process is. QProcess
// sidesteps that entirely while still proving the literal claim: the two
// demotes really did run in two different processes.
//
// The probe itself strips DTSTAMP (KCalendarCore correctly, per RFC 5545,
// regenerates it to wall-clock "now" on every serialize — legitimate,
// out-of-scope non-determinism, not O90) before printing, so this test is
// isolated to exactly the property IP.12 owns.

#include <QProcess>
#include <QTest>

class TestDemotePurity : public QObject {
    Q_OBJECT
private slots:

    void twoProcessesDemoteTheSameCanonToByteIdenticalOutput()
    {
#ifndef DEMOTE_PURITY_PROBE_PATH
        QSKIP("DEMOTE_PURITY_PROBE_PATH not defined by the build");
#else
        const QString probePath = QStringLiteral(DEMOTE_PURITY_PROBE_PATH);

        QProcess p1;
        p1.start(probePath, {});
        QVERIFY2(p1.waitForFinished(10000), "first probe process did not finish");
        QCOMPARE(p1.exitStatus(), QProcess::NormalExit);
        QCOMPARE(p1.exitCode(), 0);
        const QByteArray out1 = p1.readAllStandardOutput();

        QProcess p2;
        p2.start(probePath, {});
        QVERIFY2(p2.waitForFinished(10000), "second probe process did not finish");
        QCOMPARE(p2.exitStatus(), QProcess::NormalExit);
        QCOMPARE(p2.exitCode(), 0);
        const QByteArray out2 = p2.readAllStandardOutput();

        // Non-vacuity guards: prove both runs actually exercised the ATTENDEE
        // path (so a silently-empty or silently-erroring probe couldn't
        // trivially pass this test), and that the fix actually did its job.
        QVERIFY2(out1.contains("ATTENDEE"), "probe 1 must have serialized an ATTENDEE");
        QVERIFY2(out2.contains("ATTENDEE"), "probe 2 must have serialized an ATTENDEE");
        QVERIFY2(!out1.contains("X-UID"), "probe 1 must not carry the heap-derived X-UID");
        QVERIFY2(!out2.contains("X-UID"), "probe 2 must not carry the heap-derived X-UID");

        QCOMPARE(out1, out2);
#endif
    }
};

QTEST_GUILESS_MAIN(TestDemotePurity)
#include "tst_demote_purity.moc"
