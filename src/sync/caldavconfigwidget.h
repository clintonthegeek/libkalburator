#ifndef KALBURATOR_SYNC_CALDAVCONFIGWIDGET_H
#define KALBURATOR_SYNC_CALDAVCONFIGWIDGET_H

#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;

namespace Kalburator::Sync {

class CalDavProvider;

/**
 * @brief Form widget for editing a CalDavProvider's account config.
 *
 * Hosts QLineEdits for displayName, server URL, username, password,
 * plus a "Test Connection" button that calls the provider's connect()
 * with the current form values and reports the outcome.
 *
 * The widget reads the provider's current state on construction and
 * holds unsaved edits in its own QLineEdits. Callers drive commit via
 * applyToProvider() — this widget has no Save button itself; the
 * hosting dialog typically owns the Apply/OK button.
 */
class CalDavConfigWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CalDavConfigWidget(CalDavProvider *provider, QWidget *parent = nullptr);
    ~CalDavConfigWidget() override;

    /// Write the form's current values back into the bound provider.
    /// Idempotent. Safe to call multiple times.
    void applyToProvider();

    // Test-only accessors. Public for unit testing; not part of the
    // intended user API surface.
    QLineEdit  *displayNameEditForTesting() const;
    QLineEdit  *urlEditForTesting() const;
    QLineEdit  *usernameEditForTesting() const;
    QLineEdit  *passwordEditForTesting() const;
    QPushButton *testButtonForTesting() const;
    QLabel     *statusLabelForTesting() const;

private slots:
    void onTestClicked();
    void onTestFinished(bool success);

private:
    void readFromProvider();

    CalDavProvider *m_provider;       // borrowed
    QLineEdit   *m_displayNameEdit;
    QLineEdit   *m_urlEdit;
    QLineEdit   *m_usernameEdit;
    QLineEdit   *m_passwordEdit;
    QPushButton *m_testButton;
    QLabel      *m_statusLabel;
};

} // namespace Kalburator::Sync

#endif
