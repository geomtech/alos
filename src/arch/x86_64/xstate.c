/* Etat complet des composantes x87/SSE et AVX activees par XCR0. */
#include "xstate.h"
#include "cpu.h"
#include "../../mm/pmm.h"
#include "../../include/string.h"
#include "../../kernel/klog.h"

static uint8_t initial_state[4096] __attribute__((aligned(64)));
static uint64_t state_mask;
static uint32_t state_size = 512;
static int use_xsave;

void xstate_save(void *state) {
  if (use_xsave) {
    __asm__ volatile("xsave64 (%0)" :: "r"(state),
                      "a"((uint32_t)state_mask),
                      "d"((uint32_t)(state_mask >> 32)) : "memory");
  } else {
    __asm__ volatile("fxsave64 (%0)" :: "r"(state) : "memory");
  }
}

void xstate_restore(const void *state) {
  if (use_xsave) {
    __asm__ volatile("xrstor64 (%0)" :: "r"(state),
                      "a"((uint32_t)state_mask),
                      "d"((uint32_t)(state_mask >> 32)) : "memory");
  } else {
    __asm__ volatile("fxrstor64 (%0)" :: "r"(state) : "memory");
  }
}

void xstate_init(void) {
  uint32_t a, b, c, d;
  cpuid(1, &a, &b, &c, &d);
  if (c & (1U << 26)) {
    uint32_t features = c;
    cpuid(0xD, &a, &b, &c, &d);
    if ((a & 3) == 3) {
      state_mask = 3;
      if ((features & (1U << 28)) && (a & 4)) state_mask |= 4;
      write_cr4(read_cr4() | (1ULL << 18));
      __asm__ volatile("xsetbv" :: "c"(0), "a"((uint32_t)state_mask),
                        "d"(0) : "memory");
      cpuid(0xD, &a, &b, &c, &d);
      state_size = b;
      if (state_size > sizeof(initial_state)) {
        KLOG_ERROR("CPU", "Enabled XSAVE state exceeds buffer");
        for (;;) __asm__ volatile("cli; hlt");
      }
      use_xsave = 1;
    }
  }
  memset(initial_state, 0, sizeof(initial_state));
  /* Etat architectural initial, y compris MXCSR, sans fuite du bootloader. */
  *(uint16_t *)(initial_state + 0) = 0x037F;
  *(uint32_t *)(initial_state + 24) = 0x1F80;
  xstate_restore(initial_state);
  KLOG_INFO("CPU", use_xsave ? "Thread state: XSAVE/XRSTOR" :
                              "Thread state: FXSAVE/FXRSTOR");
  KLOG_INFO_DEC("CPU", "Thread state bytes: ", state_size);
  KLOG_INFO_HEX("CPU", "XCR0 enabled mask: ", (uint32_t)state_mask);
}

void *xstate_create(void) {
  void *state = pmm_alloc_block();
  if (state) xstate_reset(state);
  return state;
}

void xstate_reset(void *state) {
  memcpy(state, initial_state, sizeof(initial_state));
}

void xstate_destroy(void *state) {
  if (state) pmm_free_block(state);
}
