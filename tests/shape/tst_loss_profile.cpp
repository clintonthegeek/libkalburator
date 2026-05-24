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
};

QTEST_GUILESS_MAIN(TestLossProfile)
#include "tst_loss_profile.moc"
