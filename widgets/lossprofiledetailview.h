#pragma once

#include <QWidget>
#include <QLabel>
#include "lossprofile.h"

namespace Kalburator::Widgets {

/**
 * @brief Read-only widget displaying a LossProfile in a sync-config UI.
 *
 * Shows the loss level as a label and lists the dropped properties, if any.
 * Consumers embed this in their sync-mapping configuration dialog to give
 * users immediate feedback on what data would be dropped for a given
 * source→target shape pair.
 *
 * Usage:
 * @code
 *   auto *view = new LossProfileDetailView(parent);
 *   view->setLossProfile(registry.inspect(srcShape, tgtShape));
 * @endcode
 */
class LossProfileDetailView : public QWidget
{
    Q_OBJECT
public:
    explicit LossProfileDetailView(QWidget *parent = nullptr);

    void setLossProfile(const Kalburator::Shape::LossProfile &profile);
    Kalburator::Shape::LossProfile lossProfile() const;

private:
    void refresh();

    Kalburator::Shape::LossProfile m_profile;

    QLabel *m_levelLabel   = nullptr;
    QLabel *m_droppedLabel = nullptr;
};

} // namespace Kalburator::Widgets
