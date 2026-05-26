#include "orgcanonstages.h"
#include "outlinenode.h"
#include "canonenvelope.h"

#include <orggrove/parser.h>
#include <QJsonArray>
#include <QJsonObject>

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

namespace {

// OrgGrove returns {"TODO","|","DONE"} as the default keyword sequence for any file
// that has no explicit #+TODO: directive.  We suppress that default to keep canon clean.
bool isDefaultOrgVocabulary(const QStringList& kw)
{
    static const QStringList kDefault{ QStringLiteral("TODO"), QStringLiteral("|"), QStringLiteral("DONE") };
    return kw == kDefault;
}

// OrgGrove::Headline -> canon OutlineNode (recursive). Fields line up ~1:1.
OutlineNode fromOrg(const OrgGrove::Headline& h)
{
    OutlineNode n;
    n.text      = h.title;
    n.note      = h.body;
    n.done      = h.isDone;
    n.status    = h.todoKeyword;
    n.priority  = h.priority;          // both std::optional<int>; [#A]->1, [#B]->2, ...
    n.start     = h.planning.scheduled;
    n.due       = h.planning.deadline;
    n.completed = h.planning.closed;
    n.tags      = h.tags;
    for (auto it = h.properties.cbegin(); it != h.properties.cend(); ++it)
        n.attributes.insert(it.key(), it.value());
    for (const OrgGrove::Headline& c : h.children)
        n.children.append(fromOrg(c));
    return n;
}

// canon OutlineNode -> OrgGrove::Headline (recursive).
// `level` is the 1-based star count for this headline; OrgGrove::serialize
// uses h.level to emit the correct number of leading stars.
OrgGrove::Headline toOrg(const OutlineNode& n, int level)
{
    OrgGrove::Headline h;
    h.level              = level;
    h.title              = n.text;
    h.body               = n.note;
    h.isDone             = n.done;
    h.todoKeyword        = n.status;
    h.priority           = n.priority;
    h.planning.scheduled = n.start;
    h.planning.deadline  = n.due;
    h.planning.closed    = n.completed;
    h.tags               = n.tags;
    // Note: progress, created, and id have no org representation and are intentionally
    // dropped on serialize (they are canon-layer metadata, not Org-mode fields).
    const QJsonObject& attrs = n.attributes;
    for (auto it = attrs.begin(); it != attrs.end(); ++it)
        h.properties.insert(it.key(), it.value().toString());
    for (const OutlineNode& c : n.children)
        h.children.append(toOrg(c, level + 1));
    return h;
}

}  // namespace

QByteArray OrgToCanonStage::transform(const QByteArray& sourceBytes) const
{
    const OrgGrove::Document doc = OrgGrove::Parser().parse(sourceBytes);
    QJsonArray children;
    for (const OrgGrove::Headline& h : doc.children)
        children.append(fromOrg(h).toJson());

    QJsonObject body;
    CanonEnvelope::stampEnvelope(body, QStringLiteral("outline"), QString());
    if (!doc.title.isEmpty())
        body.insert(QStringLiteral("title"), doc.title);
    // Only emit statusVocabulary when the document declared a vocabulary that differs
    // from the org default.  OrgGrove returns {"TODO","|","DONE"} for any file that
    // has no explicit #+TODO: directive, so we suppress that case to keep canon clean.
    if (!doc.todoKeywords.isEmpty() && !isDefaultOrgVocabulary(doc.todoKeywords)) {
        // Doc-level status vocabulary rides in Tier-3 attributes.
        QJsonObject attrs;
        attrs.insert(QStringLiteral("statusVocabulary"),
                     doc.todoKeywords.join(QLatin1Char(' ')));
        body.insert(QStringLiteral("attributes"), attrs);
    }
    body.insert(QStringLiteral("children"), children);
    return CanonEnvelope::serialize(body);
}

QByteArray CanonToOrgStage::transform(const QByteArray& sourceBytes) const
{
    const QJsonObject body = CanonEnvelope::parse(sourceBytes);
    OrgGrove::Document doc;
    doc.title = body.value(QStringLiteral("title")).toString();
    const QString vocab = body.value(QStringLiteral("attributes")).toObject()
                              .value(QStringLiteral("statusVocabulary")).toString();
    if (!vocab.isEmpty())
        doc.todoKeywords = vocab.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QJsonValue& v : body.value(QStringLiteral("children")).toArray())
        doc.children.append(toOrg(OutlineNode::fromJson(v.toObject()), 1));
    return OrgGrove::serialize(doc);
}

}  // namespace Kalburator::Outline
