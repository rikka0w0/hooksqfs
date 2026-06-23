#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

HOOKSQFS_TEST_FILE="${HOOKSQFS_TEST_FILE:-/home/rikka/addons.sqfs}"
HOOKSQFS_TEST_PREFIX="${HOOKSQFS_TEST_PREFIX:-/home/rikka/Steam/l4d2/left4dead2/addons}"
HOOKSQFS_TEST_PATH="${HOOKSQFS_TEST_PATH:-${HOOKSQFS_TEST_PREFIX}/tanksplayground.vpk}"
HOOKSQFS_TEST_THREADS="${HOOKSQFS_TEST_THREADS:-8}"
HOOKSQFS_TEST_ITERS="${HOOKSQFS_TEST_ITERS:-32}"

test_bin="${script_dir}/test_concurrent"
trap 'rm -f "${test_bin}"' EXIT

make -C "${script_dir}" BITS=32 libhooksqfs.so

gcc -m32 -fPIC -O2 -Wall -Wextra -pthread \
	-o "${test_bin}" "${script_dir}/test_concurrent.c" -pthread

LD_PRELOAD="${script_dir}/libhooksqfs.so" \
HOOKSQFS_FILE="${HOOKSQFS_TEST_FILE}" \
HOOKSQFS_PREFIX="${HOOKSQFS_TEST_PREFIX}" \
"${test_bin}" "${HOOKSQFS_TEST_PATH}" "${HOOKSQFS_TEST_THREADS}" "${HOOKSQFS_TEST_ITERS}"
