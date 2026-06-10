#ifndef KALBURATOR_UI_PROVIDERCONFIGDIALOG_H
#define KALBURATOR_UI_PROVIDERCONFIGDIALOG_H

#include "backendconfiguration.h"
#include "collectioninfo.h"

#include <QDialog>
#include <QList>
#include <memory>

class QComboBox;
class QFrame;
class QHBoxLayout;
class QPlainTextEdit;
class QToolButton;
class QWidget;
class QPushButton;
class QLabel;

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

    /// A named profile a caller can offer for one-click autofill. The dialog
    /// is app-agnostic: it renders whatever profiles it is given and does not
    /// know where they came from. If never set (or set empty), no autofill UI
    /// is shown and the dialog is unchanged.
    struct AutofillProfile {
        QString label;                       // menu item text
        Sync::BackendConfiguration config;   // type + connectionParams to apply
    };

    /// Provide autofill profiles. Non-empty -> an "Autofill" menu button appears
    /// in the button row. Empty/never-called -> no UI added.
    void setAutofillProfiles(const QList<AutofillProfile> &profiles);

    /// O.1.4: registry-aware constructor. Iterates registry->contributions()
    /// to populate the kind combo and subscribes to contributionRegistered/
    /// Unregistered (O.1.1) so the kind list stays live with plugin loads.
    /// O.4.10: this is the sole constructor; the hardcoded-kinds variant
    /// was deleted.
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

    /// Copy the embedded config widget's current values into the provider
    /// (widget → provider) so connect()/save() act on the user's input.
    /// No-op if the widget doesn't implement IProviderConfigWidget.
    void applyWidgetToProvider() const;

    /// Apply one profile: select its type in the combo (rebuilds the embedded
    /// widget) and push its config into that widget. No-op if the type is not
    /// a registered kind.
    void applyAutofillProfile(const AutofillProfile &profile);

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
    QLabel                 *m_statusLabel     = nullptr;
    QHBoxLayout            *m_buttonRow       = nullptr;   // row holding Test/Save
    QPushButton            *m_autofillButton  = nullptr;   // null unless profiles set
    QList<AutofillProfile>  m_autofillProfiles;

    // Captures the provider's error() message for the in-flight Test
    // connection, so onConnectFinished can display the reason on failure.
    QString                 m_lastTestError;
    QMetaObject::Connection m_errorConn;

    // §4.3 — 0-calendars help panel (built lazily, inserted above m_picker)
    QFrame                 *m_noCalendarsPanel = nullptr;

    // §4.4 — expandable error-details disclosure (built in ctor, below status label)
    QToolButton            *m_detailsBtn  = nullptr;
    QPlainTextEdit         *m_detailsText = nullptr;
};

} // namespace Ui
} // namespace Kalburator

#endif
