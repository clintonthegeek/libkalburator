#include "opmlcanonstages.h"
#include "outlinenode.h"
#include "canonenvelope.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

using namespace Kalburator::Shape;

namespace Kalburator::Outline {

namespace {

// Build an OutlineNode from the <outline> element the reader is positioned on.
OutlineNode readOutline(QXmlStreamReader& xml)
{
    OutlineNode n;
    const QXmlStreamAttributes attrs = xml.attributes();
    for (const QXmlStreamAttribute& a : attrs) {
        const QString name = a.name().toString();
        const QString val  = a.value().toString();
        if (name == QLatin1String("text"))
            n.text = val;
        else if (name == QLatin1String("created"))
            n.created = val;
        else if (name == QLatin1String("category"))
            n.tags = val.split(QLatin1Char(','), Qt::SkipEmptyParts);
        else if (name == QLatin1String("_note"))
            n.note = val;
        else
            n.attributes.insert(name, val);
    }
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement
                && xml.name() == QStringLiteral("outline"))
            n.children.append(readOutline(xml));
        else if (tok == QXmlStreamReader::EndElement
                && xml.name() == QStringLiteral("outline"))
            break;
    }
    return n;
}

void writeOutline(QXmlStreamWriter& xml, const OutlineNode& n)
{
    xml.writeStartElement(QStringLiteral("outline"));
    xml.writeAttribute(QStringLiteral("text"), n.text);
    if (!n.created.isEmpty()) xml.writeAttribute(QStringLiteral("created"), n.created);
    if (!n.tags.isEmpty())    xml.writeAttribute(QStringLiteral("category"), n.tags.join(QLatin1Char(',')));
    if (!n.note.isEmpty())    xml.writeAttribute(QStringLiteral("_note"), n.note);   // Reversible
    static const QSet<QString> kReservedAttrs {
        QStringLiteral("text"), QStringLiteral("created"),
        QStringLiteral("category"), QStringLiteral("_note")
    };
    for (auto it = n.attributes.begin(); it != n.attributes.end(); ++it) {
        if (!kReservedAttrs.contains(it.key()))
            xml.writeAttribute(it.key(), it.value().toString());
    }
    // Task fields intentionally dropped (no OPML representation).
    for (const OutlineNode& c : n.children) writeOutline(xml, c);
    xml.writeEndElement();
}

}  // namespace

QByteArray OpmlToCanonStage::transform(const QByteArray& sourceBytes) const
{
    QXmlStreamReader xml(sourceBytes);
    QString title;
    QJsonArray children;
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok != QXmlStreamReader::StartElement) continue;
        if (xml.name() == QStringLiteral("title"))
            title = xml.readElementText();
        else if (xml.name() == QStringLiteral("outline"))
            children.append(readOutline(xml).toJson());
    }
    // Stage contract: best-effort, empty tree on parse error.
    QJsonObject body;
    CanonEnvelope::stampEnvelope(body, QStringLiteral("outline"), QString());
    if (!title.isEmpty()) body.insert(QStringLiteral("title"), title);
    if (xml.hasError())
        body.insert(QStringLiteral("children"), QJsonArray{});
    else
        body.insert(QStringLiteral("children"), children);
    return CanonEnvelope::serialize(body);
}

QByteArray CanonToOpmlStage::transform(const QByteArray& sourceBytes) const
{
    const QJsonObject body = CanonEnvelope::parse(sourceBytes);
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("opml"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("2.0"));
    xml.writeStartElement(QStringLiteral("head"));
    if (body.contains(QStringLiteral("title")))
        xml.writeTextElement(QStringLiteral("title"), body.value(QStringLiteral("title")).toString());
    xml.writeEndElement();  // head
    xml.writeStartElement(QStringLiteral("body"));
    for (const QJsonValue& v : body.value(QStringLiteral("children")).toArray())
        writeOutline(xml, OutlineNode::fromJson(v.toObject()));
    xml.writeEndElement();  // body
    xml.writeEndElement();  // opml
    xml.writeEndDocument();
    return out;
}

}  // namespace Kalburator::Outline
