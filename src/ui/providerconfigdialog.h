#ifndef KALBURATOR_UI_PROVIDERCONFIGDIALOG_H
#define KALBURATOR_UI_PROVIDERCONFIGDIALOG_H

#include "backendconfiguration.h"
#include "collectioninfo.h"

#include <QDialog>
#include <QList>

class QComboBox;
class QWidget;
class QPushButton;

namespace Kalburator {
namespace Sync { class ProviderManager; class IProvider; }

namespace Ui {

class CollectionPickerWidget;

class ProviderConfigDialog : public QDialog
{
    Q_OBJECT
public:
    enum Mode { AddNew, EditExisting };

    struct ProviderKind { QString backendType; QString displayLabel; };

    ProviderConfigDialog(Sync::ProviderManager *manager,
                         const QList<ProviderKind> &availableKinds,
                         Mode mode,
                         const Sync::BackendConfiguration &existing = {},
                         QWidget *parent = nullptr);

    Sync::BackendConfiguration result() const;
    QStringList selectedCollectionIds() const;

private slots:
    void onProviderChanged(int comboIndex);
    void onTestClicked();
    void onConnectFinished(bool ok);

private:
    void rebuildProviderWidget();

    Sync::ProviderManager  *m_manager;
    QList<ProviderKind>     m_availableKinds;
    Mode                    m_mode;
    Sync::BackendConfiguration m_existing;

    QComboBox              *m_combo           = nullptr;
    QWidget                *m_embeddedConfig  = nullptr;
    Sync::IProvider        *m_currentProvider = nullptr;
    CollectionPickerWidget *m_picker          = nullptr;
    QPushButton            *m_testButton      = nullptr;
    QPushButton            *m_saveButton      = nullptr;
};

} // namespace Ui
} // namespace Kalburator

#endif
