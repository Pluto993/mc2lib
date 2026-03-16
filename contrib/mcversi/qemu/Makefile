# Makefile for QEMU-based McVerSi workflow (manual iteration, ObsLog mode)
#
# Targets:
#   host_generate  -- build on host (x86_64), generates initial test + GA pool
#   host_verify    -- build on host (x86_64), verifies + evolves + generates next
#   guest_runner   -- build for guest (native x86_64 or cross-compile)
#
# Usage:
#   make host          -- build host-side tools
#   make guest         -- build guest-side runner
#   make all           -- build everything
#   make clean         -- remove build artifacts
#
# Workflow (manual iteration with observation log):
#   1. make host guest
#   2. mkdir -p ./data
#   3. ./host_generate 4 50 8192 16 0x100000000 ./data
#   4. (copy data/ to QEMU guest, or use directly with qemu-x86_64)
#   5. ./guest_runner ./data     (or: qemu-x86_64 ./guest_runner ./data)
#   6. (copy data/test_result.bin + data/obs_log.bin back to host)
#   7. ./host_verify ./data
#   8. Repeat from step 4
#
# Key files produced:
#   thread_*.bin    -- per-thread machine code (with obs log store epilogues)
#   meta.bin        -- metadata including per-thread obs_log_count
#   pool_state.bin  -- serialized GA population state
#   test_result.bin -- final memory dump after guest execution
#   obs_log.bin     -- per-Read observation log (what each Read saw)

MC2LIB_DIR = ../../../include

# ---- Host tools (native x86_64 build) ----
HOST_CXX      = g++
HOST_CXXFLAGS = -std=c++14 -O2 -Wall -I$(MC2LIB_DIR)

# ---- Guest runner (can cross-compile) ----
# For native x86_64 guest:
GUEST_CC       = gcc
GUEST_CFLAGS   = -O2 -Wall
GUEST_LDFLAGS  = -lpthread

# For QEMU user mode (same arch):
# No changes needed, just run with: qemu-x86_64 ./guest_runner ./data

.PHONY: all host guest clean

all: host guest

host: host_generate host_verify

guest: guest_runner

host_generate: host_generate.cpp
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ $<

host_verify: host_verify.cpp
	$(HOST_CXX) $(HOST_CXXFLAGS) -o $@ $<

guest_runner: guest_runner.c
	$(GUEST_CC) $(GUEST_CFLAGS) -o $@ $< $(GUEST_LDFLAGS)

clean:
	rm -f host_generate host_verify guest_runner
	rm -f *.o
