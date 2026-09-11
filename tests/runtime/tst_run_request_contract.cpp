#include <QtTest/QtTest>

#include <kalburator/runtime/collectionruntime.h>

using namespace Kalburator::Runtime;

class TestRunRequestContract : public QObject
{
    Q_OBJECT
private slots:
    void allEnabledIsExplicit();
    void exactSetMustBeNonEmpty();
    void oneRequiresExactlyOneId();
    void noneCannotCarryIds();
    void everyIntentUsesSameSelectionValidation();
};

void TestRunRequestContract::allEnabledIsExplicit()
{
    QString error;
    const RunRequest request{RunSelection::allEnabled(), RunIntent::Normal};
    QVERIFY(request.validate(error));
    QVERIFY(error.isEmpty());
}

void TestRunRequestContract::exactSetMustBeNonEmpty()
{
    QString error;
    const RunRequest empty{RunSelection::exactSet({}), RunIntent::Normal};
    QVERIFY(!empty.validate(error));
    QVERIFY(!error.isEmpty());
    const RunRequest oneSet{RunSelection::exactSet({QStringLiteral("m1")}), RunIntent::Normal};
    QVERIFY(oneSet.validate(error));
}

void TestRunRequestContract::oneRequiresExactlyOneId()
{
    QString error;
    const RunRequest emptyOne{RunSelection::one({}), RunIntent::Normal};
    QVERIFY(!emptyOne.validate(error));
    const RunRequest many{RunSelection{RunSelection::Kind::One,
                                       {QStringLiteral("a"), QStringLiteral("b")}},
                          RunIntent::Normal};
    QVERIFY(!many.validate(error));
    const RunRequest one{RunSelection::one(QStringLiteral("a")), RunIntent::Normal};
    QVERIFY(one.validate(error));
}

void TestRunRequestContract::noneCannotCarryIds()
{
    QString error;
    const RunRequest none{RunSelection::none(), RunIntent::Normal};
    QVERIFY(none.validate(error));
    const RunRequest invalid{RunSelection{RunSelection::Kind::None,
                                          {QStringLiteral("unexpected")}},
                             RunIntent::Normal};
    QVERIFY(!invalid.validate(error));
}

void TestRunRequestContract::everyIntentUsesSameSelectionValidation()
{
    for (const auto intent : {RunIntent::Normal, RunIntent::FullRediff,
                              RunIntent::Mirror, RunIntent::DestructiveRebuild}) {
        QString error;
        const RunRequest all{RunSelection::allEnabled(), intent};
        const RunRequest empty{RunSelection::exactSet({}), intent};
        QVERIFY(all.validate(error));
        QVERIFY(!empty.validate(error));
    }
}

QTEST_MAIN(TestRunRequestContract)
#include "tst_run_request_contract.moc"
