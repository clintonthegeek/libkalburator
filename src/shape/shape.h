#pragma once

#include <QHash>
#include <QString>

namespace Kalburator::Shape {

class DomainId {
    QString m_id;
public:
    DomainId() = default;
    explicit DomainId(QString id) : m_id(std::move(id)) {}
    QString toString() const { return m_id; }
    bool operator==(const DomainId&) const = default;
    bool operator!=(const DomainId&) const = default;
};

class EncodingId {
    QString m_id;
public:
    EncodingId() = default;
    explicit EncodingId(QString id) : m_id(std::move(id)) {}
    QString toString() const { return m_id; }
    bool operator==(const EncodingId&) const = default;
    bool operator!=(const EncodingId&) const = default;
};

struct Shape {
    DomainId domain;
    EncodingId encoding;

    /// Sentinel value identifying universal sinks. `Shape::Any().isAny()`
    /// returns true; otherwise `isAny()` is false. The engine treats Any
    /// sinks with identity-passthrough pipelines regardless of source shape.
    static Shape Any();
    bool isAny() const noexcept;

    bool operator==(const Shape&) const = default;
    bool operator!=(const Shape&) const = default;

    /// "<domain>+<encoding>" or "any" if isAny().
    QString toString() const;
};

size_t qHash(const DomainId&, size_t seed = 0) noexcept;
size_t qHash(const EncodingId&, size_t seed = 0) noexcept;
size_t qHash(const Shape&, size_t seed = 0) noexcept;

}  // namespace Kalburator::Shape
