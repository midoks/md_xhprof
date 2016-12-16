#!/bin/sh

echo "You PHP Dir[Prefix]?"
read DIR

case $DIR in
    $DIR|y|Y|yes|Yes)
        PHP_DIR=$DIR
        ;;
    n|N|no|No)
        exit 0
        ;;
esac


wget https://github.com/midoks/md_xhprof/archive/master.zip

unzip -o master.zip 
rm -f master.zip

cd md_xhprof-master/src


#/usr/local/php70


$PHP_DIR/bin/phpize

sleep 1

./configure \
--with-php-config=$PHP_DIR/bin/php-config
make && make install

rm -rf md_xhprof-master