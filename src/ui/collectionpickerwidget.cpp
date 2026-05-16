#include "collectionpickerwidget.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QVBoxLayout>

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

    bool anyCal = false, anyContacts = false;
    for (const auto &it : m_items) {
        auto *cb = new QCheckBox(it.name);
        cb->setObjectName(QStringLiteral("collection-%1").arg(it.id));
        QObject::connect(cb, &QCheckBox::toggled, this, [this]() {
            emit selectionChanged(selected());
        });
        if (it.type == QStringLiteral("calendar")) {
            calLayout->addWidget(cb); anyCal = true;
        } else if (it.type == QStringLiteral("contacts")) {
            contactsLayout->addWidget(cb); anyContacts = true;
        } else {
            layout()->addWidget(cb);
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
