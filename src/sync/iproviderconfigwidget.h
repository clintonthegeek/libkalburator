#ifndef KALBURATOR_SYNC_IPROVIDERCONFIGWIDGET_H
#define KALBURATOR_SYNC_IPROVIDERCONFIGWIDGET_H

#include "backendconfiguration.h"

namespace Kalburator::Sync {

/**
 * @brief Uniform contract for provider config widgets.
 *
 * Every widget returned by IProvider::createConfigWidget() should also
 * implement this interface so a generic consumer (an Add-Account dialog or
 * wizard) can move the user's edited values between the widget and the
 * provider without knowing the concrete widget type:
 *
 *   - setConfiguration(provider->save())   // provider → widget, on open
 *   - provider->load(widget->configuration())  // widget → provider, before
 *                                               // connect()/save()
 *
 * Without this bridge the provider never sees the user's input and connect()
 * fails its "no server URL configured" guard immediately (the bug that made
 * "Test connection" always report "Failed"). Config widgets multiply-inherit
 * QWidget and this interface; consumers reach it via
 * dynamic_cast<IProviderConfigWidget*>(someQWidget).
 */
class IProviderConfigWidget
{
public:
    virtual ~IProviderConfigWidget() = default;

    /// The configuration currently shown in the widget's fields.
    virtual BackendConfiguration configuration() const = 0;

    /// Populate the widget's fields from @p cfg.
    virtual void setConfiguration(const BackendConfiguration &cfg) = 0;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_IPROVIDERCONFIGWIDGET_H
