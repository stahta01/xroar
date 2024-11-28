/** \file
 *
 *  \brief Hitach HD6309 CPU.
 *
 *  \copyright Copyright 2012-2024 Ciaran Anscomb
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
 *
 *  A drop-in replacement for the MC6809, this Hitachi CMOS version of the CPU
 *  includes many instruction set enhancements.
 *
 *  \par Sources
 *
 *  - MC6809E data sheet, Motorola
 *
 *  - HD6309E data sheet, Hitachi
 *
 *  - MC6809 Cycle-By-Cycle Performance,
 *    http://atjs.great-site.net/mc6809/Information/6809cyc.txt
 *
 *  - Motorola 6809 and Hitachi 6309 Programmers Reference,
 *    2009 Darren Atkinson
 *
 *  - Undocumented 6309 Behaviours, David Banks [hoglet67]
 *    https://github.com/hoglet67/6809Decoder/wiki/Undocumented-6309-Behaviours
 *
 *  - Tim Lindner's Hitachi 6309 Fuzzing Project
 *    https://github.com/tlindner/Fuzz6309
 */

#include "top-config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"

#include "hd6309.h"
#include "logging.h"
#include "mc6809.h"
#include "part.h"
#include "serialise.h"

#ifdef TRACE
// Tracing supported:
#include "hd6309_trace.h"
#define HD6309_TRACE_VECTOR(c) if (UNLIKELY(logging.trace_cpu)) { hd6309_trace_vector((c)->tracer); }
#define HD6309_TRACE_INSTRUCTION(c) if (UNLIKELY(logging.trace_cpu)) { hd6309_trace_instruction((c)->tracer); }
#else
// Tracing not supported - no-op macros:
#define HD6309_TRACE_VECTOR(c)
#define HD6309_TRACE_INSTRUCTION(c)
#endif

#define HD6309_SER_TFM_SRC  (6)
#define HD6309_SER_TFM_DEST (7)

static const struct ser_struct ser_struct_hd6309[] = {
	SER_ID_STRUCT_NEST(1,  &mc6809_ser_struct_data), // 1

	SER_ID_STRUCT_ELEM(2,  struct HD6309, state),
	SER_ID_STRUCT_ELEM(3,  struct HD6309, reg_w),
	SER_ID_STRUCT_ELEM(4,  struct HD6309, reg_md),
	SER_ID_STRUCT_ELEM(5,  struct HD6309, reg_v),
	SER_ID_STRUCT_ELEM(8,  struct HD6309, reg_m),  // replaces tfm_data

	SER_ID_STRUCT_UNHANDLED(HD6309_SER_TFM_SRC),
	SER_ID_STRUCT_UNHANDLED(HD6309_SER_TFM_DEST),
	SER_ID_STRUCT_ELEM(9,  struct HD6309, tfm_src_mod),
	SER_ID_STRUCT_ELEM(10, struct HD6309, tfm_dest_mod),
};

static _Bool hd6309_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool hd6309_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data hd6309_ser_struct_data = {
	.elems = ser_struct_hd6309,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_hd6309),
	.read_elem = hd6309_read_elem,
	.write_elem = hd6309_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * External interface
 */

static void hd6309_set_pc(void *sptr, unsigned pc);

static void hd6309_reset(struct MC6809 *cpu);
static void hd6309_run(struct MC6809 *cpu);

/*
 * Data reading & writing
 */

/* Wrap common fetches */

static uint8_t fetch_byte(struct MC6809 *cpu, uint16_t a);
static uint16_t fetch_word(struct MC6809 *cpu, uint16_t a);

/* Compute effective address */

static uint16_t ea_direct(struct MC6809 *cpu);
static uint16_t ea_extended(struct MC6809 *cpu);
static uint16_t ea_indexed(struct MC6809 *cpu);

/*
 * Interrupt handling
 */

static void push_irq_registers(struct MC6809 *cpu);
static void push_firq_registers(struct MC6809 *cpu);
static void stack_irq_registers(struct MC6809 *cpu, _Bool entire);
static void take_interrupt(struct MC6809 *cpu, uint8_t mask, uint16_t vec);
static void instruction_posthook(struct MC6809 *cpu);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Register handling macros
 */

#define REG_CC (cpu->reg_cc)
#define REG_A (MC6809_REG_A(cpu))
#define REG_B (MC6809_REG_B(cpu))
#define REG_D (cpu->reg_d)
#define REG_DP (cpu->reg_dp)
#define REG_X (cpu->reg_x)
#define REG_Y (cpu->reg_y)
#define REG_U (cpu->reg_u)
#define REG_S (cpu->reg_s)
#define REG_PC (cpu->reg_pc)
#define REG_E (HD6309_REG_E(hcpu))
#define REG_F (HD6309_REG_F(hcpu))
#define REG_W (hcpu->reg_w)
#define REG_V (hcpu->reg_v)
#define REG_MD (hcpu->reg_md)

// 'M' is a pseudo-register used in internal calculations that can
// be inspected with:
//     LDX #0
//     ADDR DP,X
// [hoglet67]
#define REG_M (hcpu->reg_m)

// Read Q
#define RREG_Q ((REG_D << 16) | REG_W)

/* Condition code register macros */

#define CC_E (0x80)
#define CC_F (0x40)
#define CC_H (0x20)
#define CC_I (0x10)
#define CC_N (0x08)
#define CC_Z (0x04)
#define CC_V (0x02)
#define CC_C (0x01)

// Common operations

#define STRUCT_CPU struct MC6809

#include "mc6809_common.c"
#include "mc680x/mc680x_ops.c"

/* Mode register macros */

#define MD_D0 (0x80)
#define MD_IL (0x40)
#define MD_FM (0x02)
#define MD_NM (0x01)

#define NATIVE_MODE (REG_MD & MD_NM)
#define FIRQ_STACK_ALL (REG_MD & MD_FM)

/* ------------------------------------------------------------------------- */

static uint16_t *tfm_reg_to_ptr(struct HD6309 *hcpu, unsigned reg) {
	struct MC6809 *cpu = &hcpu->mc6809;
	switch (reg) {
	case 0: return &REG_D;
	case 1: return &REG_X;
	case 2: return &REG_Y;
	case 3: return &REG_U;
	case 4: return &REG_S;
	default: return NULL;
	}
}

static unsigned tfm_ptr_to_reg(struct HD6309 *hcpu, uint16_t *ptr) {
	struct MC6809 *cpu = &hcpu->mc6809;
	if (ptr == &cpu->reg_d)
		return 0;
	if (ptr == &cpu->reg_x)
		return 1;
	if (ptr == &cpu->reg_y)
		return 2;
	if (ptr == &cpu->reg_u)
		return 3;
	if (ptr == &cpu->reg_s)
		return 4;
	return 15;
}

/* ------------------------------------------------------------------------- */

// HD6309 part creation

static struct part *hd6309_allocate(void);
static void hd6309_initialise(struct part *p, void *options);
static void hd6309_free(struct part *p);

static _Bool hd6309_is_a(struct part *p, const char *name);

static const struct partdb_entry_funcs hd6309_funcs = {
        .allocate = hd6309_allocate,
        .initialise = hd6309_initialise,
        .free = hd6309_free,

        .ser_struct_data = &hd6309_ser_struct_data,

        .is_a = hd6309_is_a,
};

const struct partdb_entry hd6309_part = { .name = "HD6309", .description = "Hitachi | HD6309E CPU", .funcs = &hd6309_funcs };

static struct part *hd6309_allocate(void) {
	struct HD6309 *hcpu = part_new(sizeof(*hcpu));
	struct MC6809 *cpu = &hcpu->mc6809;
	struct part *p = &cpu->debug_cpu.part;

	*hcpu = (struct HD6309){0};

	cpu->debug_cpu.get_pc = DELEGATE_AS0(unsigned, mc6809_get_pc, cpu);
	cpu->debug_cpu.set_pc = DELEGATE_AS1(void, unsigned, hd6309_set_pc, cpu);

	cpu->reset = hd6309_reset;
	cpu->run = hd6309_run;
	cpu->mem_cycle = DELEGATE_DEFAULT2(void, bool, uint16);

	// Tested: on power on, all registers have random-ish values, although
	// it definitely seems to err towards bits being set in my environment.
	// I've seen all bits of CC set on power up *except* E, but no bits set
	// is valid, so I'll leave CC clear.
	//
	// CC has F and I set as part of reset.  DP is explicitly cleared on
	// reset, but other registers are left untouched.
	//
	// Not tested yet: extra 6309 register initialisation.

	REG_D = 0xffff;
	REG_X = 0xffff;
	REG_Y = 0xffff;
	REG_U = 0xffff;
	REG_S = 0xffff;
	REG_W = 0xffff;
	REG_V = 0xffff;

#ifdef TRACE
	// Tracing
	hcpu->tracer = hd6309_trace_new(hcpu);
#endif

	return p;
}

static void hd6309_initialise(struct part *p, void *options) {
	(void)options;
	struct HD6309 *hcpu = (struct HD6309 *)p;
	struct MC6809 *cpu = &hcpu->mc6809;
	hd6309_reset(cpu);
}

static void hd6309_free(struct part *p) {
	(void)p;
#ifdef TRACE
	struct HD6309 *hcpu = (struct HD6309 *)p;
	if (hcpu->tracer) {
		hd6309_trace_free(hcpu->tracer);
	}
#endif
}

static _Bool hd6309_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct HD6309 *hcpu = sptr;
	switch (tag) {
	case HD6309_SER_TFM_SRC:
		hcpu->tfm_src = tfm_reg_to_ptr(hcpu, ser_read_vuint32(sh));
		break;
	case HD6309_SER_TFM_DEST:
		hcpu->tfm_dest = tfm_reg_to_ptr(hcpu, ser_read_vuint32(sh));
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool hd6309_write_elem(void *sptr, struct ser_handle *sh, int tag) {
        struct HD6309 *hcpu = sptr;
	switch (tag) {
	case HD6309_SER_TFM_SRC:
		ser_write_vuint32(sh, tag, tfm_ptr_to_reg(hcpu, hcpu->tfm_src));
		break;
	case HD6309_SER_TFM_DEST:
		ser_write_vuint32(sh, tag, tfm_ptr_to_reg(hcpu, hcpu->tfm_dest));
		break;
	default:
		return 0;
	}
        return 1;
}

static _Bool hd6309_is_a(struct part *p, const char *name) {
	return strcmp(name, "MC6809") == 0 || mc6809_is_a(p, name);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void hd6309_reset(struct MC6809 *cpu) {
	struct HD6309 *hcpu = (struct HD6309 *)cpu;
	cpu->halt = 0;
	cpu->nmi_armed = 0;
	cpu->nmi = cpu->nmi_latch = cpu->nmi_active = 0;
	cpu->firq = cpu->firq_latch = cpu->firq_active = 0;
	cpu->irq = cpu->irq_latch = cpu->irq_active = 0;
	hcpu->state = hd6309_state_reset;
}

/* Run CPU while cpu->running is true. */

static void hd6309_run(struct MC6809 *cpu) {
	struct HD6309 *hcpu = (struct HD6309 *)cpu;

	do {

		switch (hcpu->state) {

		case hd6309_state_reset:
			REG_MD = 0;
			REG_DP = 0;
			REG_CC |= (CC_F | CC_I);
			cpu->nmi_armed = 0;
			cpu->nmi = cpu->nmi_active = 0;
			cpu->firq_active = 0;
			cpu->irq_active = 0;
			hcpu->state = hd6309_state_reset_check_halt;
			HD6309_TRACE_VECTOR(hcpu);
			// fall through

		case hd6309_state_reset_check_halt:
			if (!cpu->halt) {
				take_interrupt(cpu, 0, MC6809_INT_VEC_RESET);
			} else {
				NVMA_CYCLE;
			}
			continue;

		// done_instruction case for backwards-compatibility
		case hd6309_state_done_instruction:
		case hd6309_state_label_a:
			if (cpu->halt) {
				NVMA_CYCLE;
				continue;
			}
			hcpu->state = hd6309_state_label_b;
			// fall through

		case hd6309_state_label_b:
			if (UNLIKELY(cpu->nmi_active)) {
				HD6309_TRACE_VECTOR(hcpu);
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu, 1);
				hcpu->state = hd6309_state_dispatch_irq;
			} else if (UNLIKELY(!(REG_CC & CC_F) && cpu->firq_active)) {
				HD6309_TRACE_VECTOR(hcpu);
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu, FIRQ_STACK_ALL);
				hcpu->state = hd6309_state_dispatch_irq;
			} else if (UNLIKELY(!(REG_CC & CC_I) && cpu->irq_active)) {
				HD6309_TRACE_VECTOR(hcpu);
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu, 1);
				hcpu->state = hd6309_state_dispatch_irq;
			} else {
				HD6309_TRACE_INSTRUCTION(hcpu);
				hcpu->state = hd6309_state_next_instruction;
				cpu->page = 0;
				// Instruction fetch hook called here so that machine
				// can be stopped beforehand.
				DELEGATE_SAFE_CALL(cpu->debug_cpu.instruction_hook);
			}
			continue;

		case hd6309_state_dispatch_irq:
			if (cpu->nmi_active) {
				cpu->nmi_active = cpu->nmi = cpu->nmi_latch = 0;
				take_interrupt(cpu, CC_F|CC_I, MC6809_INT_VEC_NMI);
			} else if (!(REG_CC & CC_F) && cpu->firq_active) {
				take_interrupt(cpu, CC_F|CC_I, MC6809_INT_VEC_FIRQ);
			} else if (!(REG_CC & CC_I) && cpu->irq_active) {
				take_interrupt(cpu, CC_I, MC6809_INT_VEC_IRQ);
			} else {
				hcpu->state = hd6309_state_cwai_check_halt;
			}
			continue;

		case hd6309_state_cwai_check_halt:
			cpu->nmi_active = cpu->nmi_latch;
			cpu->firq_active = cpu->firq_latch;
			cpu->irq_active = cpu->irq_latch;
			NVMA_CYCLE;
			if (!cpu->halt) {
				hcpu->state = hd6309_state_dispatch_irq;
			}
			continue;

		case hd6309_state_sync:
			if (cpu->nmi_active || cpu->firq_active || cpu->irq_active) {
				NVMA_CYCLE;
				instruction_posthook(cpu);
				hcpu->state = hd6309_state_label_b;
				continue;
			}
			cpu->nmi_active = cpu->nmi_latch;
			cpu->firq_active = cpu->firq_latch;
			cpu->irq_active = cpu->irq_latch;
			NVMA_CYCLE;
			if (cpu->halt) {
				hcpu->state = hd6309_state_sync_check_halt;
			}
			continue;

		case hd6309_state_sync_check_halt:
			NVMA_CYCLE;
			if (!cpu->halt) {
				hcpu->state = hd6309_state_sync;
			}
			continue;

		case hd6309_state_tfm:
			// Order is read, NVMA, write.  XXX but is it?  Should
			// check that and flag this as verified, because it
			// would make more sense to do read, write, then NVMA
			// (while it updates pointers which are all
			// post-applied).  For sure though, the instruction is
			// interruptable between the read and the write.
			if (REG_W == 0) {
				REG_PC += 3;
				REG_CC |= CC_Z; // [hoglet67]
				hcpu->state = hd6309_state_label_a;
				break;
			}
			REG_M = fetch_byte_notrace(cpu, *hcpu->tfm_src);
			hcpu->state = hd6309_state_tfm_write;
			continue;

		case hd6309_state_tfm_write:
			if (cpu->nmi_active) {
				hcpu->state = hd6309_state_label_b;
			} else if (!(REG_CC & CC_F) && cpu->firq_active) {
				hcpu->state = hd6309_state_label_b;
			} else if (!(REG_CC & CC_I) && cpu->irq_active) {
				hcpu->state = hd6309_state_label_b;
			} else {
				NVMA_CYCLE;
				store_byte(cpu, *hcpu->tfm_dest, REG_M);
				*hcpu->tfm_src += hcpu->tfm_src_mod;
				*hcpu->tfm_dest += hcpu->tfm_dest_mod;
				REG_W--;
				cpu->nmi_active = cpu->nmi_latch;
				cpu->firq_active = cpu->firq_latch;
				cpu->irq_active = cpu->irq_latch;
				hcpu->state = hd6309_state_tfm;
			}
			continue;

		case hd6309_state_next_instruction:
			{
			unsigned op;
			// Fetch op-code and process
			op = byte_immediate(cpu);
			op |= cpu->page;
			hcpu->state = hd6309_state_label_a;
			switch (op) {

			// 0x00 - 0x0f direct mode ops
			// 0x40 - 0x4f inherent A register ops
			// 0x50 - 0x5f inherent B register ops
			// 0x60 - 0x6f indexed mode ops
			// 0x70 - 0x7f extended mode ops
			case 0x00: case 0x03:
			case 0x04: case 0x06: case 0x07:
			case 0x08: case 0x09: case 0x0a:
			case 0x0c: case 0x0d: case 0x0f:
			case 0x40: case 0x43:
			case 0x44: case 0x46: case 0x47:
			case 0x48: case 0x49: case 0x4a:
			case 0x4c: case 0x4d: case 0x4f:
			case 0x50: case 0x53:
			case 0x54: case 0x56: case 0x57:
			case 0x58: case 0x59: case 0x5a:
			case 0x5c: case 0x5d: case 0x5f:
			case 0x60: case 0x63:
			case 0x64: case 0x66: case 0x67:
			case 0x68: case 0x69: case 0x6a:
			case 0x6c: case 0x6d: case 0x6f:
			case 0x70: case 0x73:
			case 0x74: case 0x76: case 0x77:
			case 0x78: case 0x79: case 0x7a:
			case 0x7c: case 0x7d: case 0x7f: {
				uint16_t ea;
				unsigned tmp1;
				switch ((op >> 4) & 0xf) {
				case 0x0: ea = ea_direct(cpu); tmp1 = fetch_byte_notrace(cpu, ea); break;
				case 0x4: ea = 0; tmp1 = REG_A; break;
				case 0x5: ea = 0; tmp1 = REG_B; break;
				case 0x6: ea = ea_indexed(cpu); tmp1 = fetch_byte_notrace(cpu, ea); break;
				case 0x7: ea = ea_extended(cpu); tmp1 = fetch_byte_notrace(cpu, ea); break;
				default: ea = tmp1 = 0; break;
				}
				switch (op & 0xf) {
				case 0x0: tmp1 = op_neg(cpu, tmp1); break; // NEG, NEGA, NEGB
				case 0x3: tmp1 = op_com(cpu, tmp1); break; // COM, COMA, COMB
				case 0x4: tmp1 = op_lsr(cpu, tmp1); break; // LSR, LSRA, LSRB
				case 0x6: tmp1 = op_ror(cpu, tmp1); break; // ROR, RORA, RORB
				case 0x7: tmp1 = op_asr(cpu, tmp1); break; // ASR, ASRA, ASRB
				case 0x8: tmp1 = op_asl(cpu, tmp1); break; // ASL, ASLA, ASLB
				case 0x9: tmp1 = op_rol(cpu, tmp1); break; // ROL, ROLA, ROLB
				case 0xa: tmp1 = op_dec(cpu, tmp1); break; // DEC, DECA, DECB
				case 0xc: tmp1 = op_inc(cpu, tmp1); break; // INC, INCA, INCB
				case 0xd: tmp1 = op_tst(cpu, tmp1); break; // TST, TSTA, TSTB
				case 0xf: tmp1 = op_clr(cpu, tmp1); break; // CLR, CLRA, CLRB
				default: break;
				}
				switch (op & 0xf) {
				case 0xd: // TST
					NVMA_CYCLE;
					if (!NATIVE_MODE)
						NVMA_CYCLE;
					// XXX does the result end up in M for TST?
					break;
				default: // the rest need storing
					switch ((op >> 4) & 0xf) {
					default:
					case 0x0: case 0x6: case 0x7:
						NVMA_CYCLE;
						REG_M = tmp1;  // [hoglet67]
						store_byte(cpu, ea, tmp1);
						break;
					case 0x4:
						REG_A = tmp1;
						if (!NATIVE_MODE)
							peek_byte(cpu, REG_PC);
						break;
					case 0x5:
						REG_B = tmp1;
						if (!NATIVE_MODE)
							peek_byte(cpu, REG_PC);
						break;
					}
				}
			} break;

			/* XXX: in documentation, the usual savings while
			 * computing effective address don't seem to apply to
			 * these instructions, so in theory an extra cycles
			 * needs to be inserted to account for that.  Needs
			 * real hardware test. */

			// 0x01, 0x61, 0x71 OIM
			// 0x02, 0x62, 0x72 AIM
			// 0x05, 0x65, 0x75 EIM
			// 0x0b, 0x6b, 0x7b TIM
			case 0x01: case 0x61: case 0x71:
			case 0x02: case 0x62: case 0x72:
			case 0x05: case 0x65: case 0x75:
			case 0x0b: case 0x6b: case 0x7b: {
				unsigned a, tmp1;
				REG_M = byte_immediate(cpu);  // [hoglet67]
				switch ((op >> 4) & 0xf) {
				default:
				case 0x0: a = ea_direct(cpu); tmp1 = fetch_byte_notrace(cpu, a); break;
				case 0x6: a = ea_indexed(cpu); tmp1 = fetch_byte_notrace(cpu, a); break;
				case 0x7: a = ea_extended(cpu); tmp1 = fetch_byte_notrace(cpu, a); break;
				}
				switch (op & 0xf) {
				default:
				case 0x1: tmp1 = op_or(cpu, tmp1, REG_M); break;
				case 0x2: tmp1 = op_and(cpu, tmp1, REG_M); break;
				case 0x5: tmp1 = op_eor(cpu, tmp1, REG_M); break;
				case 0xb: tmp1 = op_and(cpu, tmp1, REG_M); break;
				}
				switch (op & 0xf) {
				case 0xb: // TIM
					NVMA_CYCLE;
					break;
				default: // the others
					store_byte(cpu, a, tmp1);
					break;
				}
			} break;

			// 0x0e JMP direct
			// 0x6e JMP indexed
			// 0x7e JMP extended
			case 0x0e: case 0x6e: case 0x7e: {
				unsigned ea;
				switch ((op >> 4) & 0xf) {
				case 0x0: ea = ea_direct(cpu); break;
				case 0x6: ea = ea_indexed(cpu); break;
				case 0x7: ea = ea_extended(cpu); break;
				default: ea = 0; break;
				}
				REG_PC = ea;
			} break;

			// 0x10 Page 2
			// 0x1010, 0x1011 Page 2
			case 0x10:
			case 0x0210:
			case 0x0211:
				hcpu->state = hd6309_state_next_instruction;
				cpu->page = 0x200;
				continue;

			// 0x11 Page 3
			// 0x1110, 0x1111 Page 3
			case 0x11:
			case 0x0310:
			case 0x0311:
				hcpu->state = hd6309_state_next_instruction;
				cpu->page = 0x300;
				continue;

			// 0x12 NOP inherent
			case 0x12: peek_byte(cpu, REG_PC); break;

			// 0x13 SYNC inherent
			// TODO: "There appears to be a bug with SYNC in native
			// mode" [hoglet67]
			case 0x13:
				if (!NATIVE_MODE)
					peek_byte(cpu, REG_PC);
				cpu->nmi_active = cpu->nmi_latch;
				cpu->firq_active = cpu->firq_latch;
				cpu->irq_active = cpu->irq_latch;
				instruction_posthook(cpu);
				hcpu->state = hd6309_state_sync;
				continue;

			// 0x14 SEXW inherent
			case 0x14:
				REG_D = (REG_W & 0x8000) ? 0xffff : 0;
				CLR_NZ;
				SET_N16(REG_D);
				if (REG_D == 0 && REG_W == 0)
					REG_CC |= CC_Z;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				break;

			// 0x16 LBRA relative
			case 0x16: {
				uint16_t ea;
				ea = long_relative(cpu);
				REG_PC += ea;
				NVMA_CYCLE;
				if (!NATIVE_MODE)
					NVMA_CYCLE;
			} break;

			// 0x17 LBSR relative
			case 0x17: {
				uint16_t ea;
				ea = long_relative(cpu);
				ea += REG_PC;
				NVMA_CYCLE;
				NVMA_CYCLE;
				if (!NATIVE_MODE) {
					NVMA_CYCLE;
					NVMA_CYCLE;
				}
				push_s_word(cpu, REG_PC);
				REG_PC = ea;
			} break;

			// 0x19 DAA inherent
			case 0x19:
				// TODO: behaviour for illegal input differs on
				// the 6309 [hoglet67]
				REG_A = op_daa(cpu, REG_A);
				peek_byte(cpu, REG_PC);
				break;

			// 0x1a ORCC immediate
			case 0x1a: {
				unsigned data;
				data = byte_immediate(cpu);
				REG_CC |= data;
				peek_byte(cpu, REG_PC);
			} break;

			// 0x1c ANDCC immediate
			case 0x1c: {
				unsigned data;
				data = byte_immediate(cpu);
				REG_CC &= data;
				peek_byte(cpu, REG_PC);
			} break;

			// 0x1d SEX inherent
			case 0x1d:
				REG_D = sex8(REG_B);
				CLR_NZ;
				SET_NZ16(REG_D);
				if (!NATIVE_MODE)
					peek_byte(cpu, REG_PC);
				break;

			// 0x1e EXG immediate
			case 0x1e: {
				unsigned postbyte;
				uint16_t tmp1, tmp2;
				postbyte = byte_immediate(cpu);
				switch (postbyte >> 4) {
					case 0x0: tmp1 = REG_D; break;
					case 0x1: tmp1 = REG_X; break;
					case 0x2: tmp1 = REG_Y; break;
					case 0x3: tmp1 = REG_U; break;
					case 0x4: tmp1 = REG_S; break;
					case 0x5: tmp1 = REG_PC; break;
					case 0x6: tmp1 = REG_W; break;
					case 0x7: tmp1 = REG_V; break;
					case 0x8: tmp1 = (REG_A << 8) | REG_A; break;
					case 0x9: tmp1 = (REG_B << 8) | REG_B; break;
					case 0xa: tmp1 = (REG_CC << 8) | REG_CC; break;
					case 0xb: tmp1 = (REG_DP << 8) | REG_DP; break;
					case 0xe: tmp1 = (REG_E << 8) | REG_E; break;
					case 0xf: tmp1 = (REG_F << 8) | REG_F; break;
					default:  tmp1 = 0; break;
				}
				switch (postbyte & 0xf) {
					case 0x0: tmp2 = REG_D; REG_D = tmp1; break;
					case 0x1: tmp2 = REG_X; REG_X = tmp1; break;
					case 0x2: tmp2 = REG_Y; REG_Y = tmp1; break;
					case 0x3: tmp2 = REG_U; REG_U = tmp1; break;
					case 0x4: tmp2 = REG_S; REG_S = tmp1; break;
					case 0x5: tmp2 = REG_PC; REG_PC = tmp1; break;
					case 0x6: tmp2 = REG_W; REG_W = tmp1; break;
					case 0x7: tmp2 = REG_V; REG_V = tmp1; break;
					case 0x8: tmp2 = (REG_A << 8) | REG_A; REG_A = tmp1 >> 8; break;
					case 0x9: tmp2 = (REG_B << 8) | REG_B; REG_B = tmp1; break;
					case 0xa: tmp2 = (REG_CC << 8) | REG_CC; REG_CC = tmp1; break;
					case 0xb: tmp2 = (REG_DP << 8) | REG_DP; REG_DP = tmp1 >> 8; break;
					case 0xe: tmp2 = (REG_E << 8) | REG_E; REG_E = tmp1 >> 8; break;
					case 0xf: tmp2 = (REG_F << 8) | REG_F; REG_F = tmp1; break;
					default:  tmp2 = 0; break;
				}
				switch (postbyte >> 4) {
					case 0x0: REG_D = tmp2; break;
					case 0x1: REG_X = tmp2; break;
					case 0x2: REG_Y = tmp2; break;
					case 0x3: REG_U = tmp2; break;
					case 0x4: REG_S = tmp2; break;
					case 0x5: REG_PC = tmp2; break;
					case 0x6: REG_W = tmp2; break;
					case 0x7: REG_V = tmp2; break;
					case 0x8: REG_A = tmp2 >> 8; break;
					case 0x9: REG_B = tmp2; break;
					case 0xa: REG_CC = tmp2; break;
					case 0xb: REG_DP = tmp2 >> 8; break;
					case 0xe: REG_E = tmp2 >> 8; break;
					case 0xf: REG_F = tmp2; break;
					default: break;
				}
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				if (!NATIVE_MODE) {
					NVMA_CYCLE;
					NVMA_CYCLE;
					NVMA_CYCLE;
				}
			} break;

			// 0x1f TFR immediate
			case 0x1f: {
				unsigned postbyte;
				uint16_t tmp1;
				postbyte = byte_immediate(cpu);
				switch (postbyte >> 4) {
					case 0x0: tmp1 = REG_D; break;
					case 0x1: tmp1 = REG_X; break;
					case 0x2: tmp1 = REG_Y; break;
					case 0x3: tmp1 = REG_U; break;
					case 0x4: tmp1 = REG_S; break;
					case 0x5: tmp1 = REG_PC; break;
					case 0x6: tmp1 = REG_W; break;
					case 0x7: tmp1 = REG_V; break;
					case 0x8: tmp1 = (REG_A << 8) | REG_A; break;
					case 0x9: tmp1 = (REG_B << 8) | REG_B; break;
					case 0xa: tmp1 = (REG_CC << 8) | REG_CC; break;
					case 0xb: tmp1 = (REG_DP << 8) | REG_DP; break;
					case 0xe: tmp1 = (REG_E << 8) | REG_E; break;
					case 0xf: tmp1 = (REG_F << 8) | REG_F; break;
					default: tmp1 = 0; break;
				}
				switch (postbyte & 0xf) {
					case 0x0: REG_D = tmp1; break;
					case 0x1: REG_X = tmp1; break;
					case 0x2: REG_Y = tmp1; break;
					case 0x3: REG_U = tmp1; break;
					case 0x4: REG_S = tmp1; break;
					case 0x5: REG_PC = tmp1; break;
					case 0x6: REG_W = tmp1; break;
					case 0x7: REG_V = tmp1; break;
					case 0x8: REG_A = tmp1 >> 8; break;
					case 0x9: REG_B = tmp1; break;
					case 0xa: REG_CC = tmp1; break;
					case 0xb: REG_DP = tmp1 >> 8; break;
					case 0xe: REG_E = tmp1 >> 8; break;
					case 0xf: REG_F = tmp1; break;
					default: break;
				}
				NVMA_CYCLE;
				NVMA_CYCLE;
				if (!NATIVE_MODE) {
					NVMA_CYCLE;
					NVMA_CYCLE;
				}
			} break;

			// 0x20 - 0x2f short branches
			case 0x20: case 0x21: case 0x22: case 0x23:
			case 0x24: case 0x25: case 0x26: case 0x27:
			case 0x28: case 0x29: case 0x2a: case 0x2b:
			case 0x2c: case 0x2d: case 0x2e: case 0x2f: {
				unsigned tmp = sex8(byte_immediate(cpu));
				NVMA_CYCLE;
				if (branch_condition(cpu, op))
					REG_PC += tmp;
			} break;

			// 0x30 LEAX indexed
			case 0x30:
				REG_X = ea_indexed(cpu);
				CLR_Z;
				SET_Z16(REG_X);
				NVMA_CYCLE;
				break;

			// 0x31 LEAY indexed
			case 0x31:
				REG_Y = ea_indexed(cpu);
				CLR_Z;
				SET_Z16(REG_Y);
				NVMA_CYCLE;
				break;

			// 0x32 LEAS indexed
			case 0x32:
				REG_S = ea_indexed(cpu);
				NVMA_CYCLE;
				cpu->nmi_armed = 1;  // XXX: Really?
				break;

			// 0x33 LEAU indexed
			case 0x33:
				REG_U = ea_indexed(cpu);
				NVMA_CYCLE;
				break;

			// 0x34 PSHS immediate
			case 0x34:
				{
					unsigned postbyte = byte_immediate(cpu);
					NVMA_CYCLE;
					if (!NATIVE_MODE)
						NVMA_CYCLE;
					peek_byte(cpu, REG_S);
					if (postbyte & 0x80) { push_s_word(cpu, REG_PC); }
					if (postbyte & 0x40) { push_s_word(cpu, REG_U); }
					if (postbyte & 0x20) { push_s_word(cpu, REG_Y); }
					if (postbyte & 0x10) { push_s_word(cpu, REG_X); }
					if (postbyte & 0x08) { push_s_byte(cpu, REG_DP); }
					if (postbyte & 0x04) { push_s_byte(cpu, REG_B); }
					if (postbyte & 0x02) { push_s_byte(cpu, REG_A); }
					if (postbyte & 0x01) { push_s_byte(cpu, REG_CC); }
				}
				break;

			// 0x35 PULS immediate
			case 0x35:
				{
					unsigned postbyte = byte_immediate(cpu);
					NVMA_CYCLE;
					if (!NATIVE_MODE)
						NVMA_CYCLE;
					if (postbyte & 0x01) { REG_CC = pull_s_byte(cpu); }
					if (postbyte & 0x02) { REG_A = pull_s_byte(cpu); }
					if (postbyte & 0x04) { REG_B = pull_s_byte(cpu); }
					if (postbyte & 0x08) { REG_DP = pull_s_byte(cpu); }
					if (postbyte & 0x10) { REG_X = pull_s_word(cpu); }
					if (postbyte & 0x20) { REG_Y = pull_s_word(cpu); }
					if (postbyte & 0x40) { REG_U = pull_s_word(cpu); }
					if (postbyte & 0x80) { REG_PC = pull_s_word(cpu); }
					peek_byte(cpu, REG_S);
				}
				break;

			// 0x36 PSHU immediate
			case 0x36:
				{
					unsigned postbyte = byte_immediate(cpu);
					NVMA_CYCLE;
					if (!NATIVE_MODE)
						NVMA_CYCLE;
					peek_byte(cpu, REG_U);
					if (postbyte & 0x80) { push_u_word(cpu, REG_PC); }
					if (postbyte & 0x40) { push_u_word(cpu, REG_S); }
					if (postbyte & 0x20) { push_u_word(cpu, REG_Y); }
					if (postbyte & 0x10) { push_u_word(cpu, REG_X); }
					if (postbyte & 0x08) { push_u_byte(cpu, REG_DP); }
					if (postbyte & 0x04) { push_u_byte(cpu, REG_B); }
					if (postbyte & 0x02) { push_u_byte(cpu, REG_A); }
					if (postbyte & 0x01) { push_u_byte(cpu, REG_CC); }
				}
				break;

			// 0x37 PULU immediate
			case 0x37:
				{
					unsigned postbyte = byte_immediate(cpu);
					NVMA_CYCLE;
					if (!NATIVE_MODE)
						NVMA_CYCLE;
					if (postbyte & 0x01) { REG_CC = pull_u_byte(cpu); }
					if (postbyte & 0x02) { REG_A = pull_u_byte(cpu); }
					if (postbyte & 0x04) { REG_B = pull_u_byte(cpu); }
					if (postbyte & 0x08) { REG_DP = pull_u_byte(cpu); }
					if (postbyte & 0x10) { REG_X = pull_u_word(cpu); }
					if (postbyte & 0x20) { REG_Y = pull_u_word(cpu); }
					if (postbyte & 0x40) { REG_S = pull_u_word(cpu); }
					if (postbyte & 0x80) { REG_PC = pull_u_word(cpu); }
					peek_byte(cpu, REG_U);
				}
				break;

			// 0x39 RTS inherent
			case 0x39:
				peek_byte(cpu, REG_PC);
				REG_PC = pull_s_word(cpu);
				NVMA_CYCLE;
				break;

			// 0x3a ABX inherent
			case 0x3a:
				REG_X += REG_B;
				peek_byte(cpu, REG_PC);
				if (!NATIVE_MODE)
					NVMA_CYCLE;
				break;

			// 0x3b RTI inherent
			case 0x3b:
				peek_byte(cpu, REG_PC);
				REG_CC = pull_s_byte(cpu);
				if (REG_CC & CC_E) {
					REG_A = pull_s_byte(cpu);
					REG_B = pull_s_byte(cpu);
					if (NATIVE_MODE) {
						REG_E = pull_s_byte(cpu);
						REG_F = pull_s_byte(cpu);
					}
					REG_DP = pull_s_byte(cpu);
					REG_X = pull_s_word(cpu);
					REG_Y = pull_s_word(cpu);
					REG_U = pull_s_word(cpu);
					REG_PC = pull_s_word(cpu);
				} else {
					REG_PC = pull_s_word(cpu);
				}
				cpu->nmi_armed = 1;
				peek_byte(cpu, REG_S);
				break;

			// 0x3c CWAI immediate
			case 0x3c: {
				unsigned data;
				data = byte_immediate(cpu);
				REG_CC &= data;
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				stack_irq_registers(cpu, 1);
				NVMA_CYCLE;
				hcpu->state = hd6309_state_dispatch_irq;
			} break;

			// 0x3d MUL inherent
			case 0x3d: {
				unsigned tmp;
				REG_M = REG_B;
				tmp = REG_A * REG_B;
				REG_D = tmp;
				CLR_ZC;
				SET_Z16(tmp);
				if (tmp & 0x80)
					REG_CC |= CC_C;
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				if (!NATIVE_MODE)
					NVMA_CYCLE;
			} break;

			// 0x3f SWI inherent
			// TODO: "There appears to be a bug with SWI in native
			// mode, if it is interrupted with an NMI" [hoglet67]
			case 0x3f:
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu, 1);
				instruction_posthook(cpu);
				take_interrupt(cpu, CC_F|CC_I, MC6809_INT_VEC_SWI);
				continue;

			// 0x80 - 0xbf A register arithmetic ops
			// 0xc0 - 0xff B register arithmetic ops
			case 0x80: case 0x81: case 0x82:
			case 0x84: case 0x85: case 0x86:
			case 0x88: case 0x89: case 0x8a: case 0x8b:
			case 0x90: case 0x91: case 0x92:
			case 0x94: case 0x95: case 0x96:
			case 0x98: case 0x99: case 0x9a: case 0x9b:
			case 0xa0: case 0xa1: case 0xa2:
			case 0xa4: case 0xa5: case 0xa6:
			case 0xa8: case 0xa9: case 0xaa: case 0xab:
			case 0xb0: case 0xb1: case 0xb2:
			case 0xb4: case 0xb5: case 0xb6:
			case 0xb8: case 0xb9: case 0xba: case 0xbb:
			case 0xc0: case 0xc1: case 0xc2:
			case 0xc4: case 0xc5: case 0xc6:
			case 0xc8: case 0xc9: case 0xca: case 0xcb:
			case 0xd0: case 0xd1: case 0xd2:
			case 0xd4: case 0xd5: case 0xd6:
			case 0xd8: case 0xd9: case 0xda: case 0xdb:
			case 0xe0: case 0xe1: case 0xe2:
			case 0xe4: case 0xe5: case 0xe6:
			case 0xe8: case 0xe9: case 0xea: case 0xeb:
			case 0xf0: case 0xf1: case 0xf2:
			case 0xf4: case 0xf5: case 0xf6:
			case 0xf8: case 0xf9: case 0xfa: case 0xfb: {
				unsigned tmp1, tmp2;
				tmp1 = !(op & 0x40) ? REG_A : REG_B;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = byte_immediate(cpu); break;
				case 1: tmp2 = byte_direct(cpu); break;
				case 2: tmp2 = byte_indexed(cpu); break;
				case 3: tmp2 = byte_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				switch (op & 0xf) {
				case 0x0: tmp1 = op_sub(cpu, tmp1, tmp2); break; // SUBA, SUBB
				case 0x1: (void)op_sub(cpu, tmp1, tmp2); break; // CMPA, CMPB
				case 0x2: tmp1 = op_sbc(cpu, tmp1, tmp2); break; // SBCA, SBCB
				case 0x4: tmp1 = op_and(cpu, tmp1, tmp2); break; // ANDA, ANDB
				case 0x5: (void)op_and(cpu, tmp1, tmp2); break; // BITA, BITB
				case 0x6: tmp1 = op_ld(cpu, 0, tmp2); break; // LDA, LDB
				case 0x8: tmp1 = op_eor(cpu, tmp1, tmp2); break; // EORA, EORB
				case 0x9: tmp1 = op_adc(cpu, tmp1, tmp2); break; // ADCA, ADCB
				case 0xa: tmp1 = op_or(cpu, tmp1, tmp2); break; // ORA, ORB
				case 0xb: tmp1 = op_add(cpu, tmp1, tmp2); break; // ADDA, ADDB
				default: break;
				}
				if (!(op & 0x40)) {
					REG_A = tmp1;
				} else {
					REG_B = tmp1;
				}
			} break;

			// 0x83, 0x93, 0xa3, 0xb3 SUBD
			// 0xc3, 0xd3, 0xe3, 0xf3 ADDD
			case 0x83: case 0x93: case 0xa3: case 0xb3:
			case 0xc3: case 0xd3: case 0xe3: case 0xf3: {
				unsigned tmp1, tmp2;
				tmp1 = REG_D;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				switch (op & 0x40) {
				default:
				case 0x00: tmp1 = op_sub16(cpu, tmp1, tmp2); break; // SUBD
				case 0x40: tmp1 = op_add16(cpu, tmp1, tmp2); break; // ADDD
				}
				if (!NATIVE_MODE)
					NVMA_CYCLE;
				REG_D = tmp1;
			} break;

			// 0x8c, 0x9c, 0xac, 0xbc CMPX
			// 0x1083, 0x1093, 0x10a3, 0x10b3 CMPD
			// 0x108c, 0x109c, 0x10ac, 0x10bc CMPY
			// 0x1183, 0x1193, 0x11a3, 0x11b3 CMPU
			// 0x118c, 0x119c, 0x11ac, 0x11bc CMPS
			case 0x8c: case 0x9c: case 0xac: case 0xbc:
			case 0x0283: case 0x0293: case 0x02a3: case 0x02b3:
			case 0x028c: case 0x029c: case 0x02ac: case 0x02bc:
			case 0x0383: case 0x0393: case 0x03a3: case 0x03b3:
			case 0x038c: case 0x039c: case 0x03ac: case 0x03bc: {
				unsigned tmp1, tmp2;
				switch (op & 0x0308) {
				default:
				case 0x0000: tmp1 = REG_X; break;
				case 0x0200: tmp1 = REG_D; break;
				case 0x0208: tmp1 = REG_Y; break;
				case 0x0300: tmp1 = REG_U; break;
				case 0x0308: tmp1 = REG_S; break;
				}
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				(void)op_sub16(cpu, tmp1, tmp2);
				if (!NATIVE_MODE)
					NVMA_CYCLE;
			} break;

			// 0x8d BSR
			// 0x9d, 0xad, 0xbd JSR
			case 0x8d: case 0x9d: case 0xad: case 0xbd: {
				uint16_t ea;
				switch ((op >> 4) & 3) {
				case 0: ea = short_relative(cpu); ea += REG_PC; NVMA_CYCLE; NVMA_CYCLE; if (!NATIVE_MODE) NVMA_CYCLE; break;
				case 1: ea = ea_direct(cpu); peek_byte(cpu, ea); NVMA_CYCLE; break;
				case 2: ea = ea_indexed(cpu); peek_byte(cpu, ea); NVMA_CYCLE; break;
				case 3: ea = ea_extended(cpu); peek_byte(cpu, ea); NVMA_CYCLE; break;
				default: ea = 0; break;
				}
				push_s_word(cpu, REG_PC);
				REG_PC = ea;
			} break;

			// 0x8e, 0x9e, 0xae, 0xbe LDX
			// 0xcc, 0xdc, 0xec, 0xfc LDD
			// 0xce, 0xde, 0xee, 0xfe LDU
			// 0x1086, 0x1096, 0x10a6, 0x10b6 LDW
			// 0x108e, 0x109e, 0x10ae, 0x10be LDY
			// 0x10ce, 0x10de, 0x10ee, 0x10fe LDS
			case 0x8e: case 0x9e: case 0xae: case 0xbe:
			case 0xcc: case 0xdc: case 0xec: case 0xfc:
			case 0xce: case 0xde: case 0xee: case 0xfe:
			case 0x0286: case 0x0296: case 0x02a6: case 0x02b6:
			case 0x028e: case 0x029e: case 0x02ae: case 0x02be:
			case 0x02ce: case 0x02de: case 0x02ee: case 0x02fe: {
				unsigned tmp1, tmp2;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				tmp1 = op_ld16(cpu, 0, tmp2);
				switch (op & 0x034e) {
				default:
				case 0x000e: REG_X = tmp1; break;
				case 0x004c: REG_D = tmp1; break;
				case 0x004e: REG_U = tmp1; break;
				case 0x0206: REG_W = tmp1; break;
				case 0x020e: REG_Y = tmp1; break;
				case 0x024e: REG_S = tmp1; cpu->nmi_armed = 1; break;
				}
			} break;

			// 0x97, 0xa7, 0xb7 STA
			// 0xd7, 0xe7, 0xf7 STB
			// 0x1197, 0x11a7, 0x11b7 STE
			// 0x11d7, 0x11e7, 0x11f7 STF
			case 0x97: case 0xa7: case 0xb7:
			case 0xd7: case 0xe7: case 0xf7:
			case 0x0397: case 0x03a7: case 0x03b7:
			case 0x03d7: case 0x03e7: case 0x03f7: {
				uint16_t ea;
				uint8_t tmp1;
				switch (op & 0x0340) {
				default:
				case 0x0000: tmp1 = REG_A; break;
				case 0x0040: tmp1 = REG_B; break;
				case 0x0300: tmp1 = REG_E; break;
				case 0x0340: tmp1 = REG_F; break;
				}
				switch ((op >> 4) & 3) {
				case 1: ea = ea_direct(cpu); break;
				case 2: ea = ea_indexed(cpu); break;
				case 3: ea = ea_extended(cpu); break;
				default: ea = 0; break;
				}
				store_byte(cpu, ea, tmp1);
				CLR_NZV;
				SET_NZ8(tmp1);
			} break;

			// 0x9f, 0xaf, 0xbf STX
			// 0xdd, 0xed, 0xfd STD
			// 0xdf, 0xef, 0xff STU
			// 0x1097, 0x10a7, 0x10b7 STW
			// 0x109f, 0x10af, 0x10bf STY
			// 0x10df, 0x10ef, 0x10ff STS
			case 0x9f: case 0xaf: case 0xbf:
			case 0xdd: case 0xed: case 0xfd:
			case 0xdf: case 0xef: case 0xff:
			case 0x0297: case 0x02a7: case 0x02b7:
			case 0x029f: case 0x02af: case 0x02bf:
			case 0x02df: case 0x02ef: case 0x02ff: {
				uint16_t ea, tmp1;
				switch (op & 0x034e) {
				default:
				case 0x000e: tmp1 = REG_X; break;
				case 0x004c: tmp1 = REG_D; break;
				case 0x004e: tmp1 = REG_U; break;
				case 0x0206: tmp1 = REG_W; break;
				case 0x020e: tmp1 = REG_Y; break;
				case 0x024e: tmp1 = REG_S; break;
				}
				switch ((op >> 4) & 3) {
				case 1: ea = ea_direct(cpu); break;
				case 2: ea = ea_indexed(cpu); break;
				case 3: ea = ea_extended(cpu); break;
				default: ea = 0; break;
				}
				CLR_NZV;
				SET_NZ16(tmp1);
				store_byte(cpu, ea, tmp1 >> 8);
				store_byte(cpu, ea+1, tmp1);
			} break;

			// 0xcd LDQ immediate
			case 0xcd: {
				REG_D = word_immediate(cpu);
				REG_W = word_immediate(cpu);
				CLR_NZ;  // V not cleared [hoglet67]
				SET_N16(REG_D);
				// lower 16 bits (REG_W) ignored [hoglet67]
				if (REG_D == 0)
					REG_CC |= CC_Z;
			} break;

			// 0x1021 - 0x102f long branches
			case 0x0221: case 0x0222: case 0x0223:
			case 0x0224: case 0x0225: case 0x0226: case 0x0227:
			case 0x0228: case 0x0229: case 0x022a: case 0x022b:
			case 0x022c: case 0x022d: case 0x022e: case 0x022f: {
				unsigned tmp = word_immediate(cpu);
				if (branch_condition(cpu, op)) {
					REG_PC += tmp;
					if (!NATIVE_MODE)
						NVMA_CYCLE;
				}
				NVMA_CYCLE;
			} break;

			// XXX: The order in which bits in CC are set when it
			// is the destination register is NOT correct.  Fixing
			// this might mean rewriting all of op_*().  Also, the
			// effect on PC as a destination register needs
			// investigating.

			// 0x1030 ADDR
			// 0x1031 ADCR
			// 0x1032 SUBR
			// 0x1033 SBCR
			// 0x1034 ANDR
			// 0x1035 ORR
			// 0x1036 EORR
			// 0x1037 CMPR
			case 0x0230: case 0x0231: case 0x0232: case 0x0233:
			case 0x0234: case 0x0235: case 0x0236: case 0x0237: {
				unsigned postbyte;
				postbyte = byte_immediate(cpu);
				unsigned tmp1, tmp2;
				if (!(postbyte & 0x08)) {
					// 16-bit operation
					switch (postbyte & 0xf) {
					case 0x0: tmp1 = REG_D; break;
					case 0x1: tmp1 = REG_X; break;
					case 0x2: tmp1 = REG_Y; break;
					case 0x3: tmp1 = REG_U; break;
					case 0x4: tmp1 = REG_S; break;
					case 0x5: tmp1 = REG_PC; break;
					case 0x6: tmp1 = REG_W; break;
					case 0x7: tmp1 = REG_V; break;
					default: tmp1 = 0; break;
					}
					switch ((postbyte >> 4) & 0xf) {
					case 0x8: case 0x9:
					case 0x0: tmp2 = REG_D; break;
					case 0x1: tmp2 = REG_X; break;
					case 0x2: tmp2 = REG_Y; break;
					case 0x3: tmp2 = REG_U; break;
					case 0x4: tmp2 = REG_S; break;
					case 0x5: tmp2 = REG_PC; break;
					case 0xe: case 0xf:
					case 0x6: tmp2 = REG_W; break;
					case 0x7: tmp2 = REG_V; break;
					case 0xa: tmp2 = REG_CC; break;
					// [hoglet67] XXX unclear on whether
					// this applies to all these ops.
					// assuming it does for now.
					// XXX also unclear whether using 'D'
					// also works; example uses 'X'.
					case 0xb: tmp2 = (REG_DP << 8) | REG_M; break;
					default: tmp2 = 0; break;
					}
					switch (op & 0xf) {
					case 0x0: tmp1 = op_add16(cpu, tmp1, tmp2); break;
					case 0x1: tmp1 = op_adc16(cpu, tmp1, tmp2); break;
					case 0x2: tmp1 = op_sub16(cpu, tmp1, tmp2); break;
					case 0x3: tmp1 = op_sbc16(cpu, tmp1, tmp2); break;
					case 0x4: tmp1 = op_and16(cpu, tmp1, tmp2); break;
					case 0x5: tmp1 = op_or16(cpu, tmp1, tmp2); break;
					case 0x6: tmp1 = op_eor16(cpu, tmp1, tmp2); break;
					case 0x7: (void)op_sub16(cpu, tmp1, tmp2); break;
					default: break;
					}
					switch (postbyte & 0xf) {
					case 0x0: REG_D = tmp1; break;
					case 0x1: REG_X = tmp1; break;
					case 0x2: REG_Y = tmp1; break;
					case 0x3: REG_U = tmp1; break;
					case 0x4: REG_S = tmp1; break;
					case 0x5: REG_PC = tmp1; break;
					case 0x6: REG_W = tmp1; break;
					case 0x7: REG_V = tmp1; break;
					default: break;
					}
				} else {
					// 8-bit operation
					switch (postbyte & 0xf) {
					case 0x8: tmp1 = REG_A; break;
					case 0x9: tmp1 = REG_B; break;
					case 0xa: tmp1 = REG_CC; break;
					case 0xb: tmp1 = REG_DP; break;
					case 0xe: tmp1 = REG_E; break;
					case 0xf: tmp1 = REG_F; break;
					default: tmp1 = 0; break;
					}
					switch ((postbyte >> 4) & 0xf) {
					case 0x0: tmp2 = REG_D & 0xff; break;
					case 0x1: tmp2 = REG_X & 0xff; break;
					case 0x2: tmp2 = REG_Y & 0xff; break;
					case 0x3: tmp2 = REG_U & 0xff; break;
					case 0x4: tmp2 = REG_S & 0xff; break;
					case 0x5: tmp2 = REG_PC & 0xff; break;
					case 0x6: tmp2 = REG_W & 0xff; break;
					case 0x7: tmp2 = REG_V & 0xff; break;
					case 0x8: tmp2 = REG_A; break;
					case 0x9: tmp2 = REG_B; break;
					case 0xa: tmp2 = REG_CC; break;
					case 0xb: tmp2 = REG_DP; break;
					case 0xe: tmp2 = REG_E; break;
					case 0xf: tmp2 = REG_F; break;
					default: tmp2 = 0; break;
					}
					switch (op & 0xf) {
					case 0x0: tmp1 = op_add(cpu, tmp1, tmp2); break;
					case 0x1: tmp1 = op_adc(cpu, tmp1, tmp2); break;
					case 0x2: tmp1 = op_sub(cpu, tmp1, tmp2); break;
					case 0x3: tmp1 = op_sbc(cpu, tmp1, tmp2); break;
					case 0x4: tmp1 = op_and(cpu, tmp1, tmp2); break;
					case 0x5: tmp1 = op_or(cpu, tmp1, tmp2); break;
					case 0x6: tmp1 = op_eor(cpu, tmp1, tmp2); break;
					case 0x7: (void)op_sub(cpu, tmp1, tmp2); break;
					default: break;
					}
					switch (postbyte & 0xf) {
					case 0x8: REG_A = tmp1; break;
					case 0x9: REG_B = tmp1; break;
					case 0xa: REG_CC = tmp1; break;
					case 0xb: REG_DP = tmp1; break;
					case 0xe: REG_E = tmp1; break;
					case 0xf: REG_F = tmp1; break;
					default: break;
					}
				}
				NVMA_CYCLE;
			} break;

			// 0x1038 PSHSW inherent
			case 0x0238:
				NVMA_CYCLE;
				NVMA_CYCLE;
				push_s_byte(cpu, REG_F);
				push_s_byte(cpu, REG_E);
				break;

			// 0x1039 PULSW inherent
			case 0x0239:
				NVMA_CYCLE;
				NVMA_CYCLE;
				REG_E = pull_s_byte(cpu);
				REG_F = pull_s_byte(cpu);
				break;

			// 0x103a PSHUW inherent
			case 0x023a:
				NVMA_CYCLE;
				NVMA_CYCLE;
				push_u_byte(cpu, REG_F);
				push_u_byte(cpu, REG_E);
				break;

			// 0x103b PULUW inherent
			case 0x023b:
				NVMA_CYCLE;
				NVMA_CYCLE;
				REG_E = pull_u_byte(cpu);
				REG_F = pull_u_byte(cpu);
				break;

			// 0x103f SWI2 inherent
			case 0x023f:
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu, 1);
				instruction_posthook(cpu);
				take_interrupt(cpu, 0, MC6809_INT_VEC_SWI2);
				continue;

			// XXX to test: is there really no NEGW, ASRW or ASLW?

			// 0x1040 - 0x104f D register inherent ops
			// 0x1050 - 0x105f W register inherent ops
			case 0x0240: case 0x0243:
			case 0x0244: case 0x0246: case 0x0247:
			case 0x0248: case 0x0249: case 0x024a:
			case 0x024c: case 0x024d: case 0x024f:
			case 0x0253:
			case 0x0254: case 0x0256:
			case 0x0259: case 0x025a:
			case 0x025c: case 0x025d: case 0x025f: {
				unsigned tmp1;
				tmp1 = !(op & 0x10) ? REG_D : REG_W;
				switch (op & 0xf) {
				case 0x0: tmp1 = op_neg16(cpu, tmp1); break; // NEGD
				case 0x3: tmp1 = op_com16(cpu, tmp1); break; // COMD, COMW
				case 0x4: tmp1 = op_lsr16(cpu, tmp1); break; // LSRD, LSRW
				case 0x6: tmp1 = op_ror16(cpu, tmp1); break; // RORD, RORW
				case 0x7: tmp1 = op_asr16(cpu, tmp1); break; // ASRD
				case 0x8: tmp1 = op_asl16(cpu, tmp1); break; // ASLD
				case 0x9: tmp1 = op_rol16(cpu, tmp1); break; // ROLD, ROLW
				case 0xa: tmp1 = op_dec16(cpu, tmp1); break; // DECD, DECW
				case 0xc: tmp1 = op_inc16(cpu, tmp1); break; // INCD, INCW
				case 0xd: tmp1 = op_tst16(cpu, tmp1); break; // TSTD, TSTW
				case 0xf: tmp1 = op_clr16(cpu, tmp1); break; // CLRD, CLRW
				default: break;
				}
				switch (op & 0xf) {
				case 0xd: // TST
					if (!NATIVE_MODE)
						NVMA_CYCLE;
					break;
				default: // the rest
					if (!(op & 0x10)) {
						REG_D = tmp1;
					} else {
						REG_W = tmp1;
					}
					if (!NATIVE_MODE)
						NVMA_CYCLE;
					break;
				}
			} break;

			// 0x1080, 0x1090, 0x10a0, 0x10b0 SUBW
			// 0x1081, 0x1091, 0x10a1, 0x10b1 CMPW
			// 0x108b, 0x109b, 0x10ab, 0x10bb ADDW
			case 0x0280: case 0x0290: case 0x02a0: case 0x02b0:
			case 0x0281: case 0x0291: case 0x02a1: case 0x02b1:
			case 0x028b: case 0x029b: case 0x02ab: case 0x02bb: {
				unsigned tmp1, tmp2;
				tmp1 = REG_W;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				switch (op & 0xf) {
				case 0x0: tmp1 = op_sub16(cpu, tmp1, tmp2); break;
				case 0x1: (void)op_sub16(cpu, tmp1, tmp2); break;
				case 0xb: tmp1 = op_add16(cpu, tmp1, tmp2); break;
				default: break;
				}
				if (!NATIVE_MODE)
					NVMA_CYCLE;
				REG_W = tmp1;
			} break;

			// 0x1082, 0x1092, 0x10a2, 0x10b2 SBCD
			// 0x1084, 0x1094, 0x10a4, 0x10b4 ANDD
			// 0x1085, 0x1095, 0x10a5, 0x10b5 BITD
			// 0x1088, 0x1098, 0x10a8, 0x10b8 EORD
			// 0x1089, 0x1099, 0x10a9, 0x10b9 ADCD
			// 0x108a, 0x109a, 0x10aa, 0x10ba ORD
			case 0x0282: case 0x0292: case 0x02a2: case 0x02b2:
			case 0x0284: case 0x0294: case 0x02a4: case 0x02b4:
			case 0x0285: case 0x0295: case 0x02a5: case 0x02b5:
			case 0x0288: case 0x0298: case 0x02a8: case 0x02b8:
			case 0x0289: case 0x0299: case 0x02a9: case 0x02b9:
			case 0x028a: case 0x029a: case 0x02aa: case 0x02ba: {
				unsigned tmp1, tmp2;
				tmp1 = REG_D;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				switch (op & 0xf) {
				case 0x2: tmp1 = op_sbc16(cpu, tmp1, tmp2); break;
				case 0x4: tmp1 = op_and16(cpu, tmp1, tmp2); break;
				case 0x5: (void)op_and16(cpu, tmp1, tmp2); break;
				case 0x8: tmp1 = op_eor16(cpu, tmp1, tmp2); break;
				case 0x9: tmp1 = op_adc16(cpu, tmp1, tmp2); break;
				case 0xa: tmp1 = op_or16(cpu, tmp1, tmp2); break;
				default: break;
				}
				if (!NATIVE_MODE)
					NVMA_CYCLE;
				REG_D = tmp1;
			} break;

			// 0x10dc, 0x10ec, 0x10fc LDQ
			case 0x02dc: case 0x02ec: case 0x02fc: {
				unsigned ea;
				switch ((op >> 4) & 3) {
				case 1: ea = ea_direct(cpu); break;
				case 2: ea = ea_indexed(cpu); break;
				case 3: ea = ea_extended(cpu); break;
				default: ea = 0; break;
				}
				REG_D = fetch_word_notrace(cpu, ea);
				REG_W = fetch_word_notrace(cpu, ea+2);
				CLR_NZV;
				SET_N16(REG_D);
				if (REG_D == 0 && REG_W == 0)
					REG_CC |= CC_Z;
			} break;

			// 0x10dd, 0x10ed, 0x10fd STQ
			case 0x02dd: case 0x02ed: case 0x02fd: {
				unsigned ea;
				switch ((op >> 4) & 3) {
				case 1: ea = ea_direct(cpu); break;
				case 2: ea = ea_indexed(cpu); break;
				case 3: ea = ea_extended(cpu); break;
				default: ea = 0; break;
				}
				store_byte(cpu, ea, REG_D >> 8);
				store_byte(cpu, ea + 1, REG_D);
				store_byte(cpu, ea + 2, REG_W >> 8);
				store_byte(cpu, ea + 3, REG_W);
				CLR_NZV;
				SET_N16(REG_D);
				if (REG_D == 0 && REG_W == 0)
					REG_CC |= CC_Z;
			} break;

			// 0x1130 - 0x1137 direct logical bit ops
			case 0x0330: case 0x0331: case 0x0332: case 0x0333:
			case 0x0334: case 0x0335: case 0x0336: case 0x0337: {
				unsigned postbyte;
				unsigned mem_byte;
				unsigned ea;
				postbyte = byte_immediate(cpu);
				ea = ea_direct(cpu);
				mem_byte = fetch_byte_notrace(cpu, ea);
				NVMA_CYCLE;
				int dst_bit = postbyte & 7;
				int src_bit = (postbyte >> 3) & 7;
				unsigned reg_code = (postbyte >> 6) & 3;
				unsigned dst_mask = (1 << dst_bit);
				unsigned reg_val;
				switch (reg_code) {
				case 0: reg_val = REG_CC; break;
				case 1: reg_val = REG_A; break;
				case 2: reg_val = REG_B; break;
				// Invalid register here does *not* trigger an
				// illegal instruction trap.
				// TODO: verify if this value is predictable:
				default: reg_val = 0; break;
				}
				unsigned out;
				switch (op & 7) {
				case 0: // BAND
					out = (mem_byte >> src_bit) & (reg_val >> dst_bit);
					break;
				case 1: // BIAND
					out = (~mem_byte >> src_bit) & (reg_val >> dst_bit);
					break;
				case 2: // BOR
					out = (mem_byte >> src_bit) | (reg_val >> dst_bit);
					break;
				case 3: // BIOR
					out = (~mem_byte >> src_bit) | (reg_val >> dst_bit);
					break;
				case 4: // BEOR
					out = (mem_byte >> src_bit) ^ (reg_val >> dst_bit);
					break;
				case 5: // BIEOR
					out = (~mem_byte >> src_bit) ^ (reg_val >> dst_bit);
					break;
				case 6: // LDBT
					out = mem_byte >> src_bit;
					break;
				case 7: // STBT
					out = reg_val >> src_bit;
					break;
				default: out = 0; break;
				}
				out &= 1;
				if ((op & 7) == 7) {
					// STBT
					mem_byte = (mem_byte & ~dst_mask) | (out << dst_bit);
					store_byte(cpu, ea, mem_byte);
				} else {
					switch (reg_code) {
					default:
					case 0:
						REG_CC = (REG_CC & ~dst_mask) | (out << dst_bit);
						break;
					case 1:
						REG_A = (REG_A & ~dst_mask) | (out << dst_bit);
						break;
					case 2:
						REG_B = (REG_B & ~dst_mask) | (out << dst_bit);
						break;
					}
				}
			} break;

			// 0x1138 TFM r0+,r1+
			// 0x1139 TFM r0-,r1-
			// 0x113a TFM r0+,r1
			// 0x113b TFM r0,r1+
			case 0x0338: case 0x0339: case 0x033a: case 0x033b: {
				unsigned postbyte;
				switch (op & 3) {
				case 0: hcpu->tfm_src_mod = hcpu->tfm_dest_mod = 1; break;
				case 1: hcpu->tfm_src_mod = hcpu->tfm_dest_mod = -1; break;
				case 2: hcpu->tfm_src_mod = 1; hcpu->tfm_dest_mod = 0; break;
				case 3: hcpu->tfm_src_mod = 0; hcpu->tfm_dest_mod = 1; break;
				default: break;
				}
				postbyte = byte_immediate(cpu);
				// Verified 3 NVMA cycles:
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				hcpu->tfm_src = tfm_reg_to_ptr(hcpu, postbyte >> 4);
				hcpu->tfm_dest = tfm_reg_to_ptr(hcpu, postbyte & 0xf);
				REG_CC &= ~(CC_Z); // [hoglet67]
				if (!hcpu->tfm_src || !hcpu->tfm_dest) {
					stack_irq_registers(cpu, 1);
					instruction_posthook(cpu);
					take_interrupt(cpu, CC_F|CC_I, HD6309_INT_VEC_ILLEGAL);
					continue;
				}
				REG_PC -= 3;
				hcpu->state = hd6309_state_tfm;
				continue;
			}

			// 0x113c BITMD immediate
			case 0x033c: {
				REG_M = byte_immediate(cpu);
				unsigned data = REG_M & (MD_D0 | MD_IL);
				if (REG_MD & data)
					REG_CC &= ~CC_Z;
				else
					REG_CC |= CC_Z;
				REG_MD &= ~data;
				NVMA_CYCLE;
			} break;

			// 0x113d LDMD immediate
			case 0x033d: {
				unsigned data;
				data = byte_immediate(cpu);
				data &= (MD_FM | MD_NM);
				REG_MD = (REG_MD & ~(MD_FM | MD_NM)) | data;
				NVMA_CYCLE;
				NVMA_CYCLE;
			} break;

			// 0x113f SWI3 inherent
			case 0x033f:
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu, 1);
				instruction_posthook(cpu);
				take_interrupt(cpu, 0, MC6809_INT_VEC_SWI3);
				continue;

			// 0x1140 - 0x114f E register inherent ops
			// 0x1150 - 0x115f F register inherent ops
			case 0x0343:
			case 0x034a:
			case 0x034c: case 0x034d: case 0x034f:
			case 0x0353:
			case 0x035a:
			case 0x035c: case 0x035d: case 0x035f: {
				unsigned tmp1;
				tmp1 = !(op & 0x10) ? REG_E : REG_F;
				switch (op & 0xf) {
				case 0x3: tmp1 = op_com(cpu, tmp1); break; // COME, COMF
				case 0xa: tmp1 = op_dec(cpu, tmp1); break; // DECE, DECF
				case 0xc: tmp1 = op_inc(cpu, tmp1); break; // INCE, INCF
				case 0xd: tmp1 = op_tst(cpu, tmp1); break; // TSTE, TSTF
				case 0xf: tmp1 = op_clr(cpu, tmp1); break; // CLRE, CLRF
				default: break;
				}
				switch (op & 0xf) {
				case 0xd: // TST
					NVMA_CYCLE;
					if (!NATIVE_MODE)
						NVMA_CYCLE;
					break;
				default: // the rest need storing
					if (!(op & 0x10)) {
						REG_E = tmp1;
					} else {
						REG_F = tmp1;
					}
					if (!NATIVE_MODE)
						NVMA_CYCLE;
					break;
				}
			} break;

			// 0x1180 - 0x11bf E register arithmetic ops
			// 0x11c0 - 0x11ff F register arithmetic ops
			case 0x0380: case 0x0381: case 0x0386: case 0x038b:
			case 0x0390: case 0x0391: case 0x0396: case 0x039b:
			case 0x03a0: case 0x03a1: case 0x03a6: case 0x03ab:
			case 0x03b0: case 0x03b1: case 0x03b6: case 0x03bb:
			case 0x03c0: case 0x03c1: case 0x03c6: case 0x03cb:
			case 0x03d0: case 0x03d1: case 0x03d6: case 0x03db:
			case 0x03e0: case 0x03e1: case 0x03e6: case 0x03eb:
			case 0x03f0: case 0x03f1: case 0x03f6: case 0x03fb: {
				unsigned tmp1, tmp2;
				tmp1 = !(op & 0x40) ? REG_E : REG_F;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = byte_immediate(cpu); break;
				case 1: tmp2 = byte_direct(cpu); break;
				case 2: tmp2 = byte_indexed(cpu); break;
				case 3: tmp2 = byte_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				switch (op & 0xf) {
				case 0x0: tmp1 = op_sub(cpu, tmp1, tmp2); break; // SUBE, SUBF
				case 0x1: (void)op_sub(cpu, tmp1, tmp2); break; // CMPE, CMPF
				case 0x6: tmp1 = op_ld(cpu, 0, tmp2); break; // LDE, LDF
				case 0xb: tmp1 = op_add(cpu, tmp1, tmp2); break; // ADDE, ADDF
				default: break;
				}
				if (!(op & 0x40)) {
					REG_E = tmp1;
				} else {
					REG_F = tmp1;
				}
			} break;

			// 0x118d, 0x119d, 0x11ad, 0x11bd DIVD
			case 0x038d: case 0x039d: case 0x03ad: case 0x03bd: {
				uint16_t tmp1 = REG_D;
				uint8_t tmp2;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = byte_immediate(cpu); break;
				case 1: tmp2 = byte_direct(cpu); break;
				case 2: tmp2 = byte_indexed(cpu); break;
				case 3: tmp2 = byte_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				if (tmp2 == 0) {
					REG_MD |= MD_D0;
					CLR_NZV;
					REG_CC |= CC_Z;  // [hoglet67]
					stack_irq_registers(cpu, 1);
					take_interrupt(cpu, CC_F|CC_I, HD6309_INT_VEC_ILLEGAL);
					continue;
				}
				_Bool nsign = 0;  // dividend sign
				_Bool vsign = 0;  // divisor sign
				if ((tmp1 >> 15) & 1) {
					tmp1 = ~tmp1 + 1;
					// Even if calculation is aborted, this
					// negation is reflected in D:
					REG_D = tmp1;
					NVMA_CYCLE;  // [hoglet67]
					nsign = 1;
				}
				if ((tmp2 >> 7) & 1) {
					tmp2 = ~tmp2 + 1;
					NVMA_CYCLE;  // [hoglet67]
					vsign = 1;
				}

				for (int i = 6; i; i--)
					NVMA_CYCLE;
				uint16_t quotient = tmp1 / tmp2;
				uint8_t remainder = tmp1 % tmp2;

				CLR_NZVC;
				if ((quotient >> 8) != 0) {
					// Range overflow
					REG_CC |= CC_V;
					if (nsign)
						REG_CC |= CC_N;
					break;
				}

				// According to [hoglet67] there is now a
				// maximum of 13 cycles remaining, one fewer
				// on 2's complement overflow.

				if (nsign) {
					remainder = ~remainder + 1;
				}
				if (nsign != vsign) {
					uint16_t new_quotient = ~quotient + 1;
					if ((new_quotient & 0x80) && !(quotient & 0x80)) {
						quotient = new_quotient;
					}
					REG_M = 0xff;
				} else {
					REG_M = 0;
				}

				for (int i = 12; i; i--)
					NVMA_CYCLE;
				REG_A = remainder;
				REG_B = quotient;
				REG_CC |= (REG_B & 1);
				if ((((quotient >> 15) ^ (REG_B >> 7)) & 1) == 0) {
					// No overflow, take the extra cycle
					SET_NZ8(REG_B);
					NVMA_CYCLE;
				} else {
					// 2's complement overflow.  NOTE:
					// [hoglet67] says N is clear here, but
					// Tim Lindner's fuzzing suggests
					// otherwise?
					REG_CC |= (CC_N | CC_V);
				}
			} break;

			// 0x118e, 0x119e, 0x11ae, 0x11be DIVQ
			case 0x038e: case 0x039e: case 0x03ae: case 0x03be: {
				uint32_t tmp1 = RREG_Q;
				uint16_t tmp2;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				if (tmp2 == 0) {
					REG_MD |= MD_D0;
					CLR_NZV;
					REG_CC |= CC_Z;  // [hoglet67]
					stack_irq_registers(cpu, 1);
					take_interrupt(cpu, CC_F|CC_I, HD6309_INT_VEC_ILLEGAL);
					continue;
				}
				_Bool nsign = 0;  // dividend sign
				_Bool vsign = 0;  // divisor sign
				if ((tmp1 >> 31) & 1) {
					tmp1 = ~tmp1 + 1;
					// Even if calculation is aborted, this
					// negation is reflected in Q:
					REG_D = tmp1 >> 16;
					REG_W = tmp1 & 0xffff;;
					NVMA_CYCLE;  // [hoglet67]
					nsign = 1;
				}
				if ((tmp2 >> 15) & 1) {
					tmp2 = ~tmp2 + 1;
					NVMA_CYCLE;  // [hoglet67]
					vsign = 1;
				}

				for (int i = 6; i; i--)
					NVMA_CYCLE;
				uint32_t quotient = tmp1 / tmp2;
				uint16_t remainder = tmp1 % tmp2;

				CLR_NZVC;
				if ((quotient >> 16) != 0) {
					// Range overflow
					REG_CC |= CC_V;
					if (nsign)
						REG_CC |= CC_N;
					break;
				}

				// According to [hoglet67] there are now 21
				// cycles remaining.

				if (nsign) {
					remainder = ~remainder + 1;
				}
				if (nsign != vsign) {
					uint32_t new_quotient = ~quotient + 1;
					if ((new_quotient & 0x8000) && !(quotient & 0x8000)) {
						quotient = new_quotient;
					}
					REG_M = 0xff;
				} else {
					REG_M = 0;
				}

				for (int i = 21; i; i--)
					NVMA_CYCLE;
				REG_D = remainder;
				REG_W = quotient;
				REG_CC |= (REG_W & 1);
				if ((((quotient >> 31) ^ (REG_E >> 7)) & 1) == 0) {
					// No overflow
					SET_NZ16(REG_W);
				} else {
					// 2's complement overflow.  NOTE:
					// [hoglet67] says N is clear here, but
					// adopting results of Tim Lindner's
					// fuzzing of DIVD (see above) until we
					// know one way or another.
					REG_CC |= (CC_N | CC_V);
				}
			} break;

			// 0x118f, 0x119f, 0x11af, 0x11bf MULD
			case 0x038f: case 0x039f: case 0x03af: case 0x03bf: {
				uint16_t tmp1, tmp2;
				tmp1 = REG_D;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				int16_t stmp1 = *((int16_t *)&tmp1);
				int16_t stmp2 = *((int16_t *)&tmp2);
				int32_t result = stmp1 * stmp2;
				REG_M = (result < 0) ? 0xff : 0x00;  // [hoglet67]
				uint32_t uresult = *((uint32_t *)&result);
				for (int i = 24; i; i--)
					NVMA_CYCLE;
				REG_D = uresult >> 16;
				REG_W = uresult & 0xffff;
				CLR_NZ;
				SET_N16(REG_D);
				// lower 16 bits (REG_W) ignored [hoglet67]
				if (REG_D == 0)
					REG_CC |= CC_N;
			} break;

			// Illegal instruction
			default:
				// XXX Two dead cycles?  Verify further!
				peek_byte(cpu, cpu->reg_pc);
				peek_byte(cpu, cpu->reg_pc);
				REG_MD |= MD_IL;
				stack_irq_registers(cpu, 1);
				instruction_posthook(cpu);
				take_interrupt(cpu, CC_F|CC_I, HD6309_INT_VEC_ILLEGAL);
				continue;
			}

			break;
			}

		// Not valid states any more:
		case hd6309_state_instruction_page_2:
		case hd6309_state_instruction_page_3:
			break;

		}

		cpu->nmi_active = cpu->nmi_latch;
		cpu->firq_active = cpu->firq_latch;
		cpu->irq_active = cpu->irq_latch;
		instruction_posthook(cpu);
		continue;

	} while (cpu->running);

}

static void hd6309_set_pc(void *sptr, unsigned pc) {
	struct HD6309 *hcpu = sptr;
	struct MC6809 *cpu = &hcpu->mc6809;
	HD6309_TRACE_INSTRUCTION(hcpu);
	REG_PC = pc;
	hcpu->state = hd6309_state_next_instruction;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Data reading & writing
 */

/* Wrap common fetches */

static uint8_t fetch_byte(struct MC6809 *cpu, uint16_t a) {
	uint8_t v = fetch_byte_notrace(cpu, a);
#ifdef TRACE
	struct HD6309 *hcpu = (struct HD6309 *)cpu;
	if (UNLIKELY(logging.trace_cpu)) {
		hd6309_trace_byte(hcpu->tracer, v, a);
	}
#endif
	return v;
}

static uint16_t fetch_word(struct MC6809 *cpu, uint16_t a) {
#ifndef TRACE
	return fetch_word_notrace(cpu, a);
#else
	struct HD6309 *hcpu = (struct HD6309 *)cpu;
	if (LIKELY(!logging.trace_cpu)) {
		return fetch_word_notrace(cpu, a);
	}
	unsigned v0 = fetch_byte_notrace(cpu, a);
	hd6309_trace_byte(hcpu->tracer, v0, a);
	unsigned v1 = fetch_byte_notrace(cpu, a+1);
	hd6309_trace_byte(hcpu->tracer, v1, a+1);
	return (v0 << 8) | v1;
#endif
}

/* Compute effective address */

static uint16_t ea_direct(struct MC6809 *cpu) {
	struct HD6309 *hcpu = (struct HD6309 *)cpu;
	unsigned ea = REG_DP << 8 | fetch_byte(cpu, REG_PC++);
	if (!NATIVE_MODE)
		NVMA_CYCLE;
	return ea;
}

static uint16_t ea_extended(struct MC6809 *cpu) {
	struct HD6309 *hcpu = (struct HD6309 *)cpu;
	unsigned ea = fetch_word(cpu, REG_PC);
	REG_PC += 2;
	if (!NATIVE_MODE)
		NVMA_CYCLE;
	return ea;
}

// Indexed addressing.

// TODO: some undefined postbytes should trigger illegal instruction trap
// [hoglet67]

static uint16_t ea_indexed(struct MC6809 *cpu) {
	struct HD6309 *hcpu = (struct HD6309 *)cpu;
	unsigned ea;
	uint16_t reg;
	unsigned postbyte = byte_immediate(cpu);
	switch ((postbyte >> 5) & 3) {
		case 0: reg = REG_X; break;
		case 1: reg = REG_Y; break;
		case 2: reg = REG_U; break;
		case 3: reg = REG_S; break;
		default: reg = 0; break;
	}
	if ((postbyte & 0x80) == 0) {
		peek_byte(cpu, REG_PC);
		NVMA_CYCLE;
		return reg + sex5(postbyte);
	}
	if (postbyte == 0x8f || postbyte == 0x90) {
		ea = REG_W;
		NVMA_CYCLE;
	} else if (postbyte == 0xaf || postbyte == 0xb0) {
		ea = word_immediate(cpu);
		ea = ea + REG_W;
		NVMA_CYCLE;
	} else if (postbyte == 0xcf || postbyte == 0xd0) {
		ea = REG_W;
		REG_W += 2;
		NVMA_CYCLE;
		NVMA_CYCLE;
	} else if (postbyte == 0xef || postbyte == 0xf0) {
		REG_W -= 2;
		ea = REG_W;
		NVMA_CYCLE;
		NVMA_CYCLE;
	} else switch (postbyte & 0x0f) {
		case 0x00: ea = reg; reg += 1; peek_byte(cpu, REG_PC); NVMA_CYCLE; if (!NATIVE_MODE) NVMA_CYCLE; break;
		case 0x01: ea = reg; reg += 2; peek_byte(cpu, REG_PC); NVMA_CYCLE; NVMA_CYCLE; if (!NATIVE_MODE) NVMA_CYCLE; break;
		case 0x02: reg -= 1; ea = reg; peek_byte(cpu, REG_PC); NVMA_CYCLE; if (!NATIVE_MODE) NVMA_CYCLE; break;
		case 0x03: reg -= 2; ea = reg; peek_byte(cpu, REG_PC); NVMA_CYCLE; NVMA_CYCLE; if (!NATIVE_MODE) NVMA_CYCLE; break;
		case 0x04: ea = reg; peek_byte(cpu, REG_PC); break;
		case 0x05: ea = reg + sex8(REG_B); peek_byte(cpu, REG_PC); NVMA_CYCLE; break;
		case 0x06: ea = reg + sex8(REG_A); peek_byte(cpu, REG_PC); NVMA_CYCLE; break;
		case 0x07: ea = reg + sex8(REG_E); peek_byte(cpu, REG_PC); NVMA_CYCLE; break;
		case 0x08: ea = byte_immediate(cpu); ea = sex8(ea) + reg; NVMA_CYCLE; break;
		case 0x09: ea = word_immediate(cpu); ea = ea + reg; NVMA_CYCLE; NVMA_CYCLE; if (!NATIVE_MODE) NVMA_CYCLE; break;
		case 0x0a: ea = reg + sex8(REG_F); peek_byte(cpu, REG_PC); NVMA_CYCLE; break;
		case 0x0b: ea = reg + REG_D; peek_byte(cpu, REG_PC); peek_byte(cpu, REG_PC + 1); NVMA_CYCLE; if (!NATIVE_MODE) { NVMA_CYCLE; NVMA_CYCLE; } break;
		case 0x0c: ea = byte_immediate(cpu); ea = sex8(ea) + REG_PC; NVMA_CYCLE; break;
		case 0x0d: ea = word_immediate(cpu); ea = ea + REG_PC; peek_byte(cpu, REG_PC); NVMA_CYCLE; if (!NATIVE_MODE) { NVMA_CYCLE; NVMA_CYCLE; } break;
		case 0x0e: ea = reg + REG_W; NVMA_CYCLE; NVMA_CYCLE; break;
		case 0x0f: ea = word_immediate(cpu); if (!NATIVE_MODE) NVMA_CYCLE; break;
		default: ea = 0; break;
	}
	if (postbyte & 0x10) {
		ea = fetch_word_notrace(cpu, ea);
		NVMA_CYCLE;
	}
	switch ((postbyte >> 5) & 3) {
	case 0: REG_X = reg; break;
	case 1: REG_Y = reg; break;
	case 2: REG_U = reg; break;
	case 3: REG_S = reg; break;
	}
	return ea;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Interrupt handling
 */

static void push_irq_registers(struct MC6809 *cpu) {
	struct HD6309 *hcpu = (struct HD6309 *)cpu;
	NVMA_CYCLE;
	push_s_word(cpu, REG_PC);
	push_s_word(cpu, REG_U);
	push_s_word(cpu, REG_Y);
	push_s_word(cpu, REG_X);
	push_s_byte(cpu, REG_DP);
	if (NATIVE_MODE) {
		push_s_byte(cpu, REG_F);
		push_s_byte(cpu, REG_E);
	}
	push_s_byte(cpu, REG_B);
	push_s_byte(cpu, REG_A);
	push_s_byte(cpu, REG_CC);
}

static void push_firq_registers(struct MC6809 *cpu) {
	NVMA_CYCLE;
	push_s_word(cpu, REG_PC);
	push_s_byte(cpu, REG_CC);
}

static void stack_irq_registers(struct MC6809 *cpu, _Bool entire) {
	if (entire) {
		REG_CC |= CC_E;
		push_irq_registers(cpu);
	} else {
		REG_CC &= ~CC_E;
		push_firq_registers(cpu);
	}
}

static void take_interrupt(struct MC6809 *cpu, uint8_t mask, uint16_t vec) {
	struct HD6309 *hcpu = (struct HD6309 *)cpu;
	REG_CC |= mask;
	NVMA_CYCLE;
	hcpu->state = hd6309_state_irq_reset_vector;
	REG_PC = fetch_word(cpu, vec);
	hcpu->state = hd6309_state_label_a;
	NVMA_CYCLE;
}

static void instruction_posthook(struct MC6809 *cpu) {
	DELEGATE_SAFE_CALL(cpu->debug_cpu.instruction_posthook);
}
