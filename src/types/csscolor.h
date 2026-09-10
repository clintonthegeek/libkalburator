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

} // namespace Kalburator
