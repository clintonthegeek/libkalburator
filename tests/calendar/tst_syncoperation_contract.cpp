// SPDX-License-Identifier: GPL-2.0-or-later

#include "syncoperation.h"

#include <QObject>
#include <QSignalSpy>
#include <QtTest>

using Kalburator::Sync::SyncOperation;

class TstSyncOperationContract : public QObject
{
    Q_OBJECT

private slots:
    void initialState();
    void setStateRunningEmitsStarted();
    void setStateSucceededEmitsFinishedOnce();
    void setStateFailedEmitsFinishedOnce();
    void setErrorImpliesFailed();
    void cancelSetsCancelRequested();
    void cancelIsIdempotent();
    void isFinishedReflectsTerminalStates();
};

namespace {

class FakeOp : public SyncOperation
{
    Q_OBJECT
public:
    FakeOp() : SyncOperation(QStringLiteral("test-cal")) {}
    using SyncOperation::setState;        // expose for test
    using SyncOperation::setError;        // expose for test
    using SyncOperation::cancelRequested; // expose for test
};

} // namespace

void TstSyncOperationContract::initialState()
{
    FakeOp op;
    QCOMPARE(op.state(), SyncOperation::Pending);
    QVERIFY(!op.isFinished());
    QVERIFY(op.errorString().isEmpty());
}

void TstSyncOperationContract::setStateRunningEmitsStarted()
{
    FakeOp op;
    QSignalSpy started(&op, &SyncOperation::started);
    QSignalSpy finished(&op, &SyncOperation::finished);
    op.setState(SyncOperation::Running);
    QCOMPARE(started.count(), 1);
    QCOMPARE(finished.count(), 0);
    QCOMPARE(op.state(), SyncOperation::Running);
    QVERIFY(!op.isFinished());
}

void TstSyncOperationContract::setStateSucceededEmitsFinishedOnce()
{
    FakeOp op;
    QSignalSpy finished(&op, &SyncOperation::finished);
    op.setState(SyncOperation::Running);
    op.setState(SyncOperation::Succeeded);
    QCOMPARE(finished.count(), 1);
    op.setState(SyncOperation::Succeeded);  // idempotent: still 1
    QCOMPARE(finished.count(), 1);
    QVERIFY(op.isFinished());
}

void TstSyncOperationContract::setStateFailedEmitsFinishedOnce()
{
    FakeOp op;
    QSignalSpy finished(&op, &SyncOperation::finished);
    op.setState(SyncOperation::Running);
    op.setState(SyncOperation::Failed);
    QCOMPARE(finished.count(), 1);
    QVERIFY(op.isFinished());
}

void TstSyncOperationContract::setErrorImpliesFailed()
{
    FakeOp op;
    QSignalSpy finished(&op, &SyncOperation::finished);
    op.setError(QStringLiteral("boom"));
    QCOMPARE(op.state(), SyncOperation::Failed);
    QCOMPARE(op.errorString(), QStringLiteral("boom"));
    QCOMPARE(finished.count(), 1);
}

void TstSyncOperationContract::cancelSetsCancelRequested()
{
    FakeOp op;
    QVERIFY(!op.cancelRequested());
    op.cancel();
    QVERIFY(op.cancelRequested());
}

void TstSyncOperationContract::cancelIsIdempotent()
{
    FakeOp op;
    op.cancel();
    op.cancel();  // no-op
    QVERIFY(op.cancelRequested());
}

void TstSyncOperationContract::isFinishedReflectsTerminalStates()
{
    {
        FakeOp op;
        op.setState(SyncOperation::Succeeded);
        QVERIFY(op.isFinished());
    }
    {
        FakeOp op;
        op.setState(SyncOperation::Failed);
        QVERIFY(op.isFinished());
    }
    {
        FakeOp op;
        op.setState(SyncOperation::Cancelled);
        QVERIFY(op.isFinished());
    }
    {
        FakeOp op;
        op.setState(SyncOperation::Running);
        QVERIFY(!op.isFinished());
    }
}

QTEST_MAIN(TstSyncOperationContract)
#include "tst_syncoperation_contract.moc"
