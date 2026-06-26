/** \file
 *
 *  \brief Motorola MC6809 CPU.
 *
 *  \copyright Copyright 2003-2026 Ciaran Anscomb
 *
 *  \licenseblock This file is part of XRoar, a Dragon/Tandy CoCo emulator.
 *
 *  XRoar is free software; you can redistribute it and/or modify it under the
 *  terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later
 *  version.
 *
 *  See COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 */

#ifndef XROAR_MC6809_MC6809_H_
#define XROAR_MC6809_MC6809_H_

#include <stdint.h>

#include "delegate.h"
#include "pl-endian.h"

#include "debug.h"
#include "part.h"

struct ser_struct_data;

#ifdef TRACE
struct mc6809_trace;
// Actual maximum (with page chain pruning) is 5 bytes of trace data - but
// allow a few more just in case:
#define MC6809_MAX_TRACE_BYTES (8)
#endif

#define MC6809_INT_VEC_RESET (0xfffe)
#define MC6809_INT_VEC_NMI (0xfffc)
#define MC6809_INT_VEC_SWI (0xfffa)
#define MC6809_INT_VEC_IRQ (0xfff8)
#define MC6809_INT_VEC_FIRQ (0xfff6)
#define MC6809_INT_VEC_SWI2 (0xfff4)
#define MC6809_INT_VEC_SWI3 (0xfff2)

// MPU state.  Represents current position in the high-level flow chart from
// the data sheet (figure 14).
//
// The values here are important, as they appear in snapshots.  We can only
// remove one if we can prove it will never have ended up in a snapshot!

enum mc6809_state {
	mc6809_state_label_a            = 0,
	mc6809_state_sync               = 1,
	mc6809_state_dispatch_irq       = 2,
	mc6809_state_label_b            = 3,
	mc6809_state_reset              = 4,
	mc6809_state_reset_check_halt   = 5,
	mc6809_state_next_instruction   = 6,
	mc6809_state_instruction_page_2 = 7,
	mc6809_state_instruction_page_3 = 8,
	mc6809_state_cwai_check_halt    = 9,
	mc6809_state_sync_check_halt    = 10,
	mc6809_state_done_instruction   = 11,
	mc6809_state_hcf                = 12,
	mc6809_state_irq_reset_vector   = 13,  // BA=0, BS=1
};

/* Interface shared with all 6809-compatible CPUs */
struct MC6809 {
	// Is a part
	struct part part;

	/* Interrupt lines */
	bool halt, nmi, firq, irq;
	uint8_t D;

	/* Methods */

	void (*reset)(struct MC6809 *cpu);
	void (*run)(struct MC6809 *cpu);

	/* External handlers */

	/* Memory access cycle */
	DELEGATE_T2(void, bool, uint16) mem_cycle;

	/* Internal state */

	unsigned state;
	bool running;
	uint16_t page;  // 0, 0x200, or 0x300
#ifdef TRACE
	struct mc6809_trace *tracer;
	uint16_t trace_pc;  // base address of instruction
	uint16_t trace_next_pc;  // next address in instruction
	unsigned trace_nbytes;
	uint8_t trace_bytes[MC6809_MAX_TRACE_BYTES];
#endif

	// Called before instruction is fetched
	DELEGATE_T1(void, uint32) instruction_hook;
	// Called after instruction is executed
	DELEGATE_T0(void) instruction_posthook;

	/* Registers */
	uint8_t reg_cc, reg_dp;
	uint16_t reg_d;
	uint16_t reg_x, reg_y, reg_u, reg_s, reg_pc;
	/* Interrupts */
	bool nmi_armed;
	bool nmi_latch, firq_latch, irq_latch;
	bool nmi_active, firq_active, irq_active;

	struct {
		struct debug_cpu cpu;
		struct debug_part part;
	} debug;
};

#if __BYTE_ORDER == __BIG_ENDIAN
# define MC6809_REG_HI (0)
# define MC6809_REG_LO (1)
#else
# define MC6809_REG_HI (1)
# define MC6809_REG_LO (0)
#endif

#define MC6809_REG_A(cpu) (*((uint8_t *)&cpu->reg_d + MC6809_REG_HI))
#define MC6809_REG_B(cpu) (*((uint8_t *)&cpu->reg_d + MC6809_REG_LO))

extern const struct ser_struct_data mc6809_ser_struct_data;

inline void MC6809_HALT_SET(struct MC6809 *cpu, bool val) {
	cpu->halt = val;
}

inline void MC6809_NMI_SET(struct MC6809 *cpu, bool val) {
	cpu->nmi = val;
}

inline void MC6809_FIRQ_SET(struct MC6809 *cpu, bool val) {
	cpu->firq = val;
}

inline void MC6809_IRQ_SET(struct MC6809 *cpu, bool val) {
	cpu->irq = val;
}

// Used by MC6809-compatibles:
bool mc6809_is_a(struct part *p, const char *name);
void mc6809_init_debug_info(struct MC6809 *cpu);
extern const struct debug_feature m6809_core_feature;
bool mc6809_get_flag(void *sptr, int n);
void mc6809_set_flag(void *sptr, int n, bool v);

#endif
