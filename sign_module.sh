#!/bin/bash

"/usr/src/linux-headers-$(uname -r)/scripts/sign-file" sha256 \
signing_key.priv \
signing_key.x509 \
cmake-build-debug/kernel/vma_driver.ko