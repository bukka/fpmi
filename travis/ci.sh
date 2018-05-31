#!/usr/bin/env sh
git clone --depth=5 --branch=$PHP https://github.com/php/php-src.git
cd php-src/sapi
git clone --depth=5 --branch=master https://github.com/bukka/fpmi.git
cd ../
./buildconf --force
./configure --disable-all --enable-fpmi
make