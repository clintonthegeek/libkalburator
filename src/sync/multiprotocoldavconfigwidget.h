#ifndef KALBURATOR_SYNC_MULTIPROTOCOLDAVCONFIGWIDGET_H
#define KALBURATOR_SYNC_MULTIPROTOCOLDAVCONFIGWIDGET_H

#include "backendconfiguration.h"
#include <QWidget>

class QLineEdit;
class QGroupBox;

namespace Kalburator::Sync {

class MultiProtocolDavConfigWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MultiProtocolDavConfigWidget(QWidget *parent = nullptr);

    void setConfiguration(const BackendConfiguration &cfg);
    BackendConfiguration configuration() const;

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
