#pragma once

#include "plugin.h"

namespace Kalburator::Memo {

class MemoPlugin : public Plugin {
public:
    QList<std::shared_ptr<Shape::DomainDefinition>> domainDefinitions() const override;
};

} // namespace Kalburator::Memo
