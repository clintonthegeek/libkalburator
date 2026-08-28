#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace Kalburator::Shape {

/// Helpers for the canon JSON envelope (schema doc §1.2):
///   { "_canon": {"domain","v"}, "uid": "...", <props...>, "providerExtras": {...} }
/// Canon records carry these bytes in CanonicalRecord::data. These helpers are
/// the ONLY place that knows the envelope key names, so stages/differ/merger
/// agree on them.
namespace CanonEnvelope {

constexpr int kCanonVersion = 1;
QString canonKey();          // "_canon"
QString uidKey();            // "uid"
QString providerExtrasKey(); // "providerExtras"
QString kindKey();           // "kind"

/// Parse bytes into a JSON object (empty object if bytes are not a JSON object).
QJsonObject parse(const QByteArray& bytes);

/// Serialize compactly (stable: QJsonDocument sorts object keys).
QByteArray serialize(const QJsonObject& obj);

/// Stamp the envelope: sets _canon={domain,v=kCanonVersion[,kind]} and uid.
/// `kind` is written only when non-empty. Leaves all other keys untouched.
void stampEnvelope(QJsonObject& obj, const QString& domain, const QString& uid,
                   const QString& kind = QString());

/// Read uid (empty if absent).
QString uid(const QJsonObject& obj);

/// Read the component kind from _canon.kind (empty if absent ⇒ caller treats
/// as "vevent" for the calendar domain).
QString kind(const QJsonObject& obj);

/// Semantic equality of two JSON values: objects compare key-by-key
/// (order-independent), arrays element-wise in order, scalars by value.
/// (QJsonValue::operator== already provides exactly this.)
bool valuesEqual(const QJsonValue& a, const QJsonValue& b);

/// Domain-neutral canonicalizing fingerprint of a JSON value (O74): a
/// stable SHA256 hex digest, House convention (see e.g.
/// src/blob/localblobbackend.cpp's sha256Hex, src/calendar/
/// subscriptionbackend.cpp's contentHash) applied to a JSON tree instead of
/// flat bytes. QJsonDocument::toJson() already serializes QJsonObject keys
/// in sorted order at every nesting level regardless of insertion order
/// (verified against this Qt6 build), so no separate recursive key-sort
/// pass is needed — the value is simply wrapped in a QJsonDocument (via a
/// single-element array for a bare scalar) and hashed. Exists so a
/// catalogue-scoped differ (CanonJsonDiffer) can detect a change confined
/// to an uncatalogued sub-tree (e.g. providerExtras) by comparing a
/// catalogued digest property instead of the sub-tree itself. Callers on a
/// leg whose extras stash mixes real content with vendor bookkeeping that
/// churns on every write (etags, lastModified timestamps) MUST filter
/// those keys out before hashing, or the digest becomes spuriously
/// "always dirty" — see the todo domain's MS/Google call sites for the
/// worked examples.
QString canonicalDigest(const QJsonValue& value);

}  // namespace CanonEnvelope
}  // namespace Kalburator::Shape
