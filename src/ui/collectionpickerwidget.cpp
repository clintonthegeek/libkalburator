#include "collectionpickerwidget.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace Kalburator::Ui {

CollectionPickerWidget::CollectionPickerWidget(QWidget *parent)
    : QWidget(parent)
{
    setLayout(new QVBoxLayout(this));
}

void CollectionPickerWidget::setCollections(const QList<Sync::CollectionInfo> &items)
{
    m_items = items;
    rebuild();
}

void CollectionPickerWidget::rebuild()
{
    // Clear existing children
    while (auto *child = layout()->takeAt(0)) {
        delete child->widget();
        delete child;
    }

    auto *calGroup      = new QGroupBox(tr("Calendars"),      this);
    auto *contactsGroup = new QGroupBox(tr("Address Books"),  this);
    auto *calLayout      = new QVBoxLayout(calGroup);
    auto *contactsLayout = new QVBoxLayout(contactsGroup);

    // O.1.5: chip factory
    auto makeChip = [](const QString &text, const QString &objName) -> QLabel * {
        auto *chip = new QLabel(text);
        chip->setObjectName(objName);
        chip->setStyleSheet(QStringLiteral(
            "QLabel { padding: 1px 4px; margin-left: 4px; "
            "border: 1px solid palette(mid); border-radius: 4px; "
            "font-size: 9pt; }"));
        return chip;
    };

    bool anyCal = false, anyContacts = false;
    for (const auto &it : m_items) {
        // Each collection gets a row widget with checkbox + chips
        auto *rowWidget = new QWidget;
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        auto *cb = new QCheckBox(it.name);
        cb->setObjectName(QStringLiteral("collection-%1").arg(it.id));
        QObject::connect(cb, &QCheckBox::toggled, this, [this]() {
            emit selectionChanged(selected());
        });
        rowLayout->addWidget(cb);

        // O.1.5: content-type chips
        for (const QString &ct : it.contentTypes) {
            rowLayout->addWidget(makeChip(ct, QStringLiteral("chip-%1").arg(ct)));
        }
        // O.1.5: read-only chip + disable checkbox
        if (it.readOnly) {
            rowLayout->addWidget(makeChip(tr("read-only"), QStringLiteral("chip-readonly-%1").arg(it.id)));
            cb->setEnabled(false);
        }
        rowLayout->addStretch();

        if (it.type == QStringLiteral("calendar")) {
            calLayout->addWidget(rowWidget); anyCal = true;
        } else if (it.type == QStringLiteral("contacts")) {
            contactsLayout->addWidget(rowWidget); anyContacts = true;
        } else {
            layout()->addWidget(rowWidget);
        }
    }
    if (anyCal)      layout()->addWidget(calGroup);      else delete calGroup;
    if (anyContacts) layout()->addWidget(contactsGroup); else delete contactsGroup;
}

QStringList CollectionPickerWidget::selected() const
{
    QStringList out;
    const auto boxes = findChildren<QCheckBox*>();
    for (auto *b : boxes)
        if (b->isChecked()) {
            const QString name = b->objectName();
            if (name.startsWith(QStringLiteral("collection-")))
                out << name.mid(QStringLiteral("collection-").length());
        }
    return out;
}

} // namespace Kalburator::Ui
