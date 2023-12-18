#!/bin/sh
meson compile ccheck -C build || exit
dir=src_test
[[ $1 = "new" ]] && touch $dir/add.c $dir/add.h $dir/greetings.h
sleep 1
echo '=================================================='
./build/ccheck ./$dir
if [[ $1 = "new" ]]; then
  echo '=================================================='
  ./build/ccheck ./$dir
fi
