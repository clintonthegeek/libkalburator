#include "outlinenode.h"

#include <QJsonArray>

namespace Kalburator::Outline {

QJsonObject OutlineNode::toJson() const
{
    QJsonObject o;
    if (!id.isEmpty())        o.insert("id", id);
    o.insert("text", text);                          // always present
    if (!note.isEmpty())      o.insert("note", note);
    if (done)                 o.insert("done", true);
    if (!status.isEmpty())    o.insert("status", status);
    if (priority)             o.insert("priority", *priority);
    if (progress)             o.insert("progress", *progress);
    if (!start.isEmpty())     o.insert("start", start);
    if (!due.isEmpty())       o.insert("due", due);
    if (!completed.isEmpty()) o.insert("completed", completed);
    if (!created.isEmpty())   o.insert("created", created);
    if (!tags.isEmpty()) {
        QJsonArray t;
        for (const QString& s : tags) t.append(s);
        o.insert("tags", t);
    }
    if (!attributes.isEmpty()) o.insert("attributes", attributes);
    if (!children.isEmpty()) {
        QJsonArray c;
        for (const OutlineNode& ch : children) c.append(ch.toJson());
        o.insert("children", c);
    }
    return o;
}

OutlineNode OutlineNode::fromJson(const QJsonObject& obj)
{
    OutlineNode n;
    n.id        = obj.value("id").toString();
    n.text      = obj.value("text").toString();
    n.note      = obj.value("note").toString();
    n.done      = obj.value("done").toBool(false);
    n.status    = obj.value("status").toString();
    if (obj.contains("priority")) n.priority = obj.value("priority").toInt();
    if (obj.contains("progress")) n.progress = obj.value("progress").toInt();
    n.start     = obj.value("start").toString();
    n.due       = obj.value("due").toString();
    n.completed = obj.value("completed").toString();
    n.created   = obj.value("created").toString();
    for (const QJsonValue& v : obj.value("tags").toArray())
        n.tags.append(v.toString());
    n.attributes = obj.value("attributes").toObject();
    for (const QJsonValue& v : obj.value("children").toArray())
        n.children.append(OutlineNode::fromJson(v.toObject()));
    return n;
}

}  // namespace Kalburator::Outline
