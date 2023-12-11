#!/bin/sh
meson compile -C build || exit
dir=src_test
[[ $1 -eq "new" ]] && touch $dir/add.c $dir/add.h $dir/greetings.h
sleep 1
echo '=================================================='
./build/ccheck $PWD/$dir
