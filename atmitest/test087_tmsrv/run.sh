#!/bin/bash
##
## @brief TMSRV State Driving verification - test launcher
##
## @file run.sh
##
## -----------------------------------------------------------------------------
## Enduro/X Middleware Platform for Distributed Transaction Processing
## Copyright (C) 2009-2016, ATR Baltic, Ltd. All Rights Reserved.
## Copyright (C) 2017-2019, Mavimax, Ltd. All Rights Reserved.
## This software is released under one of the following licenses:
## AGPL (with Java and Go exceptions) or Mavimax's license for commercial use.
## See LICENSE file for full text.
## -----------------------------------------------------------------------------
## AGPL license:
## 
## This program is free software; you can redistribute it and/or modify it under
## the terms of the GNU Affero General Public License, version 3 as published
## by the Free Software Foundation;
##
## This program is distributed in the hope that it will be useful, but WITHOUT ANY
## WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
## PARTICULAR PURPOSE. See the GNU Affero General Public License, version 3
## for more details.
##
## You should have received a copy of the GNU Affero General Public License along 
## with this program; if not, write to the Free Software Foundation, Inc., 
## 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
##
## -----------------------------------------------------------------------------
## A commercial use license is available from Mavimax, Ltd
## contact@mavimax.com
## -----------------------------------------------------------------------------
##

export TESTNAME="test087_tmsrv"

PWD=`pwd`
if [ `echo $PWD | grep $TESTNAME ` ]; then
    # Do nothing 
    echo > /dev/null
else
    # started from parent folder
    pushd .
    echo "Doing cd"
    cd $TESTNAME
fi;

. ../testenv.sh

export TESTDIR="$NDRX_APPHOME/atmitest/$TESTNAME"
export PATH=$PATH:$TESTDIR
export NDRX_ULOG=$TESTDIR
export NDRX_TOUT=10
export NDRX_CCONFIG=$TESTDIR/test.ini
export NDRX_SILENT=Y
NDRX_EXT=so
if [ "$(uname)" == "Darwin" ]; then
    NDRX_EXT=dylib
fi
export NDRX_XA_DRIVERLIB=../../xadrv/tms/libndrxxatmsx.$NDRX_EXT

#
# Domain 1 - here client will live
#
set_dom1() {
    echo "Setting domain 1"
    . ../dom1.sh
    export NDRX_CONFIG=$TESTDIR/ndrxconfig-dom1.xml
    export NDRX_DMNLOG=$TESTDIR/ndrxd-dom1.log
    export NDRX_LOG=$TESTDIR/ndrx-dom1.log
    export NDRX_DEBUG_CONF=$TESTDIR/debug-dom1.conf
}

#
# Generic exit function
#
function go_out {
    echo "Test exiting with: $1"
    
    set_dom1;
    xadmin stop -y
    xadmin down -y 2>/dev/null

    popd 2>/dev/null
    exit $1
}

#
#  cleanup tmsrv logfiles
#
function clean_logs {
    #
    # Cleanup test logs...
    #
    rm -rf ./log1 2>/dev/null; mkdir ./log1
    rm -rf ./log2 2>/dev/null; mkdir ./log2
    rm -rf ./log3 2>/dev/null; mkdir ./log3
}

#
# Build programs
#
function buildprograms_logs {

    LIBMODE=$1

    ################################################################################
    echo "Building programs..."
    ################################################################################

    echo ""
    echo "************************************************************************"
    echo "Building tmsrv NULL ..."
    echo "************************************************************************"
    buildtms -o tmsrv_null -rnullsw -v

    RET=$?

    if [ "X$RET" != "X0" ]; then
        echo "Failed to build tmstest: $RET"
        go_out 1
    fi

    echo ""
    echo "************************************************************************"
    echo "Building tmsrv libl87_1$LIBMODE ..."
    echo "************************************************************************"
    buildtms -o tmsrv_lib1 -rlibl87_1$LIBMODE -v

    RET=$?

    if [ "X$RET" != "X0" ]; then
        echo "Failed to build tmstest: $RET"
        go_out 1
    fi

    echo ""
    echo "************************************************************************"
    echo "Building tmsrv libl87_2$LIBMODE ..."
    echo "************************************************************************"
    buildtms -o tmsrv_lib2 -rlibl87_2$LIBMODE -v

    RET=$?

    if [ "X$RET" != "X0" ]; then
        echo "Failed to build tmstest: $RET"
        go_out 1
    fi

    echo ""
    echo "************************************************************************"
    echo "Building atmiclt87 NULL ..."
    echo "************************************************************************"
    buildclient -o atmiclt87 -rnullsw -l atmiclt87.c -v
    RET=$?

    if [ "X$RET" != "X0" ]; then
        echo "Build atmiclt87 failed"
        go_out 1
    fi

    echo ""
    echo "************************************************************************"
    echo "Building atmisv87 NULL ..."
    echo "************************************************************************"
    buildserver -o atmisv87_1 -rlibl87_1$LIBMODE -l atmisv87_1.c -sTESTSV1 -v
    RET=$?

    if [ "X$RET" != "X0" ]; then
        echo "Build atmiclt87 failed"
        go_out 1
    fi

    echo ""
    echo "************************************************************************"
    echo "Building atmisv87 NULL ..."
    echo "************************************************************************"
    buildserver -o atmisv87_2 -rlibl87_2$LIBMODE -l atmisv87_2.c -sTESTSV2 -v
    RET=$?

    if [ "X$RET" != "X0" ]; then
        echo "Build atmiclt87 failed"
        go_out 1
    fi

}

#
# We will work only in dom1
#
set_dom1;

################################################################################
echo "Configure build environment..."
################################################################################

# Additional, application specific
COMPFLAGS=""
#
# export the library path.
#
case $UNAME in

  AIX)
    # check compiler, we have a set of things required for each compiler to actually build the binary
    $CC -qversion 2>/dev/null
    RET=$?
    export OBJECT_MODE=64

    if [ "X$RET" == "X0" ]; then
	echo "Xlc compiler..."
        COMPFLAGS="-brtl -qtls -q64"
    else
	echo "Default to gcc..."
        COMPFLAGS="-Wl,-brtl -maix64"
    fi

    ;;
  SunOS)
        COMPFLAGS="-m64"
    ;;

  *)
    # default..
    ;;
esac

# Support sanitizer
#if [ "X`xadmin pmode 2>/dev/null | grep '#define NDRX_SANITIZE'`" != "X" ]; then
    COMPFLAGS="$COMPFLAGS -fsanitize=address"
#fi

export NDRX_HOME=.
export PATH=$PATH:$PWD/../../buildtools
export CFLAGS="-I../../include -L../../libatmi -L../../libubf -L../../tmsrv -L../../libatmisrv -L../../libexuuid -L../../libexthpool -L../../libnstd $COMPFLAGS"

clean_logs;
rm *.log

#
# Firstly test with join
#
buildprograms_logs "";

xadmin start -y || go_out 1

#
# Run OK case...
#

echo ""
echo "************************************************************************"
echo "Commit OK case ..."
echo "************************************************************************"

cat << EOF > lib1.rets
xa_open_entry:0:1:0
xa_close_entry:0:1:0
xa_start_entry:0:1:0
xa_end_entry:0:1:0
xa_rollback_entry:0:1:0
xa_prepare_entry:0:1:0
xa_commit_entry:0:1:0
xa_recover_entry:0:1:0
xa_forget_entry:0:1:0
xa_complete_entry:0:1:0
xa_open_entry:0:1:0
xa_close_entry:0:1:0
xa_start_entry:0:1:0
EOF

cat << EOF > lib2.rets
xa_open_entry:0:1:0
xa_close_entry:0:1:0
xa_start_entry:0:1:0
xa_end_entry:0:1:0
xa_rollback_entry:0:1:0
xa_prepare_entry:0:1:0
xa_commit_entry:0:1:0
xa_recover_entry:0:1:0
xa_forget_entry:0:1:0
xa_complete_entry:0:1:0
xa_open_entry:0:1:0
xa_close_entry:0:1:0
xa_start_entry:0:1:0
EOF


NDRX_CCTAG="RM3" ./atmiclt87
RET=$?

if [ "X$RET" != "X0" ]; then
    echo "Build atmiclt87 failed"
    go_out 1
fi

xadmin stop -y
xadmin start -y

echo ""
echo "************************************************************************"
echo "Prepare failed case ... with RM error"
echo "************************************************************************"

cat << EOF > lib1.rets
xa_open_entry:0:1:0
xa_close_entry:0:1:0
xa_start_entry:0:1:0
xa_end_entry:0:1:0
xa_rollback_entry:0:1:0
xa_prepare_entry:0:1:0
xa_commit_entry:0:1:0
xa_recover_entry:0:1:0
xa_forget_entry:0:1:0
xa_complete_entry:0:1:0
xa_open_entry:0:1:0
xa_close_entry:0:1:0
xa_start_entry:0:1:0
EOF

cat << EOF > lib2.rets
xa_open_entry:0:1:0
xa_close_entry:0:1:0
xa_start_entry:0:1:0
xa_end_entry:0:1:0
xa_rollback_entry:0:1:0
xa_prepare_entry:-3:1:0
xa_commit_entry:0:1:0
xa_recover_entry:0:1:0
xa_forget_entry:0:1:0
xa_complete_entry:0:1:0
xa_open_entry:0:1:0
xa_close_entry:0:1:0
xa_start_entry:0:1:0
EOF

NDRX_CCTAG="RM3" ./atmiclt87
RET=$?

if [ "X$RET" != "X0" ]; then
    echo "atmiclt87 failed"
    go_out 1
fi

xadmin psc
xadmin pt 

echo ""
echo "************************************************************************"
echo "Commit retry..."
echo "************************************************************************"


go_out $RET

# vim: set ts=4 sw=4 et smartindent:

