#include "markdowncanonstages.h"
#include "canonenvelope.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

using namespace Kalburator::Shape;

namespace Kalburator::Note {

namespace {

constexpr char kFrontmatter[] = "frontmatter";

// Split raw markdown text into (frontmatterInner, body).
// frontmatterInner is empty (and hasFm=false) when there is no leading fence.
struct Split { QString frontmatter; QString body; bool hasFm = false; };

Split splitMarkdown(const QString& text)
{
    Split s;
    if (!text.startsWith(QStringLiteral("---\n"))) {
        s.body = text;
        return s;
    }
    // Find the closing fence: a line "---" after the opener.
    const int close = text.indexOf(QStringLiteral("\n---"), 3);
    if (close < 0) {           // unterminated fence: treat whole thing as body
        s.body = text;
        return s;
    }
    s.hasFm = true;
    s.frontmatter = text.mid(4, close - 4);     // between "---\n" and "\n---"
    int after = close + 4;                      // past "\n---"
    if (after < text.size() && text.at(after) == QLatin1Char('\n'))
        ++after;                                // trailing newline of the close fence
    QString body = text.mid(after);
    while (body.startsWith(QLatin1Char('\n')))  // drop the blank separator line(s)
        body.remove(0, 1);
    s.body = body;
    return s;
}

QString uidFromFrontmatter(const QString& fm)
{
    const QStringList lines = fm.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0) continue;
        if (line.left(colon).trimmed().compare(QStringLiteral("id"), Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).trimmed();
    }
    return {};
}

QString normaliseBody(QString body)
{
    while (body.endsWith(QLatin1Char('\n')))
        body.chop(1);
    return body + QLatin1Char('\n');
}

} // namespace

QByteArray MarkdownToCanonStage::transform(const QByteArray& sourceBytes) const
{
    const Split s = splitMarkdown(QString::fromUtf8(sourceBytes));

    QJsonObject obj;
    CanonEnvelope::stampEnvelope(obj, QStringLiteral("note"),
                                 s.hasFm ? uidFromFrontmatter(s.frontmatter) : QString());
    obj.insert(QStringLiteral("body"), s.body);
    if (s.hasFm) {
        QJsonObject extras;
        extras.insert(QString::fromLatin1(kFrontmatter), s.frontmatter);
        obj.insert(CanonEnvelope::providerExtrasKey(), extras);
    }
    return CanonEnvelope::serialize(obj);
}

QByteArray CanonToMarkdownStage::transform(const QByteArray& sourceBytes) const
{
    const QJsonObject obj = CanonEnvelope::parse(sourceBytes);
    const QString body = obj.value(QStringLiteral("body")).toString();
    const QString fm = obj.value(CanonEnvelope::providerExtrasKey())
                          .toObject().value(QString::fromLatin1(kFrontmatter)).toString();

    QString out;
    if (!fm.isEmpty()) {
        out += QStringLiteral("---\n");
        out += fm;
        out += QStringLiteral("\n---\n\n");
    }
    out += normaliseBody(body);
    return out.toUtf8();
}

} // namespace Kalburator::Note
