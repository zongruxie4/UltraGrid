#!/bin/sh -eu
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2019-2026 CESNET, zájmové sdružení právnických osob
#
# here are additional run-checks of uv/reflector

# UV_TARGET=$1
REFLECTOR_TARGET=$2

# test hd-rum-transcode crasn as in the commit 66f1f0e2
check_hd_rum_transcode() {
        printf "Testing hd-rum-translator (1 sec dummy run) .............................. "
        if [ ! "$REFLECTOR_TARGET" ]; then
                echo --
                return
        fi
        $REFLECTOR_TARGET 8M 5004 >/dev/null 2>&1& pid=$!
        sleep 1
        kill $pid
        wait $pid || rc=$?
        if [ "${rc-143}" -ne 143 ]; then
                echo Fail
                exit "$rc"
        fi
        echo Ok
}

check_hd_rum_transcode
