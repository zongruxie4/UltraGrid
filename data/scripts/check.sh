#!/bin/sh -eux
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2019-2026 CESNET, zájmové sdružení právnických osob
#
# here are additional run-checks of uv/reflector

# UV_TARGET=$1
REFLECTOR_TARGET=$2

# test hd-rum-transcode crasn as in the commit 66f1f0e2
$REFLECTOR_TARGET 8M 5004 & pid=$!
sleep 1
kill $pid || true
wait $pid || [ $? -eq 143 ]
