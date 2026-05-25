#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include "markdownfilesbackend.h"
#include "collectioninfo.h"
#include "backendrecord.h"
#include "shape.h"

using namespace Kalburator::Sinks;
using namespace Kalburator::Sync;
using namespace Kalburator::Shape;

class TestMarkdownFilesBackend : public QObject {
    Q_OBJECT

    static BackendRecord rec(const QString &id, const QByteArray &data) {
        BackendRecord r; r.id = id; r.data = data; return r;
    }

    static CollectionInfo noteCollection() {
        CollectionInfo ci; ci.id = QStringLiteral("note+markdown"); ci.name = QStringLiteral("Notes");
        return ci;
    }

private slots:
    void writesTitleNamedMarkdownFile() {
        QTemporaryDir dir;
        MarkdownFilesBackend b(dir.path());
        const auto ci = noteCollection();
        b.createCollection(ci, Shape{ DomainId{"note"}, EncodingId{"markdown"} });

        const QByteArray md = "---\nid: 5\n---\n\nShopping list\nmilk\neggs\n";
        const QString path = b.createRecord(ci.id, rec(QStringLiteral("5"), md));

        QVERIFY(path.endsWith(QStringLiteral("Shopping_list.md")));
        QVERIFY(QFile::exists(path));
    }

    void fallsBackToNoteIdForEmptyBody() {
        QTemporaryDir dir;
        MarkdownFilesBackend b(dir.path());
        const auto ci = noteCollection();
        b.createCollection(ci, Shape{ DomainId{"note"}, EncodingId{"markdown"} });

        const QByteArray md = "---\nid: 8\n---\n\n";
        const QString path = b.createRecord(ci.id, rec(QStringLiteral("8"), md));
        QVERIFY(path.endsWith(QStringLiteral("note_8.md")));
    }

    void roundTripsRecordViaDisk() {
        QTemporaryDir dir;
        MarkdownFilesBackend b(dir.path());
        const auto ci = noteCollection();
        b.createCollection(ci, Shape{ DomainId{"note"}, EncodingId{"markdown"} });

        const QByteArray md = "---\nid: 5\n---\n\nShopping list\n";
        b.createRecord(ci.id, rec(QStringLiteral("5"), md));

        const auto loaded = b.loadRecords(ci.id);
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().data, md);
    }
};

QTEST_GUILESS_MAIN(TestMarkdownFilesBackend)
#include "tst_markdownfiles_backend.moc"
