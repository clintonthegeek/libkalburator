#include <QtTest/QtTest>

#include <QThread>
#include <memory>

#include "backendexecutor.h"
#include "mockbackend.h"

using namespace Kalburator::Sync;

class BackendExecutorTest final : public QObject
{
    Q_OBJECT

private slots:
    void ownsAffinityAndDestruction()
    {
        auto backend = std::make_unique<MockBackend>(QStringLiteral("executor-test"));
        auto executor = std::make_unique<BackendExecutor>(std::move(backend));

        QVERIFY(executor->start());
        QVERIFY(executor->isRunning());

        QThread *operationThread = nullptr;
        QVERIFY(executor->invoke([&]() {
            operationThread = QThread::currentThread();
            QVERIFY(executor->backendObject()->thread() == operationThread);
        }));
        QCOMPARE(operationThread, executor->thread());
        QVERIFY(executor->shutdown(2000));
        QVERIFY(!executor->isRunning());
        QVERIFY(executor->backend() == nullptr);
    }
};

QTEST_MAIN(BackendExecutorTest)
#include "tst_backend_executor.moc"
