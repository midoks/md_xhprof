#!/bin/sh

echo -n "You PHP Dir?"
read PHP_DIR
echo $PHP_DIR
# case $PHP_DIR in
#     y|Y|yes|Yes)
#         PHP_DIR='/usr/local/php'
#         ;;
#     n|N|no|No)
#         exit 0
#         ENV='uat'
#         ;;
# esac

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