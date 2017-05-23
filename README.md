# md_xhprof
```
为xhprof升级版,与PHP7配合使用
#使用时需要设置环境变量
export USE_ZEND_DTRACE=1
```
# 安装
```

# 快速安装
curl -fsSL  https://raw.githubusercontent.com/midoks/md_xhprof/master/scripts/install.sh | sh


cd ~~/md_xhprof/src
$DIR/php/$PHP_VER/bin/phpize
./configure \
--with-php-config=~~/php7/bin/php-config --enable-debug
make && make install 

```

# 使用方法及原因

- PHP7编译时要使用(--enable-dtrace)
 * http://lxr.php.net/xref/PHP-7.0/Zend/zend.c#687
 * http://lxr.php.net/xref/PHP-7.1/Zend/zend.c#702

# 调试

- export USE_ZEND_ALLOC=0 	#关闭内存管理
- export USE_ZEND_DTRACE=1 	#开启DTRACE调试
- yum install valgrind
- valgrind --leak-check=full php leak.php
- php -dvld.active=1 leak.php 

# 相关
```
PHP集成环境(mac),已经集成
https://github.com/midoks/mdserver-mac
PHP集成环境(win)
https://github.com/midoks/MDserver_64
```

# Windows Xhprof(官方的有问题)
- https://github.com/midoks/midoks/tree/master/c/win_php_xhprof
- [php5.5_v11_ts_dll](https://github.com/midoks/midoks/tree/master/c/win_php_xhprof/xhprof5.5/dll)
- [php7_v14_ts_dll](https://github.com/midoks/midoks/tree/master/c/win_php_xhprof/md_xhprof7/dll)


