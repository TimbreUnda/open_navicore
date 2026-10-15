#!/bin/sh
# /open-navicore/boot.sh: the navigation image's own boot step. The boot
# overlay mounts navigation.ifs at / and then runs this script, appending its
# output to the overlay log; it must not wait or loop (the overlay is a
# synchronous boot barrier).
#
# It makes System Manager start nv_navigation with Open NaviCore: the
# process-manager link points /pas/scfg/nv_navigation.cfg at the image's
# config (NaviDllName=/open-navicore/libNaviCore.so). There is no fallback to
# the native NaviCore; rollback is the overlay's disable marker
# (/datastore/navicore/disabled) or removing the image.
ROOT=/open-navicore
if [ -r $ROOT/.open_navicore_navigation_v1 ] && [ -r $ROOT/nv_navigation.cfg ] &&
   [ -r $ROOT/libNaviCore.so ]; then
    /proc/boot/ln -sPf $ROOT/nv_navigation.cfg /pas/scfg/nv_navigation.cfg 2>&1
    print "open_navicore_config_link_rc=$?"
else
    print "open_navicore_image_incomplete"
fi
