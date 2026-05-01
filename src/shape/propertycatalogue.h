#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace Kalburator::Shape {

class PropertyId {
    QString m_id;
public:
    PropertyId() = default;
    explicit PropertyId(QString id) : m_id(std::move(id)) {}
    QString toString() const { return m_id; }
    bool operator==(const PropertyId&) const = default;
    bool operator!=(const PropertyId&) const = default;
};

size_t qHash(const PropertyId&, size_t seed = 0) noexcept;

enum class PropertyKind {
    String,
    Integer,
    Boolean,
    DateTime,
    Duration,
    Bytes,
    StringList,
    Json,            // for nested or composite values (attendees, etc.)
};

struct PropertyDescriptor {
    PropertyId id;
    PropertyKind kind = PropertyKind::String;
    QString displayName;            // for loss-profile UX
    bool optional = true;
};

class PropertyCatalogue {
public:
    void addProperty(PropertyDescriptor descriptor);
    const QList<PropertyDescriptor>& properties() const { return m_properties; }
    bool hasProperty(const PropertyId&) const;
    const PropertyDescriptor* find(const PropertyId&) const;

    /// Returns one DDL fragment per property, mapping PropertyKind to
    /// SQLite column types. Format: `<id> <type> [NOT NULL]`.
    /// String/StringList/Json → TEXT, Integer/Boolean → INTEGER,
    /// DateTime/Duration → TEXT (ISO 8601), Bytes → BLOB.
    QStringList sqlColumnDdl() const;

private:
    QList<PropertyDescriptor> m_properties;
    QHash<PropertyId, int> m_indexById;  // index into m_properties
};

}  // namespace Kalburator::Shape
