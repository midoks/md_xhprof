# md_xhprof
为xhprof升级版,与PHP7配合使用

# 安装
```
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

 export USE_ZEND_ALLOC=0 	#设置环境变量关闭内存管理
 export USE_ZEND_DTRACE=1 	#开启DTRACE调试
