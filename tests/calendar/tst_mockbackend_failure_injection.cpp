// tests/calendar/tst_mockbackend_failure_injection.cpp
// Phase F2 Task 6 — pin MockBackend's unified failure injection on the
// 3-arg pushItems(id, items, plan) path. Both OnPush and OnStoreItems
// must drive the operation to Failed symmetrically.

#include <QtTest>
#include <QSignalSpy>

#include "mockbackend.h"
#include "syncoperation.h"
#include "transcodingplan.h"

#include <KCalendarCore/Event>

using namespace Kalburator::Sync;

class TstMockBackendFailureInjection : public QObject
{
    Q_OBJECT
private slots:
    void pushItemsHonoursOnPushFailureInjection();
    void pushItemsHonoursOnStoreItemsFailureInjection();
    void pushItemsThreeArgSucceedsWithoutFailureInjection();
    void twoArgPushItemsDelegatesToThreeArgForm();
};

namespace {

QList<KCalendarCore::Incidence::Ptr> makeOneEvent(const QString &uid)
{
    auto event = KCalendarCore::Event::Ptr::create();
    event->setUid(uid);
    event->setSummary(QStringLiteral("test event"));
    return {event};
}

} // namespace

void TstMockBackendFailureInjection::pushItemsHonoursOnPushFailureInjection()
{
    MockBackend mock;
    mock.setFailurePoint(MockBackend::FailurePoint::OnPush);

    auto items = makeOneEvent(QStringLiteral("evt-1"));
    auto *op = mock.pushItems(QStringLiteral("cal1"), items, TranscodingPlan{});
    QVERIFY(op);

    QSignalSpy finished(op, &SyncOperation::finished);
    QVERIFY(finished.wait(1000));

    QCOMPARE(op->state(), SyncOperation::Failed);
    QVERIFY(!op->errorString().isEmpty());
    op->deleteLater();
}

void TstMockBackendFailureInjection::pushItemsHonoursOnStoreItemsFailureInjection()
{
    MockBackend mock;
    mock.setFailurePoint(MockBackend::FailurePoint::OnStoreItems);

    auto items = makeOneEvent(QStringLiteral("evt-2"));
    auto *op = mock.pushItems(QStringLiteral("cal1"), items, TranscodingPlan{});
    QVERIFY(op);

    QSignalSpy finished(op, &SyncOperation::finished);
    QVERIFY(finished.wait(1000));

    QCOMPARE(op->state(), SyncOperation::Failed);
    QVERIFY(!op->errorString().isEmpty());
    op->deleteLater();
}

void TstMockBackendFailureInjection::pushItemsThreeArgSucceedsWithoutFailureInjection()
{
    MockBackend mock;
    auto items = makeOneEvent(QStringLiteral("evt-3"));
    auto *op = mock.pushItems(QStringLiteral("cal1"), items, TranscodingPlan{});
    QVERIFY(op);

    QSignalSpy finished(op, &SyncOperation::finished);
    QVERIFY(finished.wait(1000));

    QCOMPARE(op->state(), SyncOperation::Succeeded);
    QCOMPARE(op->succeededUids(), QStringList{QStringLiteral("evt-3")});
    QCOMPARE(mock.allUids(QStringLiteral("cal1")),
             QStringList{QStringLiteral("evt-3")});
    op->deleteLater();
}

void TstMockBackendFailureInjection::twoArgPushItemsDelegatesToThreeArgForm()
{
    // Sanity: the 2-arg convenience wrapper (non-virtual inline in
    // SyncBackend) must forward to the 3-arg override and exercise the
    // same failure-injection path.
    MockBackend mock;
    mock.setFailurePoint(MockBackend::FailurePoint::OnStoreItems);

    auto items = makeOneEvent(QStringLiteral("evt-4"));
    auto *op = mock.pushItems(QStringLiteral("cal1"), items, TranscodingPlan{});
    QVERIFY(op);

    QSignalSpy finished(op, &SyncOperation::finished);
    QVERIFY(finished.wait(1000));

    QCOMPARE(op->state(), SyncOperation::Failed);
    QVERIFY(!op->errorString().isEmpty());
    op->deleteLater();
}

QTEST_MAIN(TstMockBackendFailureInjection)
#include "tst_mockbackend_failure_injection.moc"
