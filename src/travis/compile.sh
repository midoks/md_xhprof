#!/bin/sh
cd src && phpize && ./configure --enable-mdprof && make clean && make
