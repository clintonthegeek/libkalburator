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

/// Build an ICS byte string with an explicit SUMMARY field.
static QByteArray makeIcsBytesWithSummary(const QString &uid, const QString &summary)
{
    return QStringLiteral(
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//test//test//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:%1\r\n"
        "SUMMARY:%2\r\n"
        "DTSTART:20250101T120000Z\r\n"
        "DTEND:20250101T130000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n").arg(uid, summary).toUtf8();
}

/// Build an ICS byte string with an explicit LAST-MODIFIED stamp
/// (@p lastMod in iCal "yyyyMMddTHHmmssZ" form).
static QByteArray makeIcsBytesWithLastModified(const QString &uid, const QString &lastMod)
{
    return QStringLiteral(
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//test//test//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:%1\r\n"
        "SUMMARY:Test event %1\r\n"
        "LAST-MODIFIED:%2\r\n"
        "DTSTART:20250101T120000Z\r\n"
        "DTEND:20250101T130000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n").arg(uid, lastMod).toUtf8();
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
    void updateRecord_modifies_existing_record();
    void updateRecord_nonexistent_id_returns_error();

    // lastModified derivation (PlanStan LWW tie-bias fix A2)
    void lastModified_explicitStamp_authoritativeOverFileMtime();
    void lastModified_fileMtime_subsecondTiebreakWithinSameSecond();
    void lastModified_fileMtime_whenNoExplicitStamp();
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
// updateRecord_modifies_existing_record
//
// Create a record with an "original summary". Call updateRecord with the
// same uid but "updated summary". Load and verify the summary changed.
// ---------------------------------------------------------------------------

void TestLocalBackendBlobView::updateRecord_modifies_existing_record()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    LocalBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);

    const QString calendarId = QStringLiteral("cal-update");

    // Create a record with the original summary
    const QByteArray originalData =
        makeIcsBytesWithSummary(QStringLiteral("uid-update-L"),
                                QStringLiteral("original summary"));
    const BackendRecord original = makeRecord(QStringLiteral("uid-update-L"), originalData);
    blob->createRecord(calendarId, original);

    // Update the record with a new summary
    const QByteArray updatedData =
        makeIcsBytesWithSummary(QStringLiteral("uid-update-L"),
                                QStringLiteral("updated summary"));
    const BackendRecord updated = makeRecord(QStringLiteral("uid-update-L"), updatedData);
    bool ok = blob->updateRecord(updated);
    QVERIFY(ok);

    // Load and verify the new summary is present, old one is gone
    auto loaded = blob->loadRecord(QStringLiteral("uid-update-L"));
    QVERIFY(loaded.has_value());
    const QString ical = QString::fromUtf8(loaded->data);
    QVERIFY(ical.contains(QStringLiteral("updated summary")));
    QVERIFY(!ical.contains(QStringLiteral("original summary")));
}

// ---------------------------------------------------------------------------
// updateRecord_nonexistent_id_returns_error
//
// Do NOT call createRecord. updateRecord on an unknown uid must return false.
// ---------------------------------------------------------------------------

void TestLocalBackendBlobView::updateRecord_nonexistent_id_returns_error()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());

    LocalBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);

    // This uid has never been created
    const QByteArray ghostData =
        makeIcsBytesWithSummary(QStringLiteral("uid-ghost-L"),
                                QStringLiteral("ghost summary"));
    const BackendRecord ghost = makeRecord(QStringLiteral("uid-ghost-L"), ghostData);
    bool ok = blob->updateRecord(ghost);
    QVERIFY(!ok);
}

// ---------------------------------------------------------------------------
// lastModified derivation — PlanStan LWW tie-bias fix A2.
// An explicit iCal LAST-MODIFIED is authoritative; file mtime only folds in its
// sub-second part when it lands in the same second; and is the sole source when
// no explicit stamp exists.
// ---------------------------------------------------------------------------

void TestLocalBackendBlobView::lastModified_explicitStamp_authoritativeOverFileMtime()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString calPath = root.filePath(QStringLiteral("cal-lm"));
    QVERIFY(QDir().mkpath(calPath));
    const QString filePath = calPath + QStringLiteral("/uid-lm1.ics");

    // Explicit historical stamp; file written/touched "now" (a different second).
    const QDateTime stamp = QDateTime(QDate(2020, 1, 1), QTime(0, 0, 0), QTimeZone::utc());
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(makeIcsBytesWithLastModified(QStringLiteral("uid-lm1"),
                                             QStringLiteral("20200101T000000Z")));
    }
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime));
        f.close();
    }

    LocalBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);
    auto rec = blob->loadRecord(QStringLiteral("uid-lm1"));
    QVERIFY(rec.has_value());

    // The explicit stamp wins — NOT the much-later file mtime (the old bug).
    QCOMPARE(rec->lastModified.toUTC(), stamp);
}

void TestLocalBackendBlobView::lastModified_fileMtime_subsecondTiebreakWithinSameSecond()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString calPath = root.filePath(QStringLiteral("cal-lm"));
    QVERIFY(QDir().mkpath(calPath));
    const QString filePath = calPath + QStringLiteral("/uid-lm2.ics");

    const QDateTime stampSecond = QDateTime(QDate(2026, 6, 4), QTime(12, 0, 0), QTimeZone::utc());
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(makeIcsBytesWithLastModified(QStringLiteral("uid-lm2"),
                                             QStringLiteral("20260604T120000Z")));
    }
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::ReadWrite));
        // Same wall-clock second as the stamp, but with a sub-second part.
        QVERIFY(f.setFileTime(stampSecond.addMSecs(500), QFileDevice::FileModificationTime));
        f.close();
    }

    LocalBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);
    auto rec = blob->loadRecord(QStringLiteral("uid-lm2"));
    QVERIFY(rec.has_value());

    // Expected = the stamp's second + whatever sub-second the filesystem stored
    // (robust to filesystems that round mtime precision).
    const int storedMs = QFileInfo(filePath).lastModified().time().msec();
    QCOMPARE(rec->lastModified.toUTC(), stampSecond.addMSecs(storedMs));
}

void TestLocalBackendBlobView::lastModified_fileMtime_whenNoExplicitStamp()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString calPath = root.filePath(QStringLiteral("cal-lm"));
    QVERIFY(QDir().mkpath(calPath));
    const QString filePath = calPath + QStringLiteral("/uid-lm3.ics");

    // makeIcsBytes carries no LAST-MODIFIED.
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(makeIcsBytes(QStringLiteral("uid-lm3")));
    }
    const QDateTime mtime = QDateTime(QDate(2024, 3, 3), QTime(9, 0, 0), QTimeZone::utc());
    {
        QFile f(filePath);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(mtime, QFileDevice::FileModificationTime));
        f.close();
    }

    LocalBackend backend(root.path());
    auto *blob = static_cast<IBlobBackend *>(&backend);
    auto rec = blob->loadRecord(QStringLiteral("uid-lm3"));
    QVERIFY(rec.has_value());

    // No stamp → file mtime is authoritative.
    QCOMPARE(rec->lastModified.toUTC(), QFileInfo(filePath).lastModified().toUTC());
}

// ---------------------------------------------------------------------------

QTEST_GUILESS_MAIN(TestLocalBackendBlobView)
#include "tst_localbackend_blob_view.moc"
