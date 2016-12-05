dnl $Id$
dnl config.m4 for extension md_xhprof

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

PHP_ARG_WITH(md_xhprof, for md_xhprof support,
Make sure that the comment is aligned:
[  --with-md_xhprof             Include md_xhprof support])

dnl Otherwise use enable:

dnl PHP_ARG_ENABLE(md_xhprof, whether to enable md_xhprof support,
dnl Make sure that the comment is aligned:
dnl [  --enable-md_xhprof           Enable md_xhprof support])

if test "$PHP_MD_XHPROF" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-md_xhprof -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/md_xhprof.h"  # you most likely want to change this
  dnl if test -r $PHP_MD_XHPROF/$SEARCH_FOR; then # path given as parameter
  dnl   MD_XHPROF_DIR=$PHP_MD_XHPROF
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for md_xhprof files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       MD_XHPROF_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$MD_XHPROF_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the md_xhprof distribution])
  dnl fi

  dnl # --with-md_xhprof -> add include path
  dnl PHP_ADD_INCLUDE($MD_XHPROF_DIR/include)

  dnl # --with-md_xhprof -> check for lib and symbol presence
  dnl LIBNAME=md_xhprof # you may want to change this
  dnl LIBSYMBOL=md_xhprof # you most likely want to change this 

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  dnl   PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $MD_XHPROF_DIR/$PHP_LIBDIR, MD_XHPROF_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_MD_XHPROFLIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong md_xhprof lib version or lib not found])
  dnl ],[
  dnl   -L$MD_XHPROF_DIR/$PHP_LIBDIR -lm
  dnl ])
  dnl
  dnl PHP_SUBST(MD_XHPROF_SHARED_LIBADD)

  md_xhprof_source="md_xhprof.c \
        xhprof.c"

  PHP_NEW_EXTENSION(md_xhprof, $md_xhprof_source, $ext_shared)
fi
