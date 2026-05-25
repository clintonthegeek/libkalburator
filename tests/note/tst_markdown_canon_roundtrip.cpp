#include <QtTest>
#include "markdowncanonstages.h"
#include "canonenvelope.h"

using namespace Kalburator::Note;
using namespace Kalburator::Shape;

class TestMarkdownCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:
    void roundTripsMarkdownWithFrontmatterByteForByte() {
        const QByteArray md =
            "---\n"
            "id: 42\n"
            "category: 3\n"
            "private: true\n"
            "---\n"
            "\n"
            "# Heading\n"
            "\n"
            "- item one\n"
            "- item two\n"
            "\n"
            "Some *emphasis* and `code`.\n";

        MarkdownToCanonStage promote;
        CanonToMarkdownStage demote;

        const QByteArray canon = promote.transform(md);
        const QByteArray back   = demote.transform(canon);
        QCOMPARE(back, md);   // byte-for-byte
    }

    void extractsUidFromIdLine() {
        const QByteArray md = "---\nid: 42\ncategory: 3\n---\n\nbody\n";
        MarkdownToCanonStage promote;
        const QJsonObject obj = CanonEnvelope::parse(promote.transform(md));
        QCOMPARE(CanonEnvelope::uid(obj), QStringLiteral("42"));
        // Body is stored verbatim, so the source's trailing newline is kept.
        QCOMPARE(obj.value(QStringLiteral("body")).toString(), QStringLiteral("body\n"));
    }

    void carriesFrontmatterVerbatimInProviderExtras() {
        const QByteArray md = "---\nid: 7\ncategory: 2\ncategoryName: Work\n---\n\nhi\n";
        MarkdownToCanonStage promote;
        const QJsonObject obj = CanonEnvelope::parse(promote.transform(md));
        const QString fm = obj.value(CanonEnvelope::providerExtrasKey())
                              .toObject().value(QStringLiteral("frontmatter")).toString();
        QCOMPARE(fm, QStringLiteral("id: 7\ncategory: 2\ncategoryName: Work"));
    }

    void handlesNoFrontmatter() {
        const QByteArray md = "just a plain note\nsecond line\n";
        MarkdownToCanonStage promote;
        CanonToMarkdownStage demote;
        const QByteArray canon = promote.transform(md);
        const QJsonObject obj = CanonEnvelope::parse(canon);
        QVERIFY(obj.value(CanonEnvelope::providerExtrasKey()).toObject()
                   .value(QStringLiteral("frontmatter")).isUndefined());
        QCOMPARE(CanonEnvelope::uid(obj), QString());
        QCOMPARE(demote.transform(canon), md);   // byte-for-byte
    }

    void normalisesBodyToSingleTrailingNewline() {
        const QByteArray noNl = "---\nid: 1\n---\n\nbody no newline";
        MarkdownToCanonStage promote;
        CanonToMarkdownStage demote;
        const QByteArray out = demote.transform(promote.transform(noNl));
        QVERIFY(out.endsWith("body no newline\n"));
        QVERIFY(!out.endsWith("\n\n"));
    }
};

QTEST_GUILESS_MAIN(TestMarkdownCanonRoundtrip)
#include "tst_markdown_canon_roundtrip.moc"
