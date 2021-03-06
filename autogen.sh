#!/bin/sh

myname=`basename $0`
CONFIGURE_OPTIONS=

if test ! -f configure.ac || test ! -f main.c; then
  echo "$myname: error: cannot run from outside the tvi source directory"
  exit 1
fi

need_autoconf="2.57"
ac_version=`${AUTOCONF:-autoconf} --version 2>/dev/null | \
  head -n 1 | sed -e 's/^[^0-9]*//' -e 's/[a-z]* *$//'`

if test -z "$ac_version"; then
  echo "$myname: error: autoconf not found, version $need_autoconf or newer"
  echo "                must be installed"
  exit 1
fi

old_IFS=$IFS
IFS='.'
set $ac_version
IFS=$old_IFS

if test "$1" = "2" -a "$2" -lt "57" || test "$1" -lt "2"; then
  echo "$myname: error: autoconf version $ac_version found, but version"
  echo "                $need_autoconf or newer must be installed. If you"
  echo "                have a sufficient autoconf installed, but it is not"
  echo "                named 'autoconf', try setting the AUTOCONF"
  echo "                environment variable"
  exit 1
fi

echo "*** autoconf version $ac_version (ok)"

am4te_version=`${AUTOM4TE:-autom4te} --version 2>/dev/null | \
  head -n 1 | sed -e 's/autom4te\(.*\)/\1/' -e \
  's/^[^0-9]*//' -e 's/[a-z]* *$//'`

if test -z "$am4te_version"; then
  echo "$myname: error: autom4te not found"
  exit 1
fi

if test "$am4te_version" = "$ac_version"; then
  echo "*** autom4te version $am4te_version (ok)"
else
  echo "$myname: error: autom4te version $am4te_version does not match"
  echo "                autoconf version $ac_version"
  exit 1
fi

need_autoheader="2.50"
ah_version=`${AUTOHEADER:-autoheader} --version 2>/dev/null | \
  head -n 1 | sed -e 's/^[^0-9]*//' -e 's/[a-z]* *$//'`

if test -x "$ah_version"; then
  echo "$myname: error: autoheader not found! Version $need_autoheader or"
  echo "                newer must be installed"
  exit 1
fi

old_IFS=$IFS
IFS='.'
set $ah_version
IFS=$old_IFS

if test "$1" = "2" -a "$2" -lt "50" || test "$1" -lt "2"; then
  echo "$myname: error: autoheader version $ah_version found, but"
  echo "                version $need_autoheader or newer must be installed."
  echo "                If you have a sufficient autoheader installed, but it"
  echo "                is not named 'autoheader', try setting the AUTOHEADER"
  echo "                environment variable"
  exit 1
fi

echo "*** autoheader version $ah_version (ok)"

need_automake="1.7"
am_version=`${AUTOMAKE:-automake} --version 2>/dev/null | \
  head -n 1 | sed -e 's/^.* \([0-9]\)/\1/' \
  -e 's/[a-z]* *$//' -e 's/\(.*\)\(-p.*\)/\1/'`

if test -z "$am_version"; then
  echo "$myname error: automake not found, version $need_automake or"
  echo "               newer must be installed"
  exit 1
fi

old_IFS=$IFS
IFS='.'
set $am_version
IFS=$old_IFS

if test "$1" = "1" -a "$2" -lt "7" || test "$1" -lt "1"; then
  echo "$myname: error: automake version $am_version found, but version"
  echo "                $need_automake or newer must be installed. If you"
  echo "                have a sufficient automake installed, but it is not"
  echo "                named 'automake', try setting the AUTOMAKE"
  echo "                environment variable"
  exit 1
fi

echo "*** automake version $am_version (ok)"

acloc_version=`${ACLOCAL:-aclocal} --version 2>/dev/null | \
  head -n 1 | sed -e 's/^.* \([0-9]\)/\1/' \
  -e 's/[a-z]* *$//' -e 's/\(.*\)\(-p.*\)/\1/'`

if test -z "$acloc_version"; then
  echo "$myname: error: aclocal not found!"
  exit 1
fi

if test "$acloc_version" = "$am_version"; then
  echo "*** aclocal version $acloc_version (ok)"
else
  echo "$myname: error: aclocal version $acloc_version does not match"
  echo "                automake version $am_version"
  exit 1
fi

m4=`(${M4:-m4} --version || ${M4:-gm4} --version) 2>/dev/null | head -n 1`
m4_version=`echo $m4 | sed -e 's/^.* \([0-9]\)/\1/' -e 's/[a-z]* *$//'`

if { echo $m4 | grep "GNU" >/dev/null 2>&1; } then
  echo "*** GNU m4 version $m4_version (ok)"
else
  if test -z "$m4"; then
    echo "$myname: error: m4 version not recognized"
    echo "$myname: error: GNU m4 needs to be installed"
  else
    echo "$myname: error: m4 version $m4 found"
    echo "$myname: error: but GNU m4 needs to be installed"
  fi
  exit 1
fi

echo "*** Running: ${ACLOCAL:-aclocal} $ACLOCAL_FLAGS"
${ACLOCAL:-aclocal} $ACLOCAL_FLAGS ||
  { echo "$myname: error: aclocal failed"; exit 1; }

echo "*** Running: ${AUTOHEADER:-autoheader}"
${AUTOHEADER:-autoheader} ||
  { echo "$myname: error: autoheader failed"; exit 1; }

echo "*** Running: ${AUTOCONF:-autoconf}"
${AUTOCONF:-autoconf} || { echo "$myname: error: autoconf failed"; exit 1; }

echo "*** Running: ${AUTOMAKE:-automake} -a -c"
${AUTOMAKE:-automake} -a -c ||
  { echo "$myname: error: automake failed"; exit 1; }

echo
echo "=================================================================="
echo
echo "To build tvi type:"
echo
echo "    ./configure"
echo "    make"
echo
echo "To install tvi type:"
echo
echo "    sudo make install"
echo
echo "=================================================================="
echo
exit 0
