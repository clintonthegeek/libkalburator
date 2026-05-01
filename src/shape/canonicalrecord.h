#pragma once

#include <QByteArray>
#include <QString>

#include "shape.h"

namespace Kalburator::Shape {

/// A BackendRecord plus the shape its bytes are in. Used wherever
/// the engine handles records mid-pipeline (during diff and merge,
/// after promote-to-canonical, before push-back-to-target).
struct CanonicalRecord {
    Shape shape;
    QByteArray data;
    QString recordId;       // logical id; matches BackendRecord::id
    bool isDeleted = false;
};

}  // namespace Kalburator::Shape
