#include "lossprofiledetailview.h"

#include <QLabel>
#include <QVBoxLayout>

using Kalburator::Shape::LossLevel;
using Kalburator::Shape::LossProfile;

namespace Kalburator::Widgets {

static QString levelText(LossLevel level)
{
    switch (level) {
        case LossLevel::Lossless:
            return QObject::tr("Lossless — full round-trip preserved");
        case LossLevel::IntraDomainLossy:
            return QObject::tr("Lossy — some fields dropped (same domain)");
        case LossLevel::InterDomainProjection:
            return QObject::tr("Projection — structural reduction to target domain");
        case LossLevel::Degenerate:
            return QObject::tr("Degenerate — only name-like fields preserved");
    }
    return {};
}

LossProfileDetailView::LossProfileDetailView(QWidget *parent)
    : QWidget(parent)
    , m_levelLabel(new QLabel(this))
    , m_droppedLabel(new QLabel(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_levelLabel);
    layout->addWidget(m_droppedLabel);
    layout->addStretch();
    m_droppedLabel->setWordWrap(true);
    refresh();
}

void LossProfileDetailView::setLossProfile(const LossProfile &profile)
{
    m_profile = profile;
    refresh();
}

LossProfile LossProfileDetailView::lossProfile() const
{
    return m_profile;
}

void LossProfileDetailView::refresh()
{
    m_levelLabel->setText(levelText(m_profile.level));

    if (m_profile.dropped.isEmpty()) {
        m_droppedLabel->hide();
    } else {
        QStringList ids;
        for (const auto &pid : std::as_const(m_profile.dropped))
            ids.append(pid.toString());
        ids.sort();
        m_droppedLabel->setText(tr("Dropped: %1").arg(ids.join(QStringLiteral(", "))));
        m_droppedLabel->show();
    }
}

} // namespace Kalburator::Widgets
