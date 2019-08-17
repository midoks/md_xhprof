#!/bin/sh
phpize && ./configure --enable-mdprof && make clean && make
