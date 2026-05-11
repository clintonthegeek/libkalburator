#include "memoplugin.h"
#include "memodomaindefinition.h"

namespace Kalburator::Memo {

QList<std::shared_ptr<Shape::DomainDefinition>> MemoPlugin::domainDefinitions() const {
    return { std::make_shared<MemoDomainDefinition>() };
}

} // namespace Kalburator::Memo
