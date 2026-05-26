#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace Kalburator::Outline {

/// One node in an outline tree. Mirrors the canon JSON node shape
/// (see docs/2026-05-25-outline-domain-design.md §2.3). Absent optional
/// fields are omitted from toJson() rather than emitted as null.
struct OutlineNode {
    QString id;            // reserved; no peer stage currently populates/serializes this (see loss profiles)
    QString text;
    QString note;                 // empty == absent
    bool done = false;
    QString status;               // empty == absent
    std::optional<int> priority;
    std::optional<int> progress;
    QString start, due, completed, created;   // ISO strings; empty == absent
    QStringList tags;
    QJsonObject attributes;       // node-level Tier-3 bag
    QList<OutlineNode> children;

    QJsonObject toJson() const;
    static OutlineNode fromJson(const QJsonObject& obj);
};

}  // namespace Kalburator::Outline
