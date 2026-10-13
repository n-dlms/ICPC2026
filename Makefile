CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -MMD -MP -DNDEBUG
SANFLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer
DEBUGFLAGS = -DDEBUG -g -O0 -MMD -MP

# PART 4.4 P PRE cloud-assignment tuning (see src/policies.cpp PpreCloudAssign)
PPA_ALPHA ?= 1.0
PPA_BETA  ?= 1.0
PPA_FLAGS = -DV10_PPA -DPPA_ALPHA=$(PPA_ALPHA) -DPPA_BETA=$(PPA_BETA)
P10_FLAGS = -DV5_DELAY -DV8_PRECOMBINE -DV9_XREMOTE $(PPA_FLAGS)

SRCS = src/Solution.cpp src/protocol.cpp src/task_table.cpp src/state.cpp \
       src/scheduler.cpp src/policies.cpp src/fwsim.cpp
HDRS = src/protocol.hpp src/task_table.hpp src/state.hpp src/fwsim.hpp src/scheduler.hpp \
       src/policies.hpp
OBJS = $(SRCS:src/%.cpp=build/obj/%.o)
OBJS_SAN = $(SRCS:src/%.cpp=build/obj_san/%.o)
OBJS_DBG = $(SRCS:src/%.cpp=build/obj_dbg/%.o)
OBJS_G = $(SRCS:src/%.cpp=build/obj_g/%.o)
OBJS_M = $(SRCS:src/%.cpp=build/obj_m/%.o)
OBJS_1 = $(SRCS:src/%.cpp=build/obj_1/%.o)
OBJS_4 = $(SRCS:src/%.cpp=build/obj_4/%.o)
OBJS_2 = $(SRCS:src/%.cpp=build/obj_2/%.o)
OBJS_5 = $(SRCS:src/%.cpp=build/obj_5/%.o)
OBJS_6 = $(SRCS:src/%.cpp=build/obj_6/%.o)
OBJS_7 = $(SRCS:src/%.cpp=build/obj_7/%.o)
OBJS_8 = $(SRCS:src/%.cpp=build/obj_8/%.o)
OBJS_9 = $(SRCS:src/%.cpp=build/obj_9/%.o)
OBJS_10 = $(SRCS:src/%.cpp=build/obj_10/%.o)
OBJS_13 = $(SRCS:src/%.cpp=build/obj_13/%.o)
DEPS = $(OBJS:.o=.d)
DEPS_SAN = $(OBJS_SAN:.o=.d)
DEPS_DBG = $(OBJS_DBG:.o=.d)
DEPS_G = $(OBJS_G:.o=.d)
DEPS_M = $(OBJS_M:.o=.d)
DEPS_1 = $(OBJS_1:.o=.d)
DEPS_4 = $(OBJS_4:.o=.d)
DEPS_2 = $(OBJS_2:.o=.d)
DEPS_5 = $(OBJS_5:.o=.d)
DEPS_6 = $(OBJS_6:.o=.d)
DEPS_7 = $(OBJS_7:.o=.d)
DEPS_8 = $(OBJS_8:.o=.d)
DEPS_9 = $(OBJS_9:.o=.d)
DEPS_10 = $(OBJS_10:.o=.d)
DEPS_13 = $(OBJS_13:.o=.d)

.PHONY: all san debug submission submit v1_size v3_marginal v4_chunk v2_transfer v5_delay v6_lout v7_balance v8_precombine v9_xremote p10_cloud p11_stagger p12_stagger part8 exp_nohold exp_nocap exp_accumulate exp_accum2 exp_accum3 frozen_v1 frozen_v1_1 clean clobber

.DEFAULT_GOAL := all

-include $(DEPS) $(DEPS_SAN) $(DEPS_DBG) $(DEPS_M) $(DEPS_1) $(DEPS_4) $(DEPS_2) $(DEPS_5) $(DEPS_6) $(DEPS_7) $(DEPS_8) $(DEPS_9) $(DEPS_10) $(DEPS_13)

all: build/solution

submission: build/solution_submit

src/submission.cpp: $(SRCS) $(HDRS) tools/merge_submission.py
	python3 tools/merge_submission.py

build/solution_submit: src/submission.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

build/obj build/obj_san build/obj_dbg build/obj_g build/obj_m build/obj_1 build/obj_4 build/obj_2 build/obj_5:
	mkdir -p $@

build/solution: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# Single-file Codeforces submission: bakes in the current candidate flags
# (see src/submission.cpp) so a plain g++ build runs the best strategy
# without needing -D flags (the judge UI cannot pass them).
build/submission: src/submission.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

submit: build/submission

build/solution_marginal: $(OBJS_M)
	$(CXX) $(CXXFLAGS) -DV3_MARGINAL -o $@ $(OBJS_M)

build/solution_v1: $(OBJS_1)
	$(CXX) $(CXXFLAGS) -DV1_SIZE -o $@ $(OBJS_1)

build/solution_v4: $(OBJS_4)
	$(CXX) $(CXXFLAGS) -DV4_CHUNK -o $@ $(OBJS_4)

build/solution_v2: $(OBJS_2)
	$(CXX) $(CXXFLAGS) -DV2_TRANSFER -o $@ $(OBJS_2)

build/solution_v5: $(OBJS_5)
	$(CXX) $(CXXFLAGS) -DV5_DELAY -o $@ $(OBJS_5)

v3_marginal: build/solution_marginal
v1_size: build/solution_v1
v4_chunk: build/solution_v4
v2_transfer: build/solution_v2
v5_delay: build/solution_v5

v6_lout: build/solution_v6

v7_balance: build/solution_v7

v8_precombine: build/solution_v8

v9_xremote: build/solution_v9

$(OBJS_4): build/obj_4/%.o: src/%.cpp | build/obj_4
	$(CXX) $(CXXFLAGS) -DV4_CHUNK -c -o $@ $<

$(OBJS_2): build/obj_2/%.o: src/%.cpp | build/obj_2
	$(CXX) $(CXXFLAGS) -DV2_TRANSFER -c -o $@ $<

$(OBJS_5): build/obj_5/%.o: src/%.cpp | build/obj_5
	$(CXX) $(CXXFLAGS) -DV5_DELAY -c -o $@ $<

build/obj_6:
	mkdir -p $@

build/solution_v6: $(OBJS_6)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_6)

$(OBJS_6): build/obj_6/%.o: src/%.cpp | build/obj_6
	$(CXX) $(CXXFLAGS) -DV5_DELAY -DV6_LOUT -c -o $@ $<

build/obj_7:
	mkdir -p $@

build/solution_v7: $(OBJS_7)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_7)

$(OBJS_7): build/obj_7/%.o: src/%.cpp | build/obj_7
	$(CXX) $(CXXFLAGS) -DV5_DELAY -DV7_BALANCE -c -o $@ $<

build/obj_8:
	mkdir -p $@

build/solution_v8: $(OBJS_8)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_8)

$(OBJS_8): build/obj_8/%.o: src/%.cpp | build/obj_8
	$(CXX) $(CXXFLAGS) -DV5_DELAY -DV7_BALANCE -DV8_PRECOMBINE -c -o $@ $<

build/obj_9:
	mkdir -p $@

build/solution_v9: $(OBJS_9)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_9)

$(OBJS_9): build/obj_9/%.o: src/%.cpp | build/obj_9
	$(CXX) $(CXXFLAGS) -DV5_DELAY -DV7_BALANCE -DV8_PRECOMBINE -DV9_XREMOTE -c -o $@ $<

build/obj_10:
	mkdir -p $@

build/solution_p10: $(OBJS_10)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_10)

$(OBJS_10): build/obj_10/%.o: src/%.cpp | build/obj_10
	$(CXX) $(CXXFLAGS) $(P10_FLAGS) -c -o $@ $<

p10_cloud: build/solution_p10

# PART 5.4d experiment: accurate transfer-FIFO mirror (V12_MIRROR) + prefill
# stagger (V11_PPRECAP, count-based, PPRE_MAX_INFLIGHT bounds UP depth).
# This build is NON-PROMOTED; the mirror changes candidate behavior via
# SloAwareDelay, so it is validated in isolation before any promotion.
PPRE_MAX_INFLIGHT ?= 2
P12_FLAGS = -DV5_DELAY -DV7_BALANCE -DV8_PRECOMBINE -DV9_XREMOTE -DV12_MIRROR -DV11_PPRECAP -DPPRE_MAX_INFLIGHT=$(PPRE_MAX_INFLIGHT)
OBJS_12 = $(SRCS:src/%.cpp=build/obj_12/%.o)
build/obj_12:
	mkdir -p $@
build/solution_p12: $(OBJS_12)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_12)
$(OBJS_12): build/obj_12/%.o: src/%.cpp | build/obj_12
	$(CXX) $(CXXFLAGS) $(P12_FLAGS) -c -o $@ $<
p12_stagger: build/solution_p12

# PART 8 micro-optimizations (8.1 id-order, 8.2 urgent-first D PRE, 8.3 low-index
# cloud lean). NON-PROMOTED candidate; A/B vs solution_v9 before any freeze call.
$(OBJS_13): build/obj_13/%.o: src/%.cpp | build/obj_13
	$(CXX) $(CXXFLAGS) -DV5_DELAY -DV7_BALANCE -DV8_PRECOMBINE -DV9_XREMOTE -DV13_IDSORT -DV14_URGENTDPRE -DV15_LOWINDEX -c -o $@ $<

build/obj_13:
	mkdir -p $@

build/solution_part8: $(OBJS_13)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_13)

part8: build/solution_part8

# == A/B throughput-starvation variants (D-064b, scratch only; never the artifact) ==
# NH_FLAGS: artifact baseline + NO_HOLD_DPRE (disable SloAwareDelay)  [Variant A]
# NC_FLAGS: artifact baseline + V8_PRECOMBINE + NO_XREMOTE_CAP (unbounded cross-cloud combine) [Variant B]
# AC_FLAGS: artifact baseline + V8_PRECOMBINE + DPRE_ACCUMULATE (D_POST-priority gate) [Variant D]
NH_FLAGS = -DPART7_ADAPTIVE_REGIME -DV12_MIRROR -DV15_LINORDER -DPSGO_STATIC -DV11_PPRECAP -DNO_HOLD_DPRE
NC_FLAGS = -DPART7_ADAPTIVE_REGIME -DV12_MIRROR -DV15_LINORDER -DPSGO_STATIC -DV11_PPRECAP -DV8_PRECOMBINE -DNO_XREMOTE_CAP
AC_FLAGS = -DPART7_ADAPTIVE_REGIME -DV12_MIRROR -DV15_LINORDER -DPSGO_STATIC -DV11_PPRECAP -DV8_PRECOMBINE -DDPRE_ACCUMULATE
AC2_FLAGS = -DPART7_ADAPTIVE_REGIME -DV12_MIRROR -DV15_LINORDER -DPSGO_STATIC -DV11_PPRECAP -DV8_PRECOMBINE -DDPRE_ACCUMULATE -DDPRE_ACCUM_TARGET_K
AC3_FLAGS = -DPART7_ADAPTIVE_REGIME -DV12_MIRROR -DV15_LINORDER -DPSGO_STATIC -DV11_PPRECAP -DV8_PRECOMBINE -DDPRE_ACCUMULATE -DDPRE_ACCUM_COND
OBJS_NH = $(SRCS:src/%.cpp=build/obj_nh/%.o)
OBJS_NC = $(SRCS:src/%.cpp=build/obj_nc/%.o)
OBJS_AC = $(SRCS:src/%.cpp=build/obj_ac/%.o)
OBJS_AC2 = $(SRCS:src/%.cpp=build/obj_ac2/%.o)
OBJS_AC3 = $(SRCS:src/%.cpp=build/obj_ac3/%.o)

build/obj_nh:
	mkdir -p $@
build/obj_nc:
	mkdir -p $@
build/obj_ac:
	mkdir -p $@
build/obj_ac2:
	mkdir -p $@
build/obj_ac3:
	mkdir -p $@

$(OBJS_NH): build/obj_nh/%.o: src/%.cpp | build/obj_nh
	$(CXX) $(CXXFLAGS) $(NH_FLAGS) -c -o $@ $<

$(OBJS_NC): build/obj_nc/%.o: src/%.cpp | build/obj_nc
	$(CXX) $(CXXFLAGS) $(NC_FLAGS) -c -o $@ $<

$(OBJS_AC): build/obj_ac/%.o: src/%.cpp | build/obj_ac
	$(CXX) $(CXXFLAGS) $(AC_FLAGS) -c -o $@ $<

$(OBJS_AC2): build/obj_ac2/%.o: src/%.cpp | build/obj_ac2
	$(CXX) $(CXXFLAGS) $(AC2_FLAGS) -c -o $@ $<

$(OBJS_AC3): build/obj_ac3/%.o: src/%.cpp | build/obj_ac3
	$(CXX) $(CXXFLAGS) $(AC3_FLAGS) -c -o $@ $<

build/exp_nohold: $(OBJS_NH)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_NH)

build/exp_nocap: $(OBJS_NC)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_NC)

build/exp_accumulate: $(OBJS_AC)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_AC)

build/exp_accum2: $(OBJS_AC2)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_AC2)

build/exp_accum3: $(OBJS_AC3)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS_AC3)

exp_nohold: build/exp_nohold
exp_nocap: build/exp_nocap
exp_accumulate: build/exp_accumulate
exp_accum2: build/exp_accum2
exp_accum3: build/exp_accum3

$(OBJS_G): build/obj_g/%.o: src/%.cpp | build/obj_g
	$(CXX) $(CXXFLAGS) -DV3_GREEDY -c -o $@ $<

$(OBJS_M): build/obj_m/%.o: src/%.cpp | build/obj_m
	$(CXX) $(CXXFLAGS) -DV3_MARGINAL -c -o $@ $<

$(OBJS_1): build/obj_1/%.o: src/%.cpp | build/obj_1
	$(CXX) $(CXXFLAGS) -DV1_SIZE -c -o $@ $<

frozen_v1: build/frozen_v1

build/frozen_v1: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(OBJS): build/obj/%.o: src/%.cpp | build/obj
	$(CXX) $(CXXFLAGS) -c -o $@ $<

san: build/solution_san

build/solution_san: $(OBJS_SAN)
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -o $@ $(OBJS_SAN)

$(OBJS_SAN): build/obj_san/%.o: src/%.cpp | build/obj_san
	$(CXX) $(CXXFLAGS) $(SANFLAGS) -c -o $@ $<

debug: build/solution_debug

build/solution_debug: $(OBJS_DBG)
	$(CXX) $(DEBUGFLAGS) -o $@ $(OBJS_DBG)

$(OBJS_DBG): build/obj_dbg/%.o: src/%.cpp | build/obj_dbg
	$(CXX) $(DEBUGFLAGS) -c -o $@ $<

clean:
	rm -rf build/obj build/obj_san build/obj_dbg build/obj_g build/obj_m build/obj_1 build/obj_4 build/obj_2 build/obj_5 build/obj_6 build/obj_7 build/obj_8 build/obj_9 build/obj_10 build/obj_12 build/obj_13 \
	       build/solution build/solution_san build/solution_debug \
	       build/solution_marginal build/solution_v1 build/solution_v4 build/solution_v2 build/solution_v5 build/solution_v6 build/solution_v7 build/solution_v8 build/solution_v9 build/solution_p10 build/solution_p12 build/solution_part8 \
	       build/obj_nh build/obj_nc build/obj_ac build/obj_ac2 build/obj_ac3 build/exp_nohold build/exp_nocap build/exp_accumulate build/exp_accum2 build/exp_accum3
	find build -name '*.d' -delete 2>/dev/null || true

clobber: clean
	rm -rf build
