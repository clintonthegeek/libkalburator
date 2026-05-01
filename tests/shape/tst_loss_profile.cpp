#include <QTest>

#include "lossprofile.h"

using namespace Kalburator::Shape;

namespace {
LossProfile lossless() { return LossProfile{}; }

LossProfile intraLossy(const QStringList& props) {
    LossProfile p;
    p.level = LossLevel::IntraDomainLossy;
    for (const auto& s : props) p.dropped.insert(PropertyId{s});
    return p;
}

LossProfile interProj(const QStringList& props) {
    LossProfile p;
    p.level = LossLevel::InterDomainProjection;
    for (const auto& s : props) p.dropped.insert(PropertyId{s});
    return p;
}

LossProfile degenerate(const QStringList& props) {
    LossProfile p;
    p.level = LossLevel::Degenerate;
    for (const auto& s : props) p.dropped.insert(PropertyId{s});
    return p;
}
}  // namespace

class TestLossProfile : public QObject {
    Q_OBJECT
private slots:
    void defaultIsLossless() {
        LossProfile p;
        QCOMPARE(p.level, LossLevel::Lossless);
        QVERIFY(p.dropped.isEmpty());
        QVERIFY(p.isLossless());
    }

    void composeLosslessLossless() {
        const LossProfile r = lossless().compose(lossless());
        QCOMPARE(r.level, LossLevel::Lossless);
        QVERIFY(r.dropped.isEmpty());
    }

    void composePromotesLevel() {
        const LossProfile r = lossless().compose(intraLossy({QStringLiteral("attendees")}));
        QCOMPARE(r.level, LossLevel::IntraDomainLossy);
        QCOMPARE(r.dropped.size(), 1);
        QVERIFY(r.dropped.contains(PropertyId{QStringLiteral("attendees")}));
    }

    void composeMaxLevelWins() {
        const LossProfile r =
            intraLossy({QStringLiteral("a")}).compose(degenerate({QStringLiteral("b")}));
        QCOMPARE(r.level, LossLevel::Degenerate);
        QCOMPARE(r.dropped.size(), 2);
    }

    void composeIsAssociative() {
        // Verify (a.compose(b)).compose(c) == a.compose(b.compose(c))
        const auto a = intraLossy({QStringLiteral("a1"), QStringLiteral("a2")});
        const auto b = interProj({QStringLiteral("b1")});
        const auto c = degenerate({QStringLiteral("c1")});

        const auto left = a.compose(b).compose(c);
        const auto right = a.compose(b.compose(c));

        QCOMPARE(left.level, right.level);
        QCOMPARE(left.dropped, right.dropped);
    }

    void composeUnionsDroppedSets() {
        const LossProfile r =
            intraLossy({QStringLiteral("x"), QStringLiteral("y")})
                .compose(intraLossy({QStringLiteral("y"), QStringLiteral("z")}));
        QCOMPARE(r.dropped.size(), 3);  // x, y, z (y deduped)
    }

    void summaryFormat() {
        QCOMPARE(LossProfile{}.summary(), QStringLiteral("lossless"));
        QCOMPARE(intraLossy({QStringLiteral("attendees"), QStringLiteral("attachments")}).summary(),
                 QStringLiteral("intra-lossy: drops attachments, attendees"));
        QCOMPARE(interProj({QStringLiteral("recurrenceRule")}).summary(),
                 QStringLiteral("inter-projection: drops recurrenceRule"));
    }
};

QTEST_GUILESS_MAIN(TestLossProfile)
#include "tst_loss_profile.moc"
