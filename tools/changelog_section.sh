#!/usr/bin/env sh
# Prints the CHANGELOG.md section for one version, without its heading.
# Exits with status 1 if the version has no (or an empty) section.
# Used by the release workflow for the release notes.
#
# Usage: sh tools/changelog_section.sh 0.1.0 [CHANGELOG.md]
version="$1"
file="${2:-CHANGELOG.md}"
[ -n "$version" ] || { echo "usage: $0 <version> [CHANGELOG.md]" >&2; exit 2; }

awk -v v="$version" '
  /^## / {
    if (found) exit
    h = $0
    gsub(/[][]/, "", h)          # "## [0.1.0] - date" -> "## 0.1.0 - date"
    split(h, part, " ")
    if (part[2] == v) { found = 1; next }
  }
  found {
    if ($0 ~ /^[[:space:]]*$/) { blank++; next }   # hold blank lines ...
    if (started) for (i = 0; i < blank; i++) print ""  # ... drop leading/trailing ones
    blank = 0; started = 1; print
  }
  END { if (!started) exit 1 }
' "$file"
