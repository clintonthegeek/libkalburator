#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QCheckBox>
#include <QGroupBox>

#include "../../src/ui/collectionpickerwidget.h"
#include "collectioninfo.h"

using namespace Kalburator;
using namespace Kalburator::Sync;

class TstCollectionPickerWidget : public QObject
{
    Q_OBJECT
private slots:
    void rendersCheckboxPerCollection();
    void toggleEmitsSelectionChanged();
    void groupsByType();
};

static QList<CollectionInfo> makeFixture()
{
    CollectionInfo a, b, c;
    a.id = QStringLiteral("cal-1"); a.name = QStringLiteral("Work");
    a.type = QStringLiteral("calendar");
    b.id = QStringLiteral("cal-2"); b.name = QStringLiteral("Personal");
    b.type = QStringLiteral("calendar");
    c.id = QStringLiteral("ab-1");  c.name = QStringLiteral("Family");
    c.type = QStringLiteral("contacts");
    return { a, b, c };
}

void TstCollectionPickerWidget::rendersCheckboxPerCollection()
{
    Ui::CollectionPickerWidget w;
    w.setCollections(makeFixture());
    const auto boxes = w.findChildren<QCheckBox*>();
    QVERIFY(boxes.size() >= 3);
}

void TstCollectionPickerWidget::toggleEmitsSelectionChanged()
{
    Ui::CollectionPickerWidget w;
    w.setCollections(makeFixture());
    QSignalSpy spy(&w, &Ui::CollectionPickerWidget::selectionChanged);
    auto *box = w.findChild<QCheckBox*>(QStringLiteral("collection-cal-1"));
    QVERIFY(box != nullptr);
    box->setChecked(true);
    QCOMPARE(spy.count(), 1);
    QStringList selected = spy.first().first().toStringList();
    QVERIFY(selected.contains(QStringLiteral("cal-1")));
}

void TstCollectionPickerWidget::groupsByType()
{
    Ui::CollectionPickerWidget w;
    w.setCollections(makeFixture());
    const auto groups = w.findChildren<QGroupBox*>();
    QStringList titles;
    for (auto *g : groups) titles << g->title();
    QVERIFY(titles.contains(QStringLiteral("Calendars")));
    QVERIFY(titles.contains(QStringLiteral("Address Books")));
}

QTEST_MAIN(TstCollectionPickerWidget)
#include "tst_collectionpickerwidget.moc"
