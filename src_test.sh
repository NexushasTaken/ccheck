#!/bin/sh
meson compile -C build || exit
dir=src_test
touch $dir/add.c $dir/add.h $dir/greetings.h
echo '=================================================='
./build/ccheck $PWD/$dir
