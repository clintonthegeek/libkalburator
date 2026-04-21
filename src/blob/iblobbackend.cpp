#include "iblobbackend.h"

namespace Kalburator::Sync {

IBlobBackend::IBlobBackend(QObject *parent)
    : QObject(parent)
{
}

IBlobBackend::~IBlobBackend() = default;

} // namespace Kalburator::Sync
