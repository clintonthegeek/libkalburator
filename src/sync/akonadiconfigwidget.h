#ifndef KALBURATOR_SYNC_AKONADICONFIGWIDGET_H
#define KALBURATOR_SYNC_AKONADICONFIGWIDGET_H

#ifdef HAVE_AKONADI

#include <QWidget>

class QLineEdit;

namespace Kalburator::Sync {

class AkonadiConfigWidget : public QWidget {
    Q_OBJECT
public:
    explicit AkonadiConfigWidget(QWidget *parent = nullptr);
    QString displayName() const;
    void setDisplayName(const QString &name);
private:
    QLineEdit *m_name;
};

} // namespace Kalburator::Sync

#endif // HAVE_AKONADI
#endif // KALBURATOR_SYNC_AKONADICONFIGWIDGET_H
