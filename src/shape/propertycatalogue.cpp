#include "propertycatalogue.h"

namespace Kalburator::Shape {

size_t qHash(const PropertyId& p, size_t seed) noexcept {
    return ::qHash(p.toString(), seed);
}

void PropertyCatalogue::addProperty(PropertyDescriptor descriptor) {
    if (m_indexById.contains(descriptor.id)) {
        // Idempotent on identical re-registration; replace if differs.
        const int idx = m_indexById.value(descriptor.id);
        m_properties[idx] = std::move(descriptor);
        return;
    }
    m_indexById.insert(descriptor.id, m_properties.size());
    m_properties.append(std::move(descriptor));
}

bool PropertyCatalogue::hasProperty(const PropertyId& id) const {
    return m_indexById.contains(id);
}

const PropertyDescriptor* PropertyCatalogue::find(const PropertyId& id) const {
    auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) return nullptr;
    return &m_properties.at(*it);
}

namespace {
QString sqliteTypeFor(PropertyKind kind) {
    switch (kind) {
    case PropertyKind::String:
    case PropertyKind::StringList:
    case PropertyKind::Json:
    case PropertyKind::DateTime:
    case PropertyKind::Duration:
        return QStringLiteral("TEXT");
    case PropertyKind::Integer:
    case PropertyKind::Boolean:
        return QStringLiteral("INTEGER");
    case PropertyKind::Bytes:
        return QStringLiteral("BLOB");
    }
    return QStringLiteral("TEXT");
}
}  // namespace

QStringList PropertyCatalogue::sqlColumnDdl() const {
    QStringList ddl;
    ddl.reserve(m_properties.size());
    for (const auto& p : m_properties) {
        QString line = p.id.toString() + QLatin1Char(' ') + sqliteTypeFor(p.kind);
        if (!p.optional) {
            line += QStringLiteral(" NOT NULL");
        }
        ddl.append(line);
    }
    return ddl;
}

}  // namespace Kalburator::Shape
