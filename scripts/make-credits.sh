#!/bin/bash
# Credits.rtf for the About box, from the README's Credits and Licence
# sections: each "- " item or plain paragraph becomes a paragraph, `code`
# and **bold** marks dropped.
# Usage: scripts/make-credits.sh README.md > Credits.rtf
set -euo pipefail
printf '{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Helvetica;}}\\f0\\fs20\\qc\n'
awk '
function rtf(s) { gsub(/\\/, "\\\\", s); gsub(/\{/, "\\{", s); gsub(/\}/, "\\}", s); gsub(/`/, "", s); gsub(/\*\*/, "", s); return s }
function flush() { if (para != "") printf "%s\\par\n", rtf(para); para = "" }
/^## / { flush(); section = substr($0, 4); show = (section == "Credits" || section == "Licence"); if (show) { if (started) printf "\\par\n"; printf "{\\b %s}\\par\n", rtf(section); started = 1 } next }
!show { next }
/^- / { flush(); para = substr($0, 3); next }
/^[ \t]*$/ { flush(); next }
{ para = (para == "") ? $0 : para " " $0 }
END { flush() }
' "$1"
printf '}\n'
