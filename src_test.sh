#!/bin/sh
meson compile ccheck -C build || exit
sleep 1
echo '=================================================='
./build/ccheck $@
