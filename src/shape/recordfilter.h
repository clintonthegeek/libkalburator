#pragma once

#include <QVariant>

class QJsonDocument;

#include "propertycatalogue.h"  // PropertyId

namespace Kalburator::Shape {

/// Predicate over a canon-JSON record. One filter = one property + one op.
///
/// The set of ops is deliberately tiny (Contains/Equals). The intent is that
/// new ops are added one at a time with a concrete use case rather than
/// shipping a query language — see the v0.58 RFC.
///
/// `Contains`: the property at `property` is expected to be a JSON array;
///             `matches()` returns true iff the array contains a value
///             semantically equal to `value`. String comparison is
///             case-sensitive (canonical for category routes).
/// `Equals`:   `matches()` returns true iff the property's JSON value is
///             semantically equal to `value`.
///
/// Missing property, type mismatch, or unparseable bytes => false. No
/// exceptions thrown.
struct RecordFilter {
    enum class Op { Contains, Equals };

    PropertyId property;
    Op         op = Op::Contains;
    QVariant   value;

    /// Evaluate against a parsed canon-JSON document. The document's root
    /// must be a JSON object (the canon envelope); anything else => false.
    bool matches(const QJsonDocument& canonRecord) const;

    /// Convenience: parse the bytes (must be a JSON object) and evaluate.
    /// Failure to parse => false.
    bool matches(const QByteArray& canonRecordBytes) const;

    bool operator==(const RecordFilter&) const = default;
};

}  // namespace Kalburator::Shape
