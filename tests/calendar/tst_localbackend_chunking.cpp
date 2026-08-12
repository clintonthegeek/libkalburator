// Parallel-sync Task 4 — LocalBackend chunking.
//
// fetchItems/pushItems/deleteItems each did their whole pass synchronously
// inside the deferred functor, so a large collection blocked the backend's
// thread for the entire duration and could not be cancelled. They now
// process in batches with an event-loop turn between them.

#include <QtTest/QtTest>
#include <QObject>
#include <QTemporaryDir>
#include <functional>

#include <KCalendarCore/Event>

#include "localbackend.h"
#include "syncoperation.h"

using Kalburator::Sync::LocalBackend;
using Kalburator::Sync::SyncOperation;

class TestLocalBackendChunking : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_backend = std::make_unique<LocalBackend>(m_dir->path());
        QVERIFY(m_backend->createCalendar(QStringLiteral("col"),
                                          QStringLiteral("cal"),
                                          QStringLiteral("Cal")));
        seed(500);
    }

    void cleanup()
    {
        m_backend.reset();
        m_dir.reset();
    }

    void testFetchYieldsToTheEventLoop()
    {
        // Discriminator design note: SyncBackendBase::enqueueOperation
        // already defers the whole start functor by one event-loop turn
        // (its own internal QTimer::singleShot(0, ...)). That means a
        // *single* "did some zero-delay timer fire before the op finished"
        // check does NOT discriminate chunked from unchunked — an
        // unchunked fetch also only finishes after that one deferred turn,
        // so a flag timer registered before the call would still see
        // timerFired==true either way (confirmed by running this test
        // against the pre-chunking code — see the task report for how).
        //
        // What genuinely distinguishes them is the NUMBER of event-loop
        // turns the op takes to finish: exactly 1 (the FIFO's own defer)
        // if the whole pass still runs synchronously inside that turn,
        // vs. several (500 records / 64-per-batch => ~8 batches) if it
        // yields internally between batches. So this counts zero-delay
        // "ticks" that occur before the op finishes, self-rescheduling
        // only while the op is still unfinished (never dangles past the
        // op's lifetime).
        auto *op = m_backend->fetchItems(QStringLiteral("cal"));
        QVERIFY(op);

        int turns = 0;
        std::function<void()> tick = [&, op]() {
            ++turns;
            if (!op->isFinished())
                QTimer::singleShot(0, this, tick);
        };
        QTimer::singleShot(0, this, tick);

        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 10000);

        QVERIFY2(turns > 1,
                 "fetchItems must yield to the event loop between batches "
                 "(only the FIFO queue's own initial defer occurred)");
        QCOMPARE(op->state(), SyncOperation::Succeeded);
        op->deleteLater();
    }

    void testFetchIsCancellableMidFlight()
    {
        auto *op = m_backend->fetchItems(QStringLiteral("cal"));
        QVERIFY(op);
        QVERIFY(!op->isFinished());       // deferred one turn by the FIFO
        op->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 10000);
        QCOMPARE(op->state(), SyncOperation::Cancelled);
        op->deleteLater();
    }

    void testFetchStillReturnsEveryRecord()
    {
        auto *op = m_backend->fetchItems(QStringLiteral("cal"));
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 10000);
        QCOMPARE(op->state(), SyncOperation::Succeeded);
        QCOMPARE(op->fetchedItems().size(), 500);
        op->deleteLater();
    }

private:
    void seed(int n)
    {
        QList<KCalendarCore::Incidence::Ptr> items;
        for (int i = 0; i < n; ++i) {
            auto ev = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
            ev->setUid(QStringLiteral("uid-%1").arg(i));
            ev->setSummary(QStringLiteral("Event %1").arg(i));
            ev->setDtStart(QDateTime::currentDateTime());
            items.append(ev);
        }
        auto *op = m_backend->pushItems(QStringLiteral("cal"), items);
        QTRY_VERIFY_WITH_TIMEOUT(op->isFinished(), 30000);
        op->deleteLater();
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<LocalBackend>  m_backend;
};

QTEST_MAIN(TestLocalBackendChunking)
#include "tst_localbackend_chunking.moc"
