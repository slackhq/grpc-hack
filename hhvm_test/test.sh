#!/bin/bash
set -ex
cd "$(dirname "$0")"
EDIR=`dirname $PWD`
hhvm \
  -d extension_dir=$EDIR \
  -d hhvm.extensions[]=grpc_hack.so \
  test.php
