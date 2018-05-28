#!/usr/bin/env sh
git clone https://github.com/php/php-src
cd php-src
git checkout $PHP
cd sapi
git clone https://github.com/bukka/fpmi.git
cd ../
./buildconf --force
./configure --disable-all --enable-fpmi
make