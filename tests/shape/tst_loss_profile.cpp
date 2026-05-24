#include <QTest>
#include "lossprofile.h"

using namespace Kalburator::Shape;

static LossProfile dropped(std::initializer_list<const char*> props) {
    LossProfile p;
    for (const auto* s : props) p.affected.insert(PropertyId{QString::fromUtf8(s)}, LossKind::Dropped);
    return p;
}

class TestLossProfile : public QObject {
    Q_OBJECT
private slots:
    void losslessByDefault() {
        LossProfile p;
        QVERIFY(p.isLossless());
        QCOMPARE(p.summary(), QStringLiteral("lossless"));
    }

    void composeUnionsAffected() {
        const LossProfile r = dropped({"x", "y"}).compose(dropped({"y", "z"}));
        QCOMPARE(r.affected.size(), 3);            // x, y, z (y deduped)
        QVERIFY(!r.isLossless());
    }

    void composeKeepsMoreSevereKind() {
        LossProfile a; a.affected.insert(PropertyId{QStringLiteral("rrule")}, LossKind::Reversible);
        LossProfile b; b.affected.insert(PropertyId{QStringLiteral("rrule")}, LossKind::Dropped);
        const LossProfile r = a.compose(b);
        QCOMPARE(r.affected.value(PropertyId{QStringLiteral("rrule")}), LossKind::Dropped);
    }

    void droppedPropertiesFiltersByKind() {
        LossProfile p;
        p.affected.insert(PropertyId{QStringLiteral("gender")}, LossKind::Dropped);
        p.affected.insert(PropertyId{QStringLiteral("rrule")}, LossKind::Simplified);
        const auto d = p.droppedProperties();
        QCOMPARE(d.size(), 1);
        QVERIFY(d.contains(PropertyId{QStringLiteral("gender")}));
    }

    void summaryGroupsByKind() {
        LossProfile p;
        p.affected.insert(PropertyId{QStringLiteral("gender")}, LossKind::Dropped);
        p.affected.insert(PropertyId{QStringLiteral("rrule")}, LossKind::Simplified);
        QCOMPARE(p.summary(), QStringLiteral("drops gender; simplifies rrule"));
    }

    void composeIntersectsLosslessValues() {
        // classification: Degraded, but "public"/"private"/"confidential" are
        // representable losslessly. A second hop that only spares "public" must
        // narrow the safe set to the intersection (a value is lossless only if
        // every constraining hop agrees).
        LossProfile a;
        a.affected.insert(PropertyId{QStringLiteral("classification")}, LossKind::Degraded);
        a.losslessValues.insert(PropertyId{QStringLiteral("classification")},
                                {QStringLiteral("public"), QStringLiteral("private"),
                                 QStringLiteral("confidential")});
        LossProfile b;
        b.affected.insert(PropertyId{QStringLiteral("classification")}, LossKind::Degraded);
        b.losslessValues.insert(PropertyId{QStringLiteral("classification")},
                                {QStringLiteral("public")});

        const LossProfile r = a.compose(b);
        const auto safe = r.losslessValues.value(PropertyId{QStringLiteral("classification")});
        QCOMPARE(safe.size(), 1);
        QVERIFY(safe.contains(QStringLiteral("public")));
        QVERIFY(!safe.contains(QStringLiteral("private")));
    }

    void composeCarriesUnsharedLosslessValues() {
        // A property constrained on only one side keeps its full safe set.
        LossProfile a;
        a.affected.insert(PropertyId{QStringLiteral("classification")}, LossKind::Degraded);
        a.losslessValues.insert(PropertyId{QStringLiteral("classification")},
                                {QStringLiteral("public"), QStringLiteral("private")});
        LossProfile b;  // b touches a different property, nothing for classification
        b.affected.insert(PropertyId{QStringLiteral("rrule")}, LossKind::Simplified);

        const LossProfile r = a.compose(b);
        QCOMPARE(r.losslessValues.value(PropertyId{QStringLiteral("classification")}).size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestLossProfile)
#include "tst_loss_profile.moc"
