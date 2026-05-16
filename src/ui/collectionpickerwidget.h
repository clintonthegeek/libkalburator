#ifndef KALBURATOR_UI_COLLECTIONPICKERWIDGET_H
#define KALBURATOR_UI_COLLECTIONPICKERWIDGET_H

#include "collectioninfo.h"
#include <QWidget>
#include <QStringList>

namespace Kalburator::Ui {

class CollectionPickerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CollectionPickerWidget(QWidget *parent = nullptr);

    void setCollections(const QList<Sync::CollectionInfo> &items);
    QStringList selected() const;

signals:
    void selectionChanged(const QStringList &selectedIds);

private:
    void rebuild();
    QList<Sync::CollectionInfo> m_items;
};

} // namespace Kalburator::Ui

#endif
