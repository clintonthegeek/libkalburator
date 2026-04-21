#include "ilocalesource.h"

namespace Kalburator::Sync {

ILocaleSource* ILocaleSource::s_global = nullptr;

ILocaleSource* ILocaleSource::global()
{
    return s_global;
}

void ILocaleSource::setGlobal(ILocaleSource* source)
{
    s_global = source;
}


} // namespace Kalburator::Sync
