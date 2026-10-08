#!/bin/sh
# Regression tests that run the core headless with test ROMs in place of the BIOS.
#
# usage: tests/run_tests.sh core.so suite [args]
#   mdec                              MDEC decode, compared with tests/mdec_decode.expected
#   polyline                          long GPU poly-lines, compared with tests/gpu_polyline.expected
#   regfuzz [first] [last] [frames]   register fuzzer, one run per seed (default 1 20 120)
#   statefuzz [first] [last] [iters]  corrupted save states loaded into a fuzzed system (default 1 3 150)
#
# UPDATE_EXPECTED=1 rewrites an expected file from the core under test.
# Needs clang, ld.lld and llvm-objcopy for the MIPS test ROMs, python3, and a
# host C compiler for the frontend. CC and FRONTEND_CFLAGS pick the host
# compiler and its flags (match the core's sanitizers); FE_CPU and FE_FASTMEM
# pick the core's CPU mode. A run fails on a nonzero exit, a sanitizer report
# or a hang (TIMEOUT seconds per run, default 300).
set -eu

CORE=$(readlink -f "$1")
SUITE=$2
shift 2

TESTS=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-$(mktemp -d)}
CC=${CC:-cc}
CLANG=${CLANG:-clang}
LLD=${LLD:-ld.lld}
OBJCOPY=${OBJCOPY:-llvm-objcopy}
TIMEOUT=${TIMEOUT:-300}
export FE_CPU=${FE_CPU:-Recompiler}
export FE_FASTMEM=${FE_FASTMEM:-MMap}
export ASAN_OPTIONS=${ASAN_OPTIONS:-halt_on_error=1:detect_leaks=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}

mkdir -p "$WORK"

build_rom()
{
  name=$1
  flags="--target=mipsel-unknown-none -march=mips1 -mno-abicalls -fno-pic -G0 -O2 -ffreestanding -fno-builtin -nostdlib"
  for src in crt0.S "$name.c"; do
    if ! "$CLANG" $flags -c "$TESTS/guest/$src" -o "$WORK/${src%.*}.o" > "$WORK/cc.log" 2>&1; then
      cat "$WORK/cc.log"
      exit 1
    fi
    # clang calls MIPS I experimental; it is the console's CPU.
    grep -v "experimental" "$WORK/cc.log" || true
  done
  "$LLD" -T "$TESTS/guest/rom.ld" "$WORK/crt0.o" "$WORK/$name.o" -o "$WORK/$name.elf"
  "$OBJCOPY" -O binary -j .reset -j .magic -j .params -j .exc -j .text -j .rodata "$WORK/$name.elf" "$WORK/$name.rom"
}

build_frontend()
{
  "$CC" -O1 -g ${FRONTEND_CFLAGS:-} -I"$TESTS/../src/libretro" -I"$TESTS/../dep/libretro-common/include" \
    "$TESTS/harness/frontend.c" -o "$WORK/frontend" -ldl
}

# run_rom rom seed mode frames [content]: runs one ROM in its own BIOS directory; output in $WORK/run.log
run_rom()
{
  dir="$WORK/bios-$2"
  rm -rf "$dir"
  mkdir -p "$dir"
  python3 "$TESTS/guest/mkrom.py" "$WORK/$1.rom" "$dir/bios.bin" "$2" "$3"
  rc=0
  FE_DIR="$dir" timeout "$TIMEOUT" "$WORK/frontend" "$CORE" "$4" ${5:-} > "$WORK/run.log" 2>&1 || rc=$?
  rm -rf "$dir"
  if [ $rc -ne 0 ] || grep -qE "runtime error|ERROR: (Address|Leak)Sanitizer|WARNING: ThreadSanitizer" "$WORK/run.log"; then
    [ $rc -eq 124 ] && echo "timed out after $TIMEOUT s"
    cat "$WORK/run.log"
    return 1
  fi
  return 0
}

# run_golden rom frames dump_words cases [pairs=N]
run_golden()
{
  build_rom "$1"
  if ! RAMDUMP=0:$3 run_rom "$1" 1 0 "$2"; then
    echo "$1: run failed"
    exit 1
  fi
  python3 "$TESTS/check_expected.py" "$WORK/run.log" "$TESTS/$1.expected" "$4" ${5:-}
}

build_frontend

case "$SUITE" in
  mdec)
    run_golden mdec_decode 1500 160 20 pairs=16
    ;;

  polyline)
    run_golden gpu_polyline 600 80 4
    ;;

  regfuzz)
    build_rom regfuzz
    python3 "$TESTS/make_disc.py" "$WORK"
    first=${1:-1}
    last=${2:-20}
    frames=${3:-120}
    seed=$first
    while [ "$seed" -le "$last" ]; do
      if ! run_rom regfuzz "$seed" 0xFFFFFFFF "$frames" "$WORK/disc.cue"; then
        echo "regfuzz: seed $seed failed"
        exit 1
      fi
      seed=$((seed + 1))
    done
    echo "regfuzz: seeds $first-$last passed"
    ;;

  statefuzz)
    build_rom regfuzz
    python3 "$TESTS/make_disc.py" "$WORK"
    first=${1:-1}
    last=${2:-3}
    iters=${3:-150}
    seed=$first
    while [ "$seed" -le "$last" ]; do
      if ! STATEFUZZ=$iters SEED=$seed run_rom regfuzz "$seed" 0xFFFFFFFF 120 "$WORK/disc.cue"; then
        echo "statefuzz: seed $seed failed"
        exit 1
      fi
      grep "state fuzz:" "$WORK/run.log"
      seed=$((seed + 1))
    done
    echo "statefuzz: seeds $first-$last passed"
    ;;

  *)
    echo "unknown suite $SUITE"
    exit 1
    ;;
esac
