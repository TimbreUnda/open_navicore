#!/bin/sh
# Read-only dump of QNX Screen's debug filesystem: every window Screen knows
# about (any process) with its text property files. Skips framebuffer images.
# Radio shell has no ls/head: uses globbing and cat only.
ROOT=${1:-/dev/screen}
for dev in $ROOT/*; do
    [ -d "$dev" ] || continue
    echo "== $dev"
    for node in $dev/*; do
        [ -e "$node" ] || continue
        case "$node" in
        */win-*|*/dpy-*)
            echo "-- $node"
            for f in $node/*; do
                [ -f "$f" ] || continue
                case "$f" in *.bmp|*buffer*) echo "   $f (binary, skipped)"; continue;; esac
                printf '   %s: ' "${f##*/}"
                /bin/cat "$f" 2>/dev/null
                echo
            done
            ;;
        *)
            [ -f "$node" ] && { printf '%s: ' "${node##*/}"; /bin/cat "$node" 2>/dev/null; echo; }
            ;;
        esac
    done
done
