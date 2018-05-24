#!/bin/bash
set -e
set -o pipefail
set -u

CXX=${CXX:-c++}

ALLOCATOR=${ALLOCATOR:-}
# ALLOCATOR="-DTCMALLOC=1 -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free -ltcmalloc"
# ALLOCATOR="-DJEMALLOC=1 -fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free -I$(jemalloc-config --includedir) -L$(jemalloc-config --libdir) -Wl,-rpath,$(jemalloc-config --libdir)/lib -ljemalloc"
# ALLOCATOR="$ALLOCATOR -DMEASURE_MEMORY=1"

OPT=${OPT:--O3 -flto}
OPT="$OPT -DNDEBUG"
# OPT="-Og -g -fsanitize=address,undefined"

echo "Using \$CXX='${CXX}' with \$OPT='${OPT}' and \$ALLOCATOR='${ALLOCATOR}'"

$CXX -std=c++11 $OPT -lbenchmark $ALLOCATOR -pthread main.cpp

./a.out --benchmark_counters_tabular=true --benchmark_out=result."$(date +%s)".json --benchmark_out_format=json --benchmark_repetitions=6
