#!/usr/bin/env bash
# Build + run the incidence-parity audit probes against the CURRENT tree.
#
# Requires an existing build/ (cmake -S . -B build && cmake --build build).
# Links the static libs directly rather than adding CMake targets: these are
# evidence instruments, not part of the product, and must not accrete into
# the build graph. See README.md.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../../.." && pwd)"
outdir="${1:-$here/.out}"
mkdir -p "$outdir"

[[ -f "$root/build/libkalburator.a" ]] || {
  echo "error: $root/build/libkalburator.a missing — build the project first:" >&2
  echo "  cmake -S '$root' -B '$root/build' -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
  echo "  cmake --build '$root/build'" >&2
  exit 1
}

incs=()
for d in "$root"/src/*/; do incs+=("-I$d"); done

sysincs=(
  -isystem /usr/include/qt6/QtCore -isystem /usr/include/qt6
  -isystem /usr/lib/qt6/mkspecs/linux-g++ -isystem /usr/include/qt6/QtGui
  -isystem /usr/include/KF6/KCalendarCore -isystem /usr/include/KF6/KCoreAddons
)

qtlibs=(-lQt6Core -lQt6Gui -lQt6Network -lQt6Sql -lQt6Xml -lQt6Widgets
        -lQt6Concurrent -lQt6DBus -lKF6CalendarCore -lKF6CoreAddons -lKF6Contacts)

echo "==> building kcalendarcore-probe (upstream behaviour, no libkalburator)"
c++ -std=gnu++20 -fPIC -DQT_NO_DEBUG "${sysincs[@]}" \
    "$here/kcalendarcore-probe.cpp" -o "$outdir/kcalendarcore-probe" \
    -lQt6Core -lQt6Gui -lKF6CalendarCore

echo "==> building incidence-audit-probe (against build/libkalburator.a)"
c++ -std=gnu++20 -fPIC -DQT_NO_DEBUG -DKALBURATOR_HAVE_OUTLINE_ORG \
    "${incs[@]}" "${sysincs[@]}" \
    "$here/incidence-audit-probe.cpp" -o "$outdir/incidence-audit-probe" \
    "$root/build/libkalburator.a" "$root/build/libkalburator-types.a" \
    "$root/build/libkalburator-typesupport.a" "${qtlibs[@]}"

echo
echo "############ kcalendarcore-probe ############"
"$outdir/kcalendarcore-probe"
echo
echo "############ incidence-audit-probe ############"
"$outdir/incidence-audit-probe"
