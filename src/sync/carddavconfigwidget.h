#ifndef KALBURATOR_SYNC_CARDDAVCONFIGWIDGET_H
#define KALBURATOR_SYNC_CARDDAVCONFIGWIDGET_H

#include <kalburator/typesupport/backendconfiguration.h>
#include <kalburator/sync/iproviderconfigwidget.h>
#include <QWidget>

class QLineEdit;

namespace Kalburator::Sync {

/**
 * @brief Form widget for editing a CardDavProvider's account config.
 *
 * A dumb form: displayName / server URL / username / password, exposed via
 * IProviderConfigWidget. Testing, connecting and error reporting belong to the
 * consuming dialog (ProviderConfigDialog or WildPalms' AccountFormWidget),
 * which bridges widget->provider via provider->load(configuration()) before
 * connect()/save(). This is the lean pattern shared with
 * MultiProtocolDavConfigWidget and AkonadiConfigWidget.
 */
class CardDavConfigWidget : public QWidget,
                            public IProviderConfigWidget
{
    Q_OBJECT
public:
    explicit CardDavConfigWidget(QWidget *parent = nullptr);

    BackendConfiguration configuration() const override;
    void setConfiguration(const BackendConfiguration &cfg) override;

private:
    QLineEdit *m_displayNameEdit;
    QLineEdit *m_urlEdit;
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    mutable QString m_passwordRef;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_CARDDAVCONFIGWIDGET_H
