#ifndef KALBURATOR_UI_PROVIDERCONFIGDIALOG_H
#define KALBURATOR_UI_PROVIDERCONFIGDIALOG_H

#include "backendconfiguration.h"
#include "collectioninfo.h"

#include <QDialog>
#include <QList>
#include <memory>

class QComboBox;
class QWidget;
class QPushButton;

namespace Kalburator {
namespace Sync { class ProviderManager; class IProvider; class BackendRegistry; }

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

    /// O.1.4: registry-aware constructor. Iterates registry->contributions()
    /// to populate the kind combo and subscribes to contributionRegistered/
    /// Unregistered (O.1.1) so the kind list stays live with plugin loads.
    /// Use this instead of the hardcoded-kinds constructor in new code.
    ProviderConfigDialog(Sync::ProviderManager *manager,
                         Sync::BackendRegistry *registry,
                         Mode mode,
                         const Sync::BackendConfiguration &existing = {},
                         QWidget *parent = nullptr);

    ~ProviderConfigDialog() override;

    Sync::BackendConfiguration result() const;
    QStringList selectedCollectionIds() const;

    /// M.5: moves the dialog's currently-configured provider out to the
    /// caller. After this call the dialog is inert — do not invoke
    /// onTestClicked(), result(), or selectedCollectionIds(). Returns
    /// null if no provider was constructed (e.g. user cancelled before
    /// selecting a kind, or the kind had no registered contribution).
    std::unique_ptr<Sync::IProvider> takeProvider();

private slots:
    void onProviderChanged(int comboIndex);
    void onTestClicked();
    void onConnectFinished(bool ok);

private:
    void rebuildProviderWidget();
    void populateKindsFromRegistry();

    Sync::ProviderManager  *m_manager;
    Sync::BackendRegistry  *m_registry = nullptr;   // borrowed, non-owning
    QList<ProviderKind>     m_availableKinds;
    Mode                    m_mode;
    Sync::BackendConfiguration m_existing;

    QComboBox              *m_combo           = nullptr;
    QWidget                *m_embeddedConfig  = nullptr;
    std::unique_ptr<Sync::IProvider> m_currentProvider;
    CollectionPickerWidget *m_picker          = nullptr;
    QPushButton            *m_testButton      = nullptr;
    QPushButton            *m_saveButton      = nullptr;
};

} // namespace Ui
} // namespace Kalburator

#endif
