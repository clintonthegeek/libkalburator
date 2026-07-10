// O45 (CP-C ruling, 2026-07-09) — bulk-write timeout honesty in
// RemoteCalendarBackend::applyRecords().
//
// applyRecords dispatches every create/delete job up-front, but the HTTP
// stack and the server drain them at a bounded rate (single-threaded dev
// Radicale: ~1-2 PUTs/s). The per-job wall-clock watchdog started at
// DISPATCH time therefore measures queue position, not server health: any
// batch larger than (drain rate x timeout) self-reports spurious timeouts
// even though every request eventually lands — FINDINGS O45's live shape
// (145/145 "failed" creates all present on the server's disk).
//
// The fix replaces the per-job watchdog with one PROGRESS-based batch
// watchdog, restarted on every per-record completion: it fires only when
// nothing at all completes for a full timeout window — a genuine stall.
//
// Two pins:
//  (1) RED against the per-job watchdog: a 40-create batch against a
//      serialized slow server (drain 150 ms/request, timeout 1500 ms) must
//      succeed completely — the server is healthy, just rate-limited.
//  (2) honesty guard: a genuinely stalled server (accepts requests, never
//      answers) must still fail the batch within a small multiple of the
//      timeout — the progress watchdog must not have destroyed stall
//      detection.

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "remotecalendarbackend.h"
#include "writerbatch.h"
#include "writeoperation.h"
#include "backendrecord.h"
#include "syncoperation.h"

#include "fakecaldavserver.h"

using namespace Kalburator::Sync;

namespace {
QByteArray makeEventIcs(const QString &uid, const QString &summary)
{
    return QStringLiteral(
               "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
               "BEGIN:VEVENT\r\nUID:%1\r\n"
               "SUMMARY:%2\r\nDTSTART:20260601T120000Z\r\n"
               "DTEND:20260601T130000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")
        .arg(uid, summary)
        .toUtf8();
}
}  // namespace

class TestBulkWriteDispatch : public QObject
{
    Q_OBJECT

private slots:
    void slowButHealthyServer_bulkCreatesAllSucceed();
    void genuinelyStalledServer_failsWithinTimeout();
};

// (1) RED: today the per-job watchdogs (all started at dispatch) expire for
// every create whose queue position exceeds timeout/drain-rate, so most of
// the batch is misreported failed even though the server answers every
// request. With the batch progress watchdog, completions arrive every
// ~150 ms, the window never elapses, and all 40 creates succeed.
void TestBulkWriteDispatch::slowButHealthyServer_bulkCreatesAllSucceed()
{
    FakeCalDavServer server;
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setSerializeResponses(true);
    server.setResponseDelayForMethod(QByteArrayLiteral("PUT"), 150);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.setDbPath(cacheDir.filePath(QStringLiteral("sync.db")));
    backend.registerCalendarUrl(QStringLiteral("Personal"),
                                server.baseUrl().toString() + calHref.mid(1));
    // 1.5 s timeout, 150 ms serialized drain: only ~10 of the 40 creates
    // can complete inside any single job's dispatch-anchored window, but
    // batch progress is continuous.
    backend.setTransferTimeoutMs(1500);

    const int kCount = 40;
    WriterBatch batch;
    for (int i = 0; i < kCount; ++i) {
        BackendRecord rec;
        rec.id = QStringLiteral("bulk-%1").arg(i);
        rec.data = makeEventIcs(rec.id, QStringLiteral("Bulk %1").arg(i));
        batch.creates.append(rec);
    }

    WriteOperation *op = backend.applyRecords(QStringLiteral("Personal"), batch);
    QVERIFY(op);
    // 40 x 150 ms = 6 s of serialized drain; allow generous slack.
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 30000);

    QCOMPARE(op->failedUids().size(), 0);
    QCOMPARE(op->succeededUids().size(), kCount);
    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(server.requestCount("PUT"), kCount);
    op->deleteLater();
}

// (2) The honesty guard: the progress watchdog must still catch a genuine
// stall. The fake accepts every request and never responds (dropRequests);
// no completion ever arrives, so the window elapses once and the whole
// batch fails — bounded, not hung.
void TestBulkWriteDispatch::genuinelyStalledServer_failsWithinTimeout()
{
    FakeCalDavServer server;
    const QString calHref = QStringLiteral("/calendars/testuser/personal/");
    server.setCalendars({{QStringLiteral("Personal"), calHref}});
    server.setDropRequests(true);
    QVERIFY(server.startListening());

    QTemporaryDir cacheDir;
    QVERIFY(cacheDir.isValid());

    RemoteCalendarBackend backend(server.baseUrl(),
                                  QStringLiteral("testuser"),
                                  QStringLiteral("testpass"));
    backend.setCacheDir(cacheDir.path());
    backend.setDbPath(cacheDir.filePath(QStringLiteral("sync.db")));
    backend.registerCalendarUrl(QStringLiteral("Personal"),
                                server.baseUrl().toString() + calHref.mid(1));
    backend.setTransferTimeoutMs(1000);

    WriterBatch batch;
    for (int i = 0; i < 5; ++i) {
        BackendRecord rec;
        rec.id = QStringLiteral("stall-%1").arg(i);
        rec.data = makeEventIcs(rec.id, QStringLiteral("Stall %1").arg(i));
        batch.creates.append(rec);
    }

    QElapsedTimer timer;
    timer.start();
    WriteOperation *op = backend.applyRecords(QStringLiteral("Personal"), batch);
    QVERIFY(op);
    QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 10000);

    QVERIFY2(timer.elapsed() < 5000,
             "stall detection must fire within a small multiple of the timeout");
    QCOMPARE(op->succeededUids().size(), 0);
    QCOMPARE(op->failedUids().size(), 5);
    QCOMPARE(op->state(), SyncOperation::Failed);
    op->deleteLater();
}

QTEST_GUILESS_MAIN(TestBulkWriteDispatch)
#include "tst_bulk_write_dispatch.moc"
