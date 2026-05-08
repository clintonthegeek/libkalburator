#include <QHash>
#include <QTest>

#include "shape.h"

using namespace Kalburator::Shape;

class TestShape : public QObject {
    Q_OBJECT
private slots:
    void equality() {
        Shape a{ DomainId{QStringLiteral("calendar")},
                 EncodingId{QStringLiteral("ical")} };
        Shape b{ DomainId{QStringLiteral("calendar")},
                 EncodingId{QStringLiteral("ical")} };
        Shape c{ DomainId{QStringLiteral("contacts")},
                 EncodingId{QStringLiteral("vcard4")} };
        QCOMPARE(a, b);
        QVERIFY(a != c);
    }

    void anySentinel() {
        Shape a = Shape::Any();
        Shape b = Shape::Any();
        QCOMPARE(a, b);
        QVERIFY(a.isAny());

        Shape c{ DomainId{QStringLiteral("calendar")},
                 EncodingId{QStringLiteral("ical")} };
        QVERIFY(!c.isAny());
    }

    void toStringFormat() {
        Shape s{ DomainId{QStringLiteral("calendar")},
                 EncodingId{QStringLiteral("ical")} };
        QCOMPARE(s.toString(), QStringLiteral("calendar+ical"));
        QCOMPARE(Shape::Any().toString(), QStringLiteral("any"));
    }

    void hashable() {
        QHash<Shape, int> h;
        h.insert(Shape{ DomainId{QStringLiteral("calendar")},
                        EncodingId{QStringLiteral("ical")} }, 1);
        h.insert(Shape::Any(), 2);
        QCOMPARE(h.value(Shape{ DomainId{QStringLiteral("calendar")},
                                EncodingId{QStringLiteral("ical")} }), 1);
        QCOMPARE(h.value(Shape::Any()), 2);
        QCOMPARE(h.size(), 2);
    }

    void domainAndEncodingIdHashable() {
        QHash<DomainId, int> dh;
        dh.insert(DomainId{QStringLiteral("calendar")}, 1);
        QCOMPARE(dh.value(DomainId{QStringLiteral("calendar")}), 1);

        QHash<EncodingId, int> eh;
        eh.insert(EncodingId{QStringLiteral("ical")}, 1);
        QCOMPARE(eh.value(EncodingId{QStringLiteral("ical")}), 1);
    }
};

QTEST_GUILESS_MAIN(TestShape)
#include "tst_shape.moc"
