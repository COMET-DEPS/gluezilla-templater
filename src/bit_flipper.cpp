/**
 * @file     bit_flipper.cpp
 *
 * @brief    Contains the actual rowhammer process, including checking for
 *           bit flips.
 *
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>
#include <x86intrin.h>

#include "config.h"
#include "db.h"
#include "dram_address.h"
#include "flip_finder.h"
#include "logging.h"
#include "operators.h"
#include "temperature_controller.h"

#include "bit_flipper.h"

static inline __attribute__((always_inline)) void clflush(virtaddr_t addr) {
#ifndef __CLFLUSHOPT__
  _mm_clflush(reinterpret_cast<void *>(addr));
#else
  _mm_clflushopt(reinterpret_cast<void *>(addr));
#endif
}

static inline __attribute__((always_inline)) void mfence() {
  asm volatile("mfence" ::: "memory");
  // _mm_mfence();
}

static inline __attribute__((always_inline)) uint64_t rdtscp(void) {
  uint64_t lo, hi;
  asm volatile("rdtscp\n" : "=a"(lo), "=d"(hi)::"%rcx");
  return (hi << 32) | lo;
}

#ifdef MEASURE_HAMMER_DURATION
#define TIMESPEC_NSEC(ts) ((ts)->tv_sec * 1e9 + (ts)->tv_nsec)

static inline __attribute__((always_inline)) uint64_t realtime_now() {
  struct timespec now_ts;
  clock_gettime(CLOCK_MONOTONIC, &now_ts);
  return TIMESPEC_NSEC(&now_ts);
}
#endif // MEASURE_HAMMER_DURATION

/**
 * @brief    Default rowhammer implementation as machine code.
 *
 * If USE_ASMJIT is specified asmjit is used to construct the machine code,
 * otherwise our manual implementation is used.
 *
 * @return   uint64_t            The hammer duration in ms if
 *                               MEASURE_HAMMER_DURATION is defined,
 *                               otherwise 0.
 */
uint64_t BitFlipper::hammer_aggs_machinecode() const {

  void (*fn)() = nullptr;

#ifdef USE_ASMJIT
  asmjit::JitRuntime rt;
  asmjit::CodeHolder code;
  code.init(rt.environment());
  asmjit::x86::Assembler a(&code);

  asmjit::Label for_begin = a.newLabel();
  asmjit::Label for_end = a.newLabel();

  // for(rbx = hammer_count, rbx >= 0, rbx--)
  // rbx = hammer_count
  a.mov(asmjit::x86::rbx, config.hammer_count);
  a.bind(for_begin);
  // rbx >= 0
  a.cmp(asmjit::x86::rbx, 0);
  a.jle(for_end);

  // access and flush the aggressors
  for (virtaddr_t agg : virt_aggs) {
    a.mov(asmjit::x86::rax, agg);
    a.mov(asmjit::x86::rcx, asmjit::x86::ptr(asmjit::x86::rax));
    for (uint64_t i = 0; i < config.nop_count; i++) {
      a.nop();
    }
#ifndef __CLFLUSHOPF__
    a.clflush(asmjit::x86::ptr(asmjit::x86::rax));
    a.lfence();
#else
    a.clflushopt(asmjit::x86::ptr(asmjit::x86::rax));
#endif
  }
  a.mfence();

  // rbx--
  a.dec(asmjit::x86::rbx);

  a.jmp(for_begin);
  a.bind(for_end);

  a.ret();

  asmjit::Error err = rt.add(&fn, &code);
  if (err)
    log_error_and_exit("Error occurred while jitting code!");
#else

  // clang-format off

    // The function start initializes the function and
    // adds the hammercount for loop iteration
    static const std::array<uint8_t, 11> function_start = {
      0xf3, 0x0f, 0x1e, 0xfa,  // ENDBR64
      0x55,                    // push rbp
      0x48, 0x89, 0xe5,        // mov rbp, rsp
      0x48, 0xc7, 0xc3         // mov rbx, <hammercount>
    };

    // move aggressor address to register
    static const std::array<uint8_t, 2> mov_agg_addr = {
      0x48, 0xb8               // movabs rax
    };

    // access aggressor address
    static const std::array<uint8_t, 3> hammer_agg = {
      0x48, 0x8b, 0x08         // mov rcx, [rax]
    };

    static const uint8_t nop = 0x90;  // nop

    // flush aggressor address
    static const auto flush_agg = std::to_array<uint8_t>({
  #ifndef __CLFLUSHOPT__
        0x0f, 0xae, 0x38       // clflush [rax]
  #else
        0x66, 0x0f, 0xae, 0x38 // clflushopt [rax]
  #endif
    });

    // end of function mfence, decrement hammercount and
    // jump to start of loop if not zero
    static const std::array<uint8_t, 8> func_end_start = {
      0x0f, 0xae, 0xf0,        // mfence
      0x48, 0xff, 0xcb,        // dec rbx
      0x0f, 0x85               // jnz rel32 -> start of loop
    };

    // return from function
    static const std::array<uint8_t, 2> func_end_end = {
      0x5d,                    // pop rbp
      0xc3                     // ret
    };

  // clang-format on

  std::vector<uint8_t> function;
  function.insert(function.end(), function_start.begin(), function_start.end());
  auto hammercount_value =
      to_byte_array(static_cast<uint32_t>(config.hammer_count));
  function.insert(function.end(), hammercount_value.begin(),
                  hammercount_value.end());

  // access_nop_flush combines these 3 instructions and
  // adds the configured nop_count
  std::vector<uint8_t> access_nop_flush(hammer_agg.begin(), hammer_agg.end());
  access_nop_flush.insert(access_nop_flush.end(), config.nop_count, nop);
  access_nop_flush.insert(access_nop_flush.end(), flush_agg.begin(),
                          flush_agg.end());

  // aggressor address
  std::array<uint8_t, sizeof(virt_aggs[0])> agg_addr;

  // add access -> nop -> flush to function for all aggressors
  for (virtaddr_t agg : virt_aggs) {
    agg_addr = to_byte_array(agg);
    function.insert(function.end(), mov_agg_addr.begin(), mov_agg_addr.end());
    function.insert(function.end(), agg_addr.begin(), agg_addr.end());
    function.insert(function.end(), access_nop_flush.begin(),
                    access_nop_flush.end());
  }

  // the jump offset for to hammercount loop, jump back ... bytes
  std::array<uint8_t, sizeof(uint32_t)> function_jump_offset;

  // calculate jump offset
  const uint32_t loop_size =
      mov_agg_addr.size() + agg_addr.size() + access_nop_flush.size();
  const uint32_t func_end_till_offsetparameter_size =
      (func_end_start.size() + function_jump_offset.size()) - 1;
  const uint32_t jump_offset = std::numeric_limits<uint32_t>::max() -
                               ((loop_size * config.aggressor_rows) +
                                func_end_till_offsetparameter_size);

  function_jump_offset = to_byte_array(jump_offset);

  // add all instructions for end of function and add jump offset for
  // hammercount loop
  function.insert(function.end(), func_end_start.begin(), func_end_start.end());
  function.insert(function.end(), function_jump_offset.begin(),
                  function_jump_offset.end());
  function.insert(function.end(), func_end_end.begin(), func_end_end.end());

  // allocate executable space
  void *mem = mmap(nullptr, function.size(), PROT_WRITE | PROT_READ | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
  if (mem == MAP_FAILED) {
    log_error_and_exit("Failed to mmap the function!");
  }
  // copy function to executable space
  std::memcpy(mem, function.data(), function.size());
  fn = (void (*)())mem;
#endif // USE_ASMJIT

#ifdef MEASURE_HAMMER_DURATION
  using namespace std::chrono;
  auto begin = steady_clock::now();
#endif // MEASURE_HAMMER_DURATION

  // execute machine code
  fn();

#ifdef MEASURE_HAMMER_DURATION
  auto end = steady_clock::now();
  auto duration = duration_cast<milliseconds>(end - begin).count();
  log_info("Hammered for ", duration, " ms");
#endif // MEASURE_HAMMER_DURATION

#ifdef USE_ASMJIT
  rt.release(fn);
#else
  munmap(mem, function.size());
#endif // USE_ASMJIT

#ifdef MEASURE_HAMMER_DURATION
  return duration;
#endif // MEASURE_HAMMER_DURATION

  return 0;
}

/**
 * @brief    Default rowhammer implementation written in assembly.
 *
 * @return   uint64_t            The hammer duration in ms if
 *                               MEASURE_HAMMER_DURATION is defined,
 *                               otherwise 0.
 */
uint64_t BitFlipper::hammer_aggs_assembly() const {
#ifdef MEASURE_HAMMER_DURATION
  using namespace std::chrono;
  auto begin = steady_clock::now();
#endif // MEASURE_HAMMER_DURATION

  for (uint64_t i = 0; i < config.hammer_count; i++) {
// clang-format off
#ifndef __CLFLUSHOPT__
    // experiments lead to most bit flips when clflush was executed directly
    // after accessing the aggressor
    for (virtaddr_t agg : virt_aggs)
      asm volatile("movq (%[agg]), %%rax;"
                   "clflush (%[agg])"
                   :
                   : [ agg ] "r"(agg)
                   : "%rax", "memory");
#else
    // experiments lead to most bit flips when clflushopt was executed for every
    // aggressor after accessing all aggressors
    for (virtaddr_t agg : virt_aggs)
      asm volatile("movq (%[agg]), %%rax"
                   :
                   : [ agg ] "r"(agg)
                   : "%rax", "memory");
    for (virtaddr_t agg : virt_aggs)
      asm volatile("clflushopt (%[agg])"
                   :
                   : [ agg ] "r"(agg)
                   : "%rax", "memory");
#endif
    // clang-format on
  }

#ifdef MEASURE_HAMMER_DURATION
  auto end = steady_clock::now();
  auto duration = duration_cast<milliseconds>(end - begin).count();
  log_info("Hammered for ", duration, " ms");
  return duration;
#endif // MEASURE_HAMMER_DURATION

  return 0;
}

/**
 * @brief    Default rowhammer implementation.
 *
 * @return   uint64_t            The hammer duration in ms if
 *                               MEASURE_HAMMER_DURATION is defined,
 *                               otherwise 0.
 */
uint64_t BitFlipper::hammer_aggs_default() const {
#ifdef MEASURE_HAMMER_DURATION
  using namespace std::chrono;
  auto begin = steady_clock::now();
#endif // MEASURE_HAMMER_DURATION

  for (uint64_t i = 0; i < config.hammer_count; i++) {
    for (virtaddr_t agg : virt_aggs)
      *reinterpret_cast<volatile int *>(agg);
    for (virtaddr_t agg : virt_aggs)
      clflush(agg);
  }

#ifdef MEASURE_HAMMER_DURATION
  auto end = steady_clock::now();
  auto duration = duration_cast<milliseconds>(end - begin).count();
  log_info("Hammered for ", duration, " ms");
  return duration;
#endif // MEASURE_HAMMER_DURATION
  return 0;
}

/**
 * @brief    TRRespass's rowhammer implementation.
 *
 * Code used from the TRRespass fuzzer (written in C) with slight modifications
 * to make it more C++ like. (https://github.com/vusec/trrespass)
 *
 * @return   uint64_t            The hammer duration in ms if
 *                               MEASURE_HAMMER_DURATION is defined,
 *                               otherwise 0.
 */
uint64_t BitFlipper::hammer_aggs_trrespass() const {
  auto &v_lst = virt_aggs;

  sched_yield();
  if (config.threshold > 0) {
    uint64_t t0 = 0, t1 = 0;
    // Threshold value depends on your system
    while (abs(static_cast<int64_t>(t1) - static_cast<int64_t>(t0)) <
           config.threshold) {
      t0 = rdtscp();
      *reinterpret_cast<volatile char *>(v_lst[0]);
      clflush(v_lst[0]);
      t1 = rdtscp();
    }
  }

#ifdef MEASURE_HAMMER_DURATION
  uint64_t cl0, cl1;
  cl0 = realtime_now();
#endif

  for (uint64_t i = 0; i < config.hammer_count; i++) {
    mfence();
    for (std::size_t j = 0; j < v_lst.size(); j++) {
      *reinterpret_cast<volatile char *>(v_lst[j]);
    }
    for (std::size_t j = 0; j < v_lst.size(); j++) {
      clflush(v_lst[j]);
    }
  }

#ifdef MEASURE_HAMMER_DURATION
  cl1 = realtime_now();
  auto duration = (cl1 - cl0) / 1000000;
  log_info("Hammered for ", duration, " ms");
  return duration;
#endif // MEASURE_HAMMER_DURATION
  return 0;
}

#ifdef USE_ASMJIT
/**
 * @brief    Blacksmith's rowhammer implementation.
 *
 * Code used from the Blacksmith Fuzzer, with minor modifications to fit our
 * tool (https://github.com/comsec-group/blacksmith).
 *
 * The Blacksmith Fuzzer is a fuzzer for non-uniform rowhammer patterns.
 *
 * The Blacksmith Fuzzer uses the asmjit library to generate machine code, like
 * we did by hand at the machine code implementation. The special thing about
 * this code it syncs the hammering to the refresh window.
 *
 * @return   uint64_t            The hammer duration in ms if
 *                               MEASURE_HAMMER_DURATION is defined,
 *                               otherwise 0.
 */
uint64_t BitFlipper::hammer_aggs_blacksmith() const {

  // bringing the aggressors into hammering order
  std::vector<virtaddr_t> ordered_aggs;
  for (uint64_t i = 0; i < config.hammer_order.size(); i++) {
    ordered_aggs.push_back(virt_aggs[config.hammer_order[i] - 1]);
  }

  asmjit::JitRuntime rt;
  asmjit::CodeHolder code;
  code.init(rt.environment());
  asmjit::x86::Assembler a(&code);

  asmjit::Label for_begin = a.newLabel();
  asmjit::Label for_end = a.newLabel();
  asmjit::Label while1_begin = a.newLabel();
  asmjit::Label while1_end = a.newLabel();

  // part 1: synchronize with the beginning of an interval

  const auto NUM_TIMED_ACCESSES = config.num_aggs_for_sync;

  // warmup
  for (uint64_t idx = 0; idx < NUM_TIMED_ACCESSES; idx++) {
    a.mov(asmjit::x86::rax, static_cast<uint64_t>(ordered_aggs[idx]));
    a.mov(asmjit::x86::rbx, asmjit::x86::ptr(asmjit::x86::rax));
  }

  a.bind(while1_begin);
  // clflushopt addresses involved in sync
  for (uint64_t idx = 0; idx < NUM_TIMED_ACCESSES; idx++) {
    a.mov(asmjit::x86::rax, static_cast<uint64_t>(ordered_aggs[idx]));
#ifndef __CLFLUSHOPF__
    a.clflush(asmjit::x86::ptr(asmjit::x86::rax));
    a.lfence();
#else
    a.clflushopt(asmjit::x86::ptr(asmjit::x86::rax));
#endif
  }
  a.mfence();

  // retrieve timestamp
  a.rdtscp(); // result of rdtscp is in [edx:eax]
  a.lfence();
  a.mov(asmjit::x86::ebx, asmjit::x86::eax); // discard upper 32 bits, store
                                             // lower 32b in ebx for later

  // use first NUM_TIMED_ACCESSES addresses for sync
  for (uint64_t idx = 0; idx < NUM_TIMED_ACCESSES; idx++) {
    a.mov(asmjit::x86::rax, static_cast<uint64_t>(ordered_aggs[idx]));
    a.mov(asmjit::x86::rcx, asmjit::x86::ptr(asmjit::x86::rax));
  }

  // a.mfence(); // Blacksmith patch evaluate
  // a.lfence(); // Blacksmith patch evaluate

  // if ((after - before) > 1000) break;
  a.rdtscp(); // result: edx:eax
  // a.lfence(); // Blacksmith patch evaluate
  a.sub(asmjit::x86::eax, asmjit::x86::ebx);
  a.cmp(asmjit::x86::eax, static_cast<uint64_t>(1000));

  // depending on the cmp's outcome, jump out of loop or to the loop's beginning
  a.jg(while1_end);
  a.jmp(while1_begin);
  a.bind(while1_end);

  // part 2: perform hammering

  a.mov(asmjit::x86::rsi, config.total_num_activations);
  a.mov(asmjit::x86::edx, 0); // num activations counter

  a.bind(for_begin);
  a.cmp(asmjit::x86::rsi, 0);
  a.jle(for_end);

  std::unordered_map<uint64_t, bool> accessed_before;

  size_t cnt_total_activations = 0;

  // hammer each aggressor once
  for (uint64_t i = NUM_TIMED_ACCESSES;
       i < static_cast<int>(ordered_aggs.size()) - NUM_TIMED_ACCESSES; i++) {
    auto cur_addr = static_cast<uint64_t>(ordered_aggs[i]);

    if (accessed_before[cur_addr]) {
      // flush
      if (config.flushing == "latest_possible") {
        a.mov(asmjit::x86::rax, cur_addr);
#ifndef __CLFLUSHOPT__
        a.clflush(asmjit::x86::ptr(asmjit::x86::rax));
        a.lfence();
#else
        a.clflushopt(asmjit::x86::ptr(asmjit::x86::rax));
#endif
        accessed_before[cur_addr] = false;
      }
      // fence to ensure flushing finished and defined order of aggressors is
      // guaranteed
      if (config.fencing == "latest_possible") {
        a.mfence();
        accessed_before[cur_addr] = false;
      }
    }

    // hammer
    a.mov(asmjit::x86::rax, cur_addr);
    a.mov(asmjit::x86::rcx, asmjit::x86::ptr(asmjit::x86::rax));
    accessed_before[cur_addr] = true;
    a.dec(asmjit::x86::rsi);
    cnt_total_activations++;

    // flush
    if (config.flushing == "earliest_possible") {
      a.mov(asmjit::x86::rax, cur_addr);
#ifndef __CLFLUSHOPT__
      a.clflush(asmjit::x86::ptr(asmjit::x86::rax));
      a.lfence();
#else
      a.clflushopt(asmjit::x86::ptr(asmjit::x86::rax));
#endif
    }
  }

  // fences -> ensure that aggressors are not interleaved, i.e., we access
  // aggressors always in same order
  a.mfence();

  // part 3: synchronize with the end

  std::vector<virtaddr_t> last_aggs(ordered_aggs.end() - NUM_TIMED_ACCESSES,
                                    ordered_aggs.end());
  sync_ref(last_aggs, a);

  a.jmp(for_begin);
  a.bind(for_end);

  // now move our counter for no. of activations in the end of interval sync.
  // to the 1st output register %eax
  a.mov(asmjit::x86::eax, asmjit::x86::edx);
  a.ret();

  int (*fn)() = nullptr;

  asmjit::Error err = rt.add(&fn, &code);
  if (err)
    throw std::runtime_error(
        "[-] Error occurred while jitting code. Aborting execution!");

#ifdef MEASURE_HAMMER_DURATION
  using namespace std::chrono;
  auto begin = steady_clock::now();
#endif // MEASURE_HAMMER_DURATION

  fn();

#ifdef MEASURE_HAMMER_DURATION
  auto end = steady_clock::now();
  auto duration = duration_cast<milliseconds>(end - begin).count();
  log_info("Hammered for ", duration, " ms");
  return duration;
#endif // MEASURE_HAMMER_DURATION

  rt.release(fn);

#ifdef MEASURE_HAMMER_DURATION
  return duration;
#endif // MEASURE_HAMMER_DURATION
  return 0;
}
#endif // USE_ASMJIT

#ifdef USE_ASMJIT
/**
 * @brief    Synchronize memory access to memory refresh.
 *
 * Code used from the Blacksmith Fuzzer, with minor modifications.
 * https://github.com/comsec-group/blacksmith
 *
 * The code accesses the aggressors till somewhat like a hiccup happens. This
 * hiccup should be an executed refresh. A hiccup is recognized, if the access
 * to the aggressors suddenly takes more than 1000 clock cycles.
 *
 * @param    aggressor_pairs     The aggressors used for syncing.
 * @param    assembler           The used (asmjit) Assembler.
 */
void BitFlipper::sync_ref(const std::vector<virtaddr_t> &aggressor_pairs,
                          asmjit::x86::Assembler &assembler) const {
  asmjit::Label wbegin = assembler.newLabel();
  asmjit::Label wend = assembler.newLabel();

  assembler.bind(wbegin);

  // assembler.push(asmjit::x86::edx); // Blacksmith patch evaluate

  assembler.mfence();
  assembler.lfence();

  assembler.push(asmjit::x86::edx); // Blacksmith patch evaluate (delete)
  assembler.rdtscp();               // result of rdtscp is in [edx:eax]
  // discard upper 32 bits and store lower 32 bits in ebx to compare later
  // assembler.lfence(); // Blacksmith patch evaluate
  assembler.mov(asmjit::x86::ebx, asmjit::x86::eax);
  assembler.lfence(); // Blacksmith patch evaluate (delete)
  assembler.pop(asmjit::x86::edx);

  for (auto agg : aggressor_pairs) {
    // flush
    assembler.mov(asmjit::x86::rax, static_cast<uint64_t>(agg));
#ifndef __CLFLUSHOPT__
    assembler.clflush(asmjit::x86::ptr(asmjit::x86::rax));
    assembler.lfence();
#else
    assembler.clflushopt(asmjit::x86::ptr(asmjit::x86::rax));
#endif

    // access
    assembler.mov(asmjit::x86::rax, static_cast<uint64_t>(agg));
    assembler.mov(asmjit::x86::rcx, asmjit::x86::ptr(asmjit::x86::rax));

    // we do not deduct the sync aggressors from the total number of
    // activations because the number of sync activations varies for different
    // patterns; if we deduct it from the total number of activations, we
    // cannot ensure anymore that we are hammering long enough/as many times
    // as needed to trigger bit flips
    //    assembler.dec(asmjit::x86::rsi);

    // update counter that counts the number of activation in the trailing
    // synchronization
    assembler.inc(asmjit::x86::edx);
  }

  assembler.push(asmjit::x86::edx);
  // assembler.mfence(); // Blacksmith patch evaluate
  // assembler.lfence(); // Blacksmith patch evaluate
  assembler.rdtscp(); // result: edx:eax
  assembler.lfence();
  assembler.pop(asmjit::x86::edx);

  // if ((after - before) > 1000) break;
  assembler.sub(asmjit::x86::eax, asmjit::x86::ebx);
  assembler.cmp(asmjit::x86::eax, static_cast<uint64_t>(1000));

  // depending on the cmp's outcome...
  assembler.jg(wend);    // ... jump out of the loop
  assembler.jmp(wbegin); // ... or jump back to the loop's beginning
  assembler.bind(wend);
}
#endif // USE_ASMJIT

/**
 * @brief    Checks if the pages for the victim and aggressor rows can be found.
 *
 * @param    finder              The physical page finder to use.
 * @return   true                If all needed pages can be found.
 * @return   false               If some pages are missing.
 */
bool BitFlipper::find_pages(const PhysPageFinder &finder) {
  bool found = true;
  for (std::size_t i = 0; i < phys.aggs.size(); ++i)
    found &= finder.find_page(phys.aggs[i], virt_aggs[i]);
  for (std::size_t i = 0; i < phys.victims.size(); ++i)
    found &= finder.find_page(phys.victims[i], virt_victims[i]);
  return found;
}

/**
 * @brief    Initializes the victim and aggressor rows, executes the hammering
 *           process and checks for bit flips.
 *
 * @param    victim_init         The initialization bits for the victim rows.
 * @param    aggressor_init      The initialization bits for the aggressor rows.
 * @return   true                If bit flips occurred.
 * @return   false               If no bit flips occurred.
 */
bool BitFlipper::hammer_and_check(uint64_t victim_init,
                                  uint64_t aggressor_init) {
  // initialize victim rows
  for (const auto &victim : virt_victims) {
    for (virtaddr_t addr = victim; addr < victim + row_size;
         addr += sizeof(uint64_t)) {
      *reinterpret_cast<uint64_t *>(addr) = victim_init;
      // flush so that the later check does not just return cached data
      clflush(addr);
    }
  }

  // initialize aggressor rows
  for (const auto &aggr : virt_aggs) {
    for (virtaddr_t addr = aggr; addr < aggr + row_size;
         addr += sizeof(uint64_t)) {
      *reinterpret_cast<uint64_t *>(addr) = aggressor_init;
      // flush so that the later check does not just return cached data
      clflush(addr);
    }
  }

  // hammer
  [[maybe_unused]] uint64_t hammer_time = hammer_aggs();

#if USE_DB
  int64_t actual_temp = 0;

  if (!config.target_temps.empty()) {
    actual_temp = temperature_controller.get_actual_temperature();
    int64_t target_temp = temperature_controller.get_target_temperature();
    if (actual_temp <= target_temp - static_cast<int64_t>(config.interval) ||
        actual_temp >= target_temp + static_cast<int64_t>(config.interval))
      log_error_and_exit("Temperature outside of given interval: expected ",
                         target_temp, ", got ", actual_temp);
    log_info("Current temperature: ", actual_temp, " °C");
  }

  log_trace("Inserted test with ID ",
            db->insert_test(phys.aggs, hammer_time, victim_init, aggressor_init,
                            actual_temp));
#endif

  // check for bit flips
  uint64_t bitflips = 0;
  const char prev_fill = std::cout.fill('0');

  for (std::size_t v = 0; v < phys.victims.size(); v++) {
    virtaddr_t victim = virt_victims[v];
    uint64_t *victim_end = reinterpret_cast<uint64_t *>(
        reinterpret_cast<uint64_t>(virt_victims[v]) + row_size);

    for (uint64_t *addr = reinterpret_cast<uint64_t *>(victim);
         addr < victim_end; addr++) {
      uint64_t val = *addr;

      if (val == victim_init)
        continue;

      flip_offset_bytes = reinterpret_cast<virtaddr_t>(addr) - victim;

      for (uint8_t bit = 0; bit < 64; bit++) {
        const auto flips_to = (val >> bit) & 1;

        if (((victim_init >> bit) & 1) == flips_to)
          continue;

        const auto victim_byte = bit / 8;
        const auto victim_phys =
            phys.victims[v] + flip_offset_bytes + victim_byte;
        const auto victim_bit = bit % 8;

        log_info_flip(std::noshowbase, "Flip at 0x", std::hex, victim_phys,
                      std::dec, " ", DRAMAddr(victim_phys), ": 0x", std::hex,
                      std::setw(2), ((victim_init >> victim_byte * 8) & 0xff),
                      " -> 0x", std::setw(2), ((val >> victim_byte * 8) & 0xff),
                      std::dec, " (bit ", (unsigned)victim_bit, " flipped to ",
                      (unsigned)flips_to, ")");
#ifdef USE_DB
        db->insert_bitflip(victim_phys, victim_bit, flips_to);
#endif
        ++bitflips;
      }
    }
  }

  log_info("Found ", bitflips, " bit flip(s)");
  std::cout.fill(prev_fill);

  // debug information if there is an implausible number of bit flips
  if (bitflips >= page_size * 8) {
    log_warn("Very high number of bit flips detected");
    std::cout << std::hex;
    log_debug("victim_init: ", victim_init,
              ", aggressor_init: ", aggressor_init);
    log_debug("victims: ", virt_victims);
    log_debug("aggressors: ", virt_aggs);

    for (const auto victim : virt_victims) {
      std::cout << "victim " << victim << ": ";
      for (uint64_t i = 0; i < row_size / sizeof(uint64_t); ++i)
        std::cout << reinterpret_cast<uint64_t *>(victim)[i] << ", ";
      std::cout << std::endl;
    }

    for (const auto agg : virt_aggs) {
      std::cout << "aggressor " << agg << ": ";
      for (uint64_t i = 0; i < row_size / sizeof(uint64_t); ++i)
        std::cout << reinterpret_cast<uint64_t *>(agg)[i] << ", ";
      std::cout << std::endl;
    }

    std::cout << std::dec;
  }

  return bitflips > 0;
}

/**
 * @brief    Invokes the hammering process.
 *
 * @return   true                If bit flips occurred.
 * @return   false               If no bit flips occurred.
 */
bool BitFlipper::hammer() {
#ifdef USE_DB
  db->begin_transaction();
#endif

  // Test both 0->1 and 1->0 bit flips.
  bool seen_flip = false;
  for (std::size_t i = 0; i < config.aggressor_init.size(); ++i) {
    seen_flip |=
        hammer_and_check(config.victim_init[i], config.aggressor_init[i]);
  }

#ifdef USE_DB
  db->commit();
#endif

  return seen_flip;
}
