#include <QTest>

#include "contactsdomaindefinition.h"
#include "contactsstockshapes.h"
#include "shaperegistries.h"
#include "vcard3to4transformation.h"

#include <KContacts/Addressee>
#include <KContacts/VCardConverter>

using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;
using Kalburator::Shape::LossProfile;
using Kalburator::Contacts::VCard3To4Stage;
using Kalburator::Contacts::VCard4To3Stage;
using Kalburator::Contacts::vcard4ToVcard3Loss;

namespace {

KContacts::Addressee parseFirst(const QByteArray &bytes)
{
    KContacts::VCardConverter conv;
    auto list = conv.parseVCards(bytes);
    return list.isEmpty() ? KContacts::Addressee{} : list.first();
}

} // namespace

class TestVCard3VCard4Edge : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        // Each slot gets a fresh bundle (preserves the prior cleanup()
        // reset semantics now that the registries are injected).
        m_shape = {};
    }

    void v3ToV4PreservesCoreProperties()
    {
        const QByteArray v3 =
            "BEGIN:VCARD\r\nVERSION:3.0\r\n"
            "UID:abc\r\nFN:Alice\r\nORG:Acme\r\nEMAIL:a@x.y\r\n"
            "END:VCARD\r\n";
        VCard3To4Stage stage;
        const auto out = stage.transform(v3);
        const auto a = parseFirst(out);
        QCOMPARE(a.formattedName(), QStringLiteral("Alice"));
        QCOMPARE(a.organization(), QStringLiteral("Acme"));
        QVERIFY(out.contains("VERSION:4.0"));
    }

    void v4ToV3DropsV4Properties()
    {
        const QByteArray v4 =
            "BEGIN:VCARD\r\nVERSION:4.0\r\n"
            "UID:abc\r\nFN:Bob\r\nGENDER:M\r\n"
            "END:VCARD\r\n";
        VCard4To3Stage stage;
        const auto out = stage.transform(v4);
        QVERIFY(out.contains("VERSION:3.0"));
        // We don't pin the exact property-set here — the loss profile
        // declares what's dropped. Verify v4's structural data made it
        // through:
        const auto a = parseFirst(out);
        QCOMPARE(a.formattedName(), QStringLiteral("Bob"));
    }

    void lossProfileDeclaresIntraDomainLossy()
    {
        const auto loss = vcard4ToVcard3Loss();
        QVERIFY(!loss.isLossless());
        QVERIFY(!loss.droppedProperties().isEmpty());
    }

    void registryCompilesPipelineV3ToV4()
    {
        // Set up via ContactsDomainDefinition::canonicalSpine() + ContactsStockShapes,
        // mirroring how PluginManager loads the contacts plugin.
        // Spine is [vcard4, canon], so v3→v4 is a direct single-hop
        // (v3 peer attaches to spine[0]=vcard4).
        auto& reg = m_shape.transformation;
        Kalburator::Contacts::ContactsDomainDefinition def;
        const auto spine = def.canonicalSpine();
        const auto& [rootShape, rootCat] = spine.first();
        reg.registerShape(rootShape, rootCat);
        reg.declareCanonical(DomainId{"contacts"}, rootShape);
        for (int i = 1; i < spine.size(); ++i) {
            const auto& [s, cat] = spine.at(i);
            reg.registerShape(s, cat);
            reg.appendCanonicalVersion(DomainId{"contacts"}, s);
        }

        Kalburator::Contacts::ContactsStockShapes shapes;
        for (const auto &[shape, cat] : shapes.peerShapes())
            reg.registerShape(shape, cat);
        for (const auto &edge : shapes.edges())
            reg.registerEdge(edge);

        const Shape v3{ DomainId{"contacts"}, EncodingId{"vcard3"} };
        const Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };

        // v3 attaches at spine[0]=v4; compile(v3,v4) = single-hop (v3→v4 direct edge).
        const auto pipeline = reg.compile(v3, v4);
        QVERIFY(pipeline.has_value());

        // Round-trip through the compiled pipeline.
        const QByteArray src =
            "BEGIN:VCARD\r\nVERSION:3.0\r\n"
            "UID:x\r\nFN:Alice\r\nEND:VCARD\r\n";
        const auto out = pipeline->apply(src);
        QVERIFY(out.contains("VERSION:4.0"));
    }

    // Step 4: assert the actual KContacts v4→v3 drop behavior matches the
    // declared loss profile. Constructs a v4 vCard with each
    // declared-dropped property populated, runs VCard4To3Stage, parses
    // the result back, and checks each property is in fact missing from
    // the resulting Addressee. If KContacts keeps a property (e.g. via
    // the X-Anniversary extension), this slot will fail and tell us to
    // refine vcard4ToVcard3Loss().
    void declaredDropsMatchKContactsReality()
    {
        const QByteArray v4 =
            "BEGIN:VCARD\r\nVERSION:4.0\r\n"
            "UID:lossy\r\nFN:Carol\r\n"
            "GENDER:F\r\n"
            "KIND:individual\r\n"
            "ANNIVERSARY:20100615\r\n"
            "LANG:en\r\n"
            "MEMBER:urn:uuid:03a0e51f-d1aa-4385-8a53-e29025acd8af\r\n"
            "END:VCARD\r\n";

        VCard4To3Stage stage;
        const QByteArray out = stage.transform(v4);
        QVERIFY(out.contains("VERSION:3.0"));

        const auto a = parseFirst(out);
        QCOMPARE(a.formattedName(), QStringLiteral("Carol"));

        // For each property the LossProfile declares dropped, verify
        // KContacts actually dropped it on the v4→v3 serialize.
        const auto loss = vcard4ToVcard3Loss();
        for (const auto &p : loss.droppedProperties()) {
            const auto name = p.toString();
            if (name == QStringLiteral("gender")) {
                QVERIFY2(a.gender().gender().isEmpty(),
                         "gender survived v4->v3 unexpectedly");
            } else if (name == QStringLiteral("kind")) {
                QVERIFY2(a.kind().isEmpty(),
                         "kind survived v4->v3 unexpectedly");
            } else if (name == QStringLiteral("anniversary")) {
                // KContacts stores anniversary as an X-Anniversary
                // extension on v3 output. If this fails, the
                // LossProfile should be updated to remove "anniversary"
                // from the dropped set.
                QVERIFY2(!a.anniversary().isValid(),
                         "anniversary survived v4->v3 (likely via "
                         "X-Anniversary) — update LossProfile");
            } else if (name == QStringLiteral("lang")) {
                QVERIFY2(a.langs().isEmpty(),
                         "lang survived v4->v3 unexpectedly");
            } else if (name == QStringLiteral("member")) {
                QVERIFY2(a.members().isEmpty(),
                         "member survived v4->v3 unexpectedly");
            }
        }
    }

private:
    Kalburator::Shape::ShapeRegistries m_shape;
};

QTEST_GUILESS_MAIN(TestVCard3VCard4Edge)
#include "tst_vcard3_vcard4_edge.moc"
