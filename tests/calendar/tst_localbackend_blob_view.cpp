// tests/calendar/tst_localbackend_blob_view.cpp
// Phase D Task 12 — verify LocalBackend's IBlobBackend implementation.

#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>

#include "localbackend.h"
#include "iblobbackend.h"

using namespace Kalburator::Sync;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Build a minimal VCALENDAR iCal byte string for the given UID.
static QByteArray makeIcsBytes(const QString &uid)
{
    return QStringLiteral(
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//test//test//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:%1\r\n"
        "SUMMARY:Test event %1\r\n"
        "DTSTART:20250101T120000Z\r\n"
        "DTEND:20250101T130000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n").arg(uid).toUtf8();
}

static BackendRecord makeRecord(const QString &uid, const QByteArray &data = QByteArray())
{
    BackendRecord r;
    r.id   = uid;
    r.data = data.isEmpty() ? makeIcsBytes(uid) : data;
    return r;
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestLocalBackendBlobView : public QObject
{
    Q_OBJECT
private slots:
    void createRecord_writesIcsFile();
    void loadRecord_readsIcsFile();
    void deleteRecord_removesFile();
    void modifiedSince_returnsChangedFiles();
    void modifiedSince_shortCircuitsOnFingerprint();
};

// ---------------------------------------------------------------------------
// createRecord_writesIcsFile
//
// Cast to IBlobBackend*; call createRecord(calendarId, makeRecord("uid-A", …)).
// Assert <dir>/uid-A.ics exists with the expected content.
// ---------------------------------------------------------------------------

void TestLocalBackendBlobView::createRecord_writesIcsFile()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    LocalBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);
    QVERIFY(blob);

    const QString calendarId = QStringLiteral("cal-create");
    const QByteArray data    = makeIcsBytes(QStringLiteral("uid-A"));
    const BackendRecord rec  = makeRecord(QStringLiteral("uid-A"), data);

    const QString returned = blob->createRecord(calendarId, rec);
    QCOMPARE(returned, QStringLiteral("uid-A"));

    // File must exist
    const QString expectedPath = root.filePath(calendarId + QStringLiteral("/uid-A.ics"));
    QVERIFY(QFile::exists(expectedPath));

    // Content must match what we provided
    QFile f(expectedPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), data);
}

// ---------------------------------------------------------------------------
// loadRecord_readsIcsFile
//
// Pre-write <dir>/uid-B.ics. loadRecord("uid-B") must return a record
// with matching id and data.
// ---------------------------------------------------------------------------

void TestLocalBackendBlobView::loadRecord_readsIcsFile()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString calendarId = QStringLiteral("cal-load");
    const QString calPath    = root.filePath(calendarId);
    QVERIFY(QDir().mkpath(calPath));

    const QByteArray data = makeIcsBytes(QStringLiteral("uid-B"));
    const QString filePath = calPath + QStringLiteral("/uid-B.ics");

    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    LocalBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);

    auto result = blob->loadRecord(QStringLiteral("uid-B"));
    QVERIFY(result.has_value());
    QCOMPARE(result->id,   QStringLiteral("uid-B"));
    QCOMPARE(result->data, data);

    // contentHash must be non-empty SHA-256 hex
    QVERIFY(!result->contentHash.isEmpty());
    QCOMPARE(result->contentHash.length(), 64); // SHA-256 = 32 bytes = 64 hex chars
}

// ---------------------------------------------------------------------------
// deleteRecord_removesFile
//
// Write file, then deleteRecord("uid-C"), assert file gone.
// ---------------------------------------------------------------------------

void TestLocalBackendBlobView::deleteRecord_removesFile()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString calendarId = QStringLiteral("cal-delete");
    const QString calPath    = root.filePath(calendarId);
    QVERIFY(QDir().mkpath(calPath));

    const QString filePath = calPath + QStringLiteral("/uid-C.ics");
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(makeIcsBytes(QStringLiteral("uid-C")));
    }
    QVERIFY(QFile::exists(filePath));

    LocalBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);

    QVERIFY(blob->deleteRecord(QStringLiteral("uid-C")));
    QVERIFY(!QFile::exists(filePath));
}

// ---------------------------------------------------------------------------
// modifiedSince_returnsChangedFiles
//
// Write 2 files. Manually set one file's mtime to be after T.
// modifiedSince(coll, T) should return just that one.
// ---------------------------------------------------------------------------

void TestLocalBackendBlobView::modifiedSince_returnsChangedFiles()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    const QString calendarId = QStringLiteral("cal-mtime");
    const QString calPath    = root.filePath(calendarId);
    QVERIFY(QDir().mkpath(calPath));

    // Fixed reference point
    const QDateTime T = QDateTime(QDate(2025, 6, 1), QTime(12, 0, 0), QTimeZone::utc());

    // Write uid-old: we'll leave its mtime alone (it was just created,
    // which is fine because we only care about > T).
    // Strategy: write both files, then set uid-recent's mtime explicitly to T+1h.

    const QString oldPath    = calPath + QStringLiteral("/uid-old.ics");
    const QString recentPath = calPath + QStringLiteral("/uid-recent.ics");

    {
        QFile f(oldPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(makeIcsBytes(QStringLiteral("uid-old")));
    }
    {
        QFile f(recentPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(makeIcsBytes(QStringLiteral("uid-recent")));
    }

    // Set uid-old's mtime to T-1h (before T) and uid-recent's to T+1h (after T)
    // QFile::setFileTime is Qt 6.x
    const QDateTime oldMtime    = T.addSecs(-3600);
    const QDateTime recentMtime = T.addSecs( 3600);

    {
        QFile f(oldPath);
        // Qt6: setFileTime(QFileDevice::FileModificationTime, …)
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(oldMtime, QFileDevice::FileModificationTime));
        f.close();
    }
    {
        QFile f(recentPath);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(recentMtime, QFileDevice::FileModificationTime));
        f.close();
    }

    // Confirm the mtimes look right before querying
    QVERIFY(QFileInfo(oldPath).lastModified()    <= T);
    QVERIFY(QFileInfo(recentPath).lastModified() >  T);

    LocalBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);

    const QList<BackendRecord> results = blob->modifiedSince(calendarId, T);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().id, QStringLiteral("uid-recent"));
}

// ---------------------------------------------------------------------------
// modifiedSince_shortCircuitsOnFingerprint
//
// Warm the fingerprint cache by calling modifiedSince once (with a very
// old `since` so it actually walks the directory). Then call again with NO
// file changes. The second call should return empty — demonstrating the
// short-circuit is in place.
//
// Note: this test requires setDbPath() so m_fingerprints is non-null.
// We use a second temporary directory for the DB file.
// ---------------------------------------------------------------------------

void TestLocalBackendBlobView::modifiedSince_shortCircuitsOnFingerprint()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    QTemporaryDir dbDir;
    QVERIFY(dbDir.isValid());

    const QString calendarId = QStringLiteral("cal-fp");
    const QString calPath    = root.filePath(calendarId);
    QVERIFY(QDir().mkpath(calPath));

    // Write one file
    {
        QFile f(calPath + QStringLiteral("/uid-X.ics"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(makeIcsBytes(QStringLiteral("uid-X")));
    }

    LocalBackend backend(root.path());
    // Inject the DB path so m_fingerprints gets initialised
    backend.setDbPath(dbDir.filePath(QStringLiteral("test.kalburator-sync.db")));

    auto *blob = static_cast<IBlobBackend *>(&backend);

    // First call: use a very old `since` so the walk runs and the fingerprint is stored
    const QDateTime veryOld = QDateTime(QDate(2000, 1, 1), QTime(0, 0, 0), QTimeZone::utc());
    QList<BackendRecord> first = blob->modifiedSince(calendarId, veryOld);
    // At least uid-X should be returned (its mtime is after year 2000)
    QVERIFY(!first.isEmpty());

    // Second call: no files changed — fingerprint matches — should short-circuit to empty
    QList<BackendRecord> second = blob->modifiedSince(calendarId, veryOld);
    QVERIFY(second.isEmpty());
}

// ---------------------------------------------------------------------------

QTEST_GUILESS_MAIN(TestLocalBackendBlobView)
#include "tst_localbackend_blob_view.moc"
