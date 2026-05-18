#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>

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
    void chipsRenderForCapabilityFields();
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

void TstCollectionPickerWidget::chipsRenderForCapabilityFields()
{
    Ui::CollectionPickerWidget w;
    QList<CollectionInfo> items;

    CollectionInfo eventsOnly;
    eventsOnly.id = QStringLiteral("id1");
    eventsOnly.name = QStringLiteral("Events");
    eventsOnly.type = QStringLiteral("calendar");
    eventsOnly.readOnly = false;
    eventsOnly.contentTypes = {QStringLiteral("VEVENT")};

    CollectionInfo readOnlyTodos;
    readOnlyTodos.id = QStringLiteral("id2");
    readOnlyTodos.name = QStringLiteral("Tasks");
    readOnlyTodos.type = QStringLiteral("calendar");
    readOnlyTodos.readOnly = true;
    readOnlyTodos.contentTypes = {QStringLiteral("VTODO")};

    items << eventsOnly << readOnlyTodos;
    w.setCollections(items);

    const auto eventChips = w.findChildren<QLabel*>(QStringLiteral("chip-VEVENT"));
    const auto todoChips  = w.findChildren<QLabel*>(QStringLiteral("chip-VTODO"));

    // Find all read-only chips (objectName starts with "chip-readonly-")
    const auto allLabels = w.findChildren<QLabel*>();
    QList<QLabel*> roChips;
    for (auto *l : allLabels) {
        if (l->objectName().startsWith(QStringLiteral("chip-readonly-")))
            roChips.append(l);
    }

    QCOMPARE(eventChips.size(), 1);
    QCOMPARE(todoChips.size(),  1);
    QCOMPARE(roChips.size(),    1);   // only the readOnlyTodos row

    // Verify the read-only collection's checkbox is disabled.
    const auto *roCheckbox = w.findChild<QCheckBox*>(QStringLiteral("collection-id2"));
    QVERIFY(roCheckbox != nullptr);
    QVERIFY(!roCheckbox->isEnabled());
}

QTEST_MAIN(TstCollectionPickerWidget)
#include "tst_collectionpickerwidget.moc"
