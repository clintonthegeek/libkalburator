#ifndef KALBURATOR_SYNC_MULTIPROTOCOLDAVCONFIGWIDGET_H
#define KALBURATOR_SYNC_MULTIPROTOCOLDAVCONFIGWIDGET_H

#include "backendconfiguration.h"
#include "iproviderconfigwidget.h"
#include <QWidget>

class QLineEdit;
class QGroupBox;

namespace Kalburator::Sync {

class MultiProtocolDavConfigWidget : public QWidget,
                                     public IProviderConfigWidget
{
    Q_OBJECT
public:
    explicit MultiProtocolDavConfigWidget(QWidget *parent = nullptr);

    void setConfiguration(const BackendConfiguration &cfg) override;
    BackendConfiguration configuration() const override;

private:
    QLineEdit *m_displayNameEdit;
    QLineEdit *m_urlEdit;
    QLineEdit *m_usernameEdit;
    QLineEdit *m_passwordEdit;
    QGroupBox *m_advancedGroup;
    QLineEdit *m_manualCalDavEdit;
    QLineEdit *m_manualCardDavEdit;
};

} // namespace Kalburator::Sync

#endif
