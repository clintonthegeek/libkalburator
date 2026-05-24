#include "lossprofiledetailview.h"

#include <QLabel>
#include <QVBoxLayout>

using Kalburator::Shape::LossProfile;
using Kalburator::Shape::LossKind;

namespace Kalburator::Widgets {

static QString levelText(const LossProfile &profile)
{
    if (profile.isLossless())
        return QObject::tr("Lossless — full round-trip preserved");

    bool anyDropped = false;
    bool anySimplified = false;
    bool anyDegraded = false;
    for (const auto kind : std::as_const(profile.affected)) {
        switch (kind) {
            case LossKind::Dropped:    anyDropped = true;    break;
            case LossKind::Simplified: anySimplified = true; break;
            case LossKind::Degraded:   anyDegraded = true;   break;
            case LossKind::Reversible: break;
        }
    }

    if (anyDegraded)
        return QObject::tr("Degenerate — only name-like fields preserved");
    if (anyDropped)
        return QObject::tr("Lossy — some fields dropped (same domain)");
    if (anySimplified)
        return QObject::tr("Simplified — some fields reduced");
    return QObject::tr("Reversible — lossless with encoding change");
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
    m_levelLabel->setText(levelText(m_profile));

    const auto dropped = m_profile.droppedProperties();
    if (dropped.isEmpty()) {
        m_droppedLabel->hide();
    } else {
        QStringList ids;
        for (const auto &pid : dropped)
            ids.append(pid.toString());
        ids.sort();
        m_droppedLabel->setText(tr("Dropped: %1").arg(ids.join(QStringLiteral(", "))));
        m_droppedLabel->show();
    }
}

} // namespace Kalburator::Widgets
