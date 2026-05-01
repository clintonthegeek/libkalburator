#include "shape.h"

namespace Kalburator::Shape {

namespace {
constexpr const char* kAnySentinel = "__any__";
}

Shape Shape::Any() {
    return Shape{ DomainId{QStringLiteral("__any__")},
                  EncodingId{QStringLiteral("__any__")} };
}

bool Shape::isAny() const noexcept {
    return domain.toString() == QLatin1String(kAnySentinel) &&
           encoding.toString() == QLatin1String(kAnySentinel);
}

QString Shape::toString() const {
    if (isAny()) return QStringLiteral("any");
    return domain.toString() + QLatin1Char('+') + encoding.toString();
}

size_t qHash(const DomainId& d, size_t seed) noexcept {
    return ::qHash(d.toString(), seed);
}

size_t qHash(const EncodingId& e, size_t seed) noexcept {
    return ::qHash(e.toString(), seed);
}

size_t qHash(const Shape& s, size_t seed) noexcept {
    return qHash(s.domain, seed) ^ qHash(s.encoding, seed << 1);
}

}  // namespace Kalburator::Shape
