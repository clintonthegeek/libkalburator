#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <KCalendarCore/Event>

#include "icsfeedfetcher.h"

using namespace Kalburator::Sync;

namespace {
QUrl fixtureUrl(const QString &name)
{
    const QString fixtureDir = QStringLiteral(KALBURATOR_CALENDAR_FIXTURE_DIR);
    return QUrl::fromLocalFile(QDir(fixtureDir).absoluteFilePath(name));
}
} // namespace

class TestIcsFeedFetcher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_network = new QNetworkAccessManager(this);
        m_fetcher = new IcsFeedFetcher(m_network, this);
    }

    void cleanupTestCase()
    {
        delete m_fetcher;
        m_fetcher = nullptr;
        delete m_network;
        m_network = nullptr;
    }

    // ----- Single-event fetch -----
    void singleEvent_fetchesOneIncidence()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("single_event.ics"));
        QVERIFY(r.success);
        QCOMPARE(r.incidences.size(), 1);
        QCOMPARE(r.incidences.first()->uid(),
                 QStringLiteral("single-event-001@example.com"));
        QCOMPARE(r.incidences.first()->summary(),
                 QStringLiteral("Single test event"));
    }

    // ----- Multi-event fetch -----
    void manyEvents_fetchesAllIncidences()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("many_events.ics"));
        QVERIFY(r.success);
        QCOMPARE(r.incidences.size(), 3);
    }

    // ----- Date-range filter, single-occurrence -----
    void dateRange_filtersSingleOccurrenceOutsideRange()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("many_events.ics"),
                                         QDate(2026, 6, 10),
                                         QDate(2026, 6, 20));
        QVERIFY(r.success);
        QCOMPARE(r.incidences.size(), 1);
        QCOMPARE(r.incidences.first()->uid(),
                 QStringLiteral("event-002@example.com"));
    }

    // ----- Date-range filter, recurring kept -----
    void dateRange_keepsRecurringWithOccurrenceInRange()
    {
        // recurring_event.ics: weekly starting 2020-01-07, no UNTIL → still
        // recurring in 2026.
        const auto r = m_fetcher->fetch(fixtureUrl("recurring_event.ics"),
                                         QDate(2026, 6, 10),
                                         QDate(2026, 6, 20));
        QVERIFY(r.success);
        // The endless weekly should be kept; the past-recurring (UNTIL=2012)
        // should be filtered out.
        QCOMPARE(r.incidences.size(), 1);
        QCOMPARE(r.incidences.first()->uid(),
                 QStringLiteral("weekly-001@example.com"));
    }

    // ----- Date-range filter, recurring with UNTIL in past dropped -----
    void dateRange_dropsRecurringEndedBeforeRange()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("recurring_event.ics"),
                                         QDate(2026, 6, 10),
                                         QDate(2026, 6, 20));
        QVERIFY(r.success);
        for (const auto &inc : r.incidences) {
            QVERIFY(inc->uid() != QStringLiteral("past-recurring-001@example.com"));
        }
    }

    // ----- VTIMEZONE handles -----
    void vtimezone_parsesWithoutError()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("with_vtimezone.ics"));
        QVERIFY2(r.success, qPrintable(r.errorMessage));
        QCOMPARE(r.incidences.size(), 1);
    }

    // ----- Error path: invalid URL -----
    void invalidUrl_returnsFailure()
    {
        const auto r = m_fetcher->fetch(QUrl("not a url"));
        QVERIFY(!r.success);
        QCOMPARE(r.errorMessage, QStringLiteral("Invalid URL"));
    }

    // ----- Error path: malformed iCal -----
    void malformedIcal_returnsParseFailure()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("malformed.ics"));
        QVERIFY(!r.success);
        QVERIFY(r.errorMessage.contains(QStringLiteral("parse"),
                                         Qt::CaseInsensitive));
    }

    // ----- Error path: missing file -----
    void missingFile_returnsNetworkFailure()
    {
        const auto r = m_fetcher->fetch(fixtureUrl("does_not_exist.ics"));
        QVERIFY(!r.success);
        QVERIFY(!r.errorMessage.isEmpty());
    }

    // ----- progress signal fires -----
    void progressSignal_emittedOnFetch()
    {
        QSignalSpy spy(m_fetcher, &IcsFeedFetcher::progress);
        m_fetcher->fetch(fixtureUrl("single_event.ics"));
        QVERIFY(spy.count() >= 1);
    }

private:
    QNetworkAccessManager *m_network = nullptr;
    IcsFeedFetcher *m_fetcher = nullptr;
};

QTEST_MAIN(TestIcsFeedFetcher)
#include "tst_icsfeedfetcher.moc"
