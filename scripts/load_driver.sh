#!/bin/bash
set -e

cd ../cmake-build-debug/kernel
sudo insmod vma_driver.ko