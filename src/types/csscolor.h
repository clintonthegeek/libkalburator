#pragma once

#include <QColor>
#include <QString>

namespace Kalburator {

/**
 * @brief Parse a CSS/CalDAV hex colour, honouring `#RRGGBBAA`.
 *
 * Apple's `calendar-color` CalDAV property (and CSS Color 4) put alpha LAST:
 * `#RRGGBBAA`. Qt reads a nine-character `#`-string as `#AARRGGBB` instead, so
 * handing one straight to QColor rotates every channel one position — a colour
 * written as `#1e88e5FF` comes back as alpha 0x1e, red 0x88, green 0xe5, blue
 * 0xFF. That made a written-then-read colour differ from itself, which the
 * sync engine reported as an unresolvable collection-property conflict on
 * every run. See
 * docs/bugs/caldav-calendar-color-roundtrip-rotates-channels.md.
 *
 * Anything Qt already handles unambiguously (`#RGB`, `#RRGGBB`, named colours,
 * the wider `#RRRGGGBBB` forms) is passed through untouched.
 */
inline QColor colorFromCssHex(const QString &text)
{
    const QString trimmed = text.trimmed();
    // Only the 8-hex-digit form is ambiguous between the two conventions.
    if (trimmed.size() == 9 && trimmed.startsWith(QLatin1Char('#'))) {
        const QString rgb = trimmed.left(7);
        bool alphaOk = false;
        const int alpha = QStringView{trimmed}.mid(7, 2).toInt(&alphaOk, 16);
        QColor color(rgb);
        if (color.isValid() && alphaOk) {
            color.setAlpha(alpha);
            return color;
        }
    }
    return QColor(trimmed);
}

/**
 * @brief Serialize a colour as CSS/CalDAV `#RRGGBBAA`, the inverse of
 *        colorFromCssHex().
 *
 * `QColor::name(QColor::HexArgb)` emits Qt's `#AARRGGBB` instead. Pairing that
 * writer with colorFromCssHex()'s reader rotates every channel one position on
 * each round trip — which is exactly what happened when the CalDAV fix
 * (26ac34c) put colorFromCssHex() into `PerCalendarCapabilities::fromJson()`
 * and left `toJson()` on `HexArgb`. Opaque red went out as `#ffff0000` and came
 * back transparent yellow. `tst_backendconfiguration_json` caught it; that
 * target had not compiled since RRD-001, so nothing reported it.
 *
 * Use this wherever colorFromCssHex() reads, so the two conventions cannot
 * drift apart again. A fully opaque colour still emits its alpha explicitly:
 * the reader treats a 9-character string as CSS, so a 7-character one would
 * round-trip fine too, but emitting the same width every time keeps stored
 * values comparable byte for byte.
 */
inline QString cssHexFromColor(const QColor &color)
{
    if (!color.isValid())
        return QString();
    return QStringLiteral("#%1%2")
        .arg(color.name(QColor::HexRgb).mid(1))
        .arg(color.alpha(), 2, 16, QLatin1Char('0'));
}

} // namespace Kalburator
