/** \file
 *
 *  \brief Motorola MC6809 CPU.
 *
 *  \copyright Copyright 2003-2024 Ciaran Anscomb
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
 *  \par Sources
 *
 *  -  MC6809E data sheet, Motorola
 *
 *  -  MC6809 Cycle-By-Cycle Performance,
 *     http://atjs.great-site.net/mc6809/Information/6809cyc.txt
 *
 *  -  Dragon Update, Illegal Op-codes, Feb 1994 Ciaran Anscomb
 *
 *  -  Motorola 6809 and Hitachi 6309 Programmers Reference,
 *     2009 Darren Atkinson [darrena]
 *
 *  -  Undocumented 6809 Behaviours, David Banks [hoglet67]
 *     https://github.com/hoglet67/6809Decoder/wiki/Undocumented-6809-Behaviours
 *
 *  -  6809 Exchange and Transfer Opcodes
 *     http://tlindner.macmess.org/?p=945
 */

// Additionally, I quite early on made reference to MAME source to understand
// the overflow (V) flag.

// TODO:
//
// - Many more instructions fall through to their unprefixed form after a
//   prefix.
//
// - The store immediate illegal instructions apparently have different flag
//   behaviour when unprefixed.

#include "top-config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"

#include "logging.h"
#include "mc6809.h"
#include "part.h"
#include "serialise.h"

#ifdef TRACE
// Tracing supported:
#include "mc6809_trace.h"
#define MC6809_TRACE_VECTOR(c) if (UNLIKELY(logging.trace_cpu)) { mc6809_trace_vector((c)->tracer); }
#define MC6809_TRACE_INSTRUCTION(c) if (UNLIKELY(logging.trace_cpu)) { mc6809_trace_instruction((c)->tracer); }
#else
// Tracing not supported - no-op macros:
#define MC6809_TRACE_VECTOR(c)
#define MC6809_TRACE_INSTRUCTION(c)
#endif

static const struct ser_struct ser_struct_mc6809[] = {
	SER_ID_STRUCT_ELEM(1, struct MC6809, halt),
	SER_ID_STRUCT_ELEM(2, struct MC6809, nmi),
	SER_ID_STRUCT_ELEM(3, struct MC6809, firq),
	SER_ID_STRUCT_ELEM(4, struct MC6809, irq),
	SER_ID_STRUCT_ELEM(5, struct MC6809, D),

	SER_ID_STRUCT_ELEM(6, struct MC6809, state),
	SER_ID_STRUCT_ELEM(7, struct MC6809, running),
	SER_ID_STRUCT_ELEM(8, struct MC6809, page),

	SER_ID_STRUCT_ELEM(9, struct MC6809, reg_cc),
	SER_ID_STRUCT_ELEM(10, struct MC6809, reg_d),
	SER_ID_STRUCT_ELEM(11, struct MC6809, reg_dp),
	SER_ID_STRUCT_ELEM(12, struct MC6809, reg_x),
	SER_ID_STRUCT_ELEM(13, struct MC6809, reg_y),
	SER_ID_STRUCT_ELEM(14, struct MC6809, reg_u),
	SER_ID_STRUCT_ELEM(15, struct MC6809, reg_s),
	SER_ID_STRUCT_ELEM(16, struct MC6809, reg_pc),

	SER_ID_STRUCT_ELEM(17, struct MC6809, nmi_armed),
	SER_ID_STRUCT_ELEM(18, struct MC6809, nmi_latch),
	SER_ID_STRUCT_ELEM(19, struct MC6809, firq_latch),
	SER_ID_STRUCT_ELEM(20, struct MC6809, irq_latch),
	SER_ID_STRUCT_ELEM(21, struct MC6809, nmi_active),
	SER_ID_STRUCT_ELEM(22, struct MC6809, firq_active),
	SER_ID_STRUCT_ELEM(23, struct MC6809, irq_active),
};

// External, as struct data is also used by 6809-compatible parts:
const struct ser_struct_data mc6809_ser_struct_data = {
        .elems = ser_struct_mc6809,
        .num_elems = ARRAY_N_ELEMENTS(ser_struct_mc6809),
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

extern inline void MC6809_HALT_SET(struct MC6809 *cpu, _Bool val);
extern inline void MC6809_NMI_SET(struct MC6809 *cpu, _Bool val);
extern inline void MC6809_FIRQ_SET(struct MC6809 *cpu, _Bool val);
extern inline void MC6809_IRQ_SET(struct MC6809 *cpu, _Bool val);

/*
 * External interface
 */

static void mc6809_set_pc(void *sptr, unsigned pc);

static void mc6809_reset(struct MC6809 *cpu);
static void mc6809_run(struct MC6809 *cpu);

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
static void stack_irq_registers(struct MC6809 *cpu);
static void stack_firq_registers(struct MC6809 *cpu);
static void take_interrupt(struct MC6809 *cpu, uint8_t mask, uint16_t vec);
static void instruction_posthook(struct MC6809 *cpu);

/*
 * ALU operations
 */

/* Illegal 6809 8-bit arithmetic operations */

static uint8_t op_discard(struct MC6809 *cpu, uint8_t a, uint8_t b);
static uint8_t op_xdec(struct MC6809 *cpu, uint8_t in);
static uint8_t op_xclr(struct MC6809 *cpu, uint8_t in);

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

/*
 * Common 6809 functions
 */

#define STRUCT_CPU struct MC6809

#include "mc6809_common.c"
#include "mc680x/mc680x_ops.c"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// MC6809 part creation

static struct part *mc6809_allocate(void);
static void mc6809_initialise(struct part *p, void *options);
static void mc6809_free(struct part *p);

static const struct partdb_entry_funcs mc6809_funcs = {
	.allocate = mc6809_allocate,
	.initialise = mc6809_initialise,
	.free = mc6809_free,

	.ser_struct_data = &mc6809_ser_struct_data,

	.is_a = mc6809_is_a,
};

const struct partdb_entry mc6809_part = { .name = "MC6809", .description = "Motorola | MC6809E CPU", .funcs = &mc6809_funcs };

static struct part *mc6809_allocate(void) {
	struct MC6809 *cpu = part_new(sizeof(*cpu));
	struct part *p = &cpu->debug_cpu.part;

	*cpu = (struct MC6809){0};

	cpu->debug_cpu.get_pc = DELEGATE_AS0(unsigned, mc6809_get_pc, cpu);
	cpu->debug_cpu.set_pc = DELEGATE_AS1(void, unsigned, mc6809_set_pc, cpu);

	cpu->reset = mc6809_reset;
	cpu->run = mc6809_run;
	cpu->mem_cycle = DELEGATE_DEFAULT2(void, bool, uint16);

	// Tested: (almost?) always, all registers are zeroed on power on.
	//
	// CC has F and I set as part of reset.  DP is explicitly cleared on
	// reset, but other registers are left untouched.

#ifdef TRACE
	// Tracing
	cpu->tracer = mc6809_trace_new(cpu);
#endif

	return p;
}

static void mc6809_initialise(struct part *p, void *options) {
	(void)options;
	struct MC6809 *cpu = (struct MC6809 *)p;
	mc6809_reset(cpu);
}

static void mc6809_free(struct part *p) {
	(void)p;
#ifdef TRACE
	struct MC6809 *cpu = (struct MC6809 *)p;
	if (cpu->tracer) {
		mc6809_trace_free(cpu->tracer);
	}
#endif
}

_Bool mc6809_is_a(struct part *p, const char *name) {
	(void)p;
        return strcmp(name, "DEBUG-CPU") == 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

unsigned mc6809_get_pc(void *sptr) {
	struct MC6809 *cpu = sptr;
	return cpu->reg_pc;
}

static void mc6809_reset(struct MC6809 *cpu) {
	cpu->halt = cpu->nmi = 0;
	cpu->nmi_armed = 0;
	cpu->nmi = cpu->nmi_latch = cpu->nmi_active = 0;
	cpu->firq = cpu->firq_latch = cpu->firq_active = 0;
	cpu->irq = cpu->irq_latch = cpu->irq_active = 0;
	cpu->state = mc6809_state_reset;
}

/* Run CPU while cpu->running is true. */

static void mc6809_run(struct MC6809 *cpu) {

	do {

		switch (cpu->state) {

		case mc6809_state_reset:
			REG_DP = 0;
			REG_CC |= (CC_F | CC_I);
			cpu->nmi_armed = 0;
			cpu->nmi = 0;
			cpu->nmi_active = 0;
			cpu->firq_active = 0;
			cpu->irq_active = 0;
			cpu->state = mc6809_state_reset_check_halt;
			MC6809_TRACE_VECTOR(cpu);
			// fall through

		case mc6809_state_reset_check_halt:
			if (!cpu->halt) {
				take_interrupt(cpu, 0, MC6809_INT_VEC_RESET);
			} else {
				NVMA_CYCLE;
			}
			continue;

		// done_instruction case for backwards-compatibility
		case mc6809_state_done_instruction:
		case mc6809_state_label_a:
			if (cpu->halt) {
				NVMA_CYCLE;
				continue;
			}
			cpu->state = mc6809_state_label_b;
			// fall through

		case mc6809_state_label_b:
			if (UNLIKELY(cpu->nmi_active)) {
				MC6809_TRACE_VECTOR(cpu);
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu);
				cpu->state = mc6809_state_dispatch_irq;
			} else if (UNLIKELY(!(REG_CC & CC_F) && cpu->firq_active)) {
				MC6809_TRACE_VECTOR(cpu);
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_PC);
				stack_firq_registers(cpu);
				cpu->state = mc6809_state_dispatch_irq;
			} else if (UNLIKELY(!(REG_CC & CC_I) && cpu->irq_active)) {
				MC6809_TRACE_VECTOR(cpu);
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu);
				cpu->state = mc6809_state_dispatch_irq;
			} else {
				MC6809_TRACE_INSTRUCTION(cpu);
				cpu->state = mc6809_state_next_instruction;
				cpu->page = 0;
				// Instruction fetch hook called here so that machine
				// can be stopped beforehand.
				DELEGATE_SAFE_CALL(cpu->debug_cpu.instruction_hook);
			}
			continue;

		case mc6809_state_dispatch_irq:
			if (cpu->nmi_active) {
				cpu->nmi_active = cpu->nmi = cpu->nmi_latch = 0;
				take_interrupt(cpu, CC_F|CC_I, MC6809_INT_VEC_NMI);
			} else if (!(REG_CC & CC_F) && cpu->firq_active) {
				take_interrupt(cpu, CC_F|CC_I, MC6809_INT_VEC_FIRQ);
			} else if (!(REG_CC & CC_I) && cpu->irq_active) {
				take_interrupt(cpu, CC_I, MC6809_INT_VEC_IRQ);
			} else {
				cpu->state = mc6809_state_cwai_check_halt;
			}
			continue;

		case mc6809_state_cwai_check_halt:
			cpu->nmi_active = cpu->nmi_latch;
			cpu->firq_active = cpu->firq_latch;
			cpu->irq_active = cpu->irq_latch;
			NVMA_CYCLE;
			if (!cpu->halt) {
				cpu->state = mc6809_state_dispatch_irq;
			}
			continue;

		case mc6809_state_sync:
			if (cpu->nmi_active || cpu->firq_active || cpu->irq_active) {
				NVMA_CYCLE;
				instruction_posthook(cpu);
				cpu->state = mc6809_state_label_b;
				continue;
			}
			cpu->nmi_active = cpu->nmi_latch;
			cpu->firq_active = cpu->firq_latch;
			cpu->irq_active = cpu->irq_latch;
			NVMA_CYCLE;
			if (cpu->halt) {
				cpu->state = mc6809_state_sync_check_halt;
			}
			continue;

		case mc6809_state_sync_check_halt:
			NVMA_CYCLE;
			if (!cpu->halt) {
				cpu->state = mc6809_state_sync;
			}
			continue;

		case mc6809_state_next_instruction: {
			unsigned op;
			// Fetch op-code and process
			op = byte_immediate(cpu);
			op |= cpu->page;
			cpu->state = mc6809_state_label_a;
			switch (op) {

			// 0x00 - 0x0f direct mode ops
			// 0x40 - 0x4f inherent A register ops
			// 0x50 - 0x5f inherent B register ops
			// 0x60 - 0x6f indexed mode ops
			// 0x70 - 0x7f extended mode ops
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x00: case 0x01: case 0x02: case 0x03:
			case 0x04: case 0x05: case 0x06: case 0x07:
			case 0x08: case 0x09: case 0x0a: case 0x0b:
			case 0x0c: case 0x0d: case 0x0f:
			case 0x40: case 0x41: case 0x42: case 0x43:
			case 0x44: case 0x45: case 0x46: case 0x47:
			case 0x48: case 0x49: case 0x4a: case 0x4b:
			case 0x4c: case 0x4d: case 0x4e: case 0x4f:
			case 0x50: case 0x51: case 0x52: case 0x53:
			case 0x54: case 0x55: case 0x56: case 0x57:
			case 0x58: case 0x59: case 0x5a: case 0x5b:
			case 0x5c: case 0x5d: case 0x5e: case 0x5f:
			case 0x60: case 0x61: case 0x62: case 0x63:
			case 0x64: case 0x65: case 0x66: case 0x67:
			case 0x68: case 0x69: case 0x6a: case 0x6b:
			case 0x6c: case 0x6d: case 0x6f:
			case 0x70: case 0x71: case 0x72: case 0x73:
			case 0x74: case 0x75: case 0x76: case 0x77:
			case 0x78: case 0x79: case 0x7a: case 0x7b:
			case 0x7c: case 0x7d: case 0x7f:

			case 0x0200: case 0x0201: case 0x0202: case 0x0203:
			case 0x0204: case 0x0205: case 0x0206: case 0x0207:
			case 0x0208: case 0x0209: case 0x020a: case 0x020b:
			case 0x020c: case 0x020d: case 0x020f:
			case 0x0240: case 0x0241: case 0x0242: case 0x0243:
			case 0x0244: case 0x0245: case 0x0246: case 0x0247:
			case 0x0248: case 0x0249: case 0x024a: case 0x024b:
			case 0x024c: case 0x024d: case 0x024e: case 0x024f:
			case 0x0250: case 0x0251: case 0x0252: case 0x0253:
			case 0x0254: case 0x0255: case 0x0256: case 0x0257:
			case 0x0258: case 0x0259: case 0x025a: case 0x025b:
			case 0x025c: case 0x025d: case 0x025e: case 0x025f:
			case 0x0260: case 0x0261: case 0x0262: case 0x0263:
			case 0x0264: case 0x0265: case 0x0266: case 0x0267:
			case 0x0268: case 0x0269: case 0x026a: case 0x026b:
			case 0x026c: case 0x026d: case 0x026f:
			case 0x0270: case 0x0271: case 0x0272: case 0x0273:
			case 0x0274: case 0x0275: case 0x0276: case 0x0277:
			case 0x0278: case 0x0279: case 0x027a: case 0x027b:
			case 0x027c: case 0x027d: case 0x027f:

			case 0x0300: case 0x0301: case 0x0302: case 0x0303:
			case 0x0304: case 0x0305: case 0x0306: case 0x0307:
			case 0x0308: case 0x0309: case 0x030a: case 0x030b:
			case 0x030c: case 0x030d: case 0x030f:
			case 0x0340: case 0x0341: case 0x0342: case 0x0343:
			case 0x0344: case 0x0345: case 0x0346: case 0x0347:
			case 0x0348: case 0x0349: case 0x034a: case 0x034b:
			case 0x034c: case 0x034d: case 0x034e: case 0x034f:
			case 0x0350: case 0x0351: case 0x0352: case 0x0353:
			case 0x0354: case 0x0355: case 0x0356: case 0x0357:
			case 0x0358: case 0x0359: case 0x035a: case 0x035b:
			case 0x035c: case 0x035d: case 0x035e: case 0x035f:
			case 0x0360: case 0x0361: case 0x0362: case 0x0363:
			case 0x0364: case 0x0365: case 0x0366: case 0x0367:
			case 0x0368: case 0x0369: case 0x036a: case 0x036b:
			case 0x036c: case 0x036d: case 0x036f:
			case 0x0370: case 0x0371: case 0x0372: case 0x0373:
			case 0x0374: case 0x0375: case 0x0376: case 0x0377:
			case 0x0378: case 0x0379: case 0x037a: case 0x037b:
			case 0x037c: case 0x037d: case 0x037f: {

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
				case 0x1: // NEG illegal
				case 0x0: tmp1 = op_neg(cpu, tmp1); break; // NEG, NEGA, NEGB
				case 0x2: tmp1 = op_ngc(cpu, tmp1); break; // NGC*,NGCA*,NGCB*
				case 0x3: tmp1 = op_com(cpu, tmp1); break; // COM, COMA, COMB
				case 0x5: // LSR illegal
				case 0x4: tmp1 = op_lsr(cpu, tmp1); break; // LSR, LSRA, LSRB
				case 0x6: tmp1 = op_ror(cpu, tmp1); break; // ROR, RORA, RORB
				case 0x7: tmp1 = op_asr(cpu, tmp1); break; // ASR, ASRA, ASRB
				case 0x8: tmp1 = op_asl(cpu, tmp1); break; // ASL, ASLA, ASLB
				case 0x9: tmp1 = op_rol(cpu, tmp1); break; // ROL, ROLA, ROLB
				case 0xa: tmp1 = op_dec(cpu, tmp1); break; // DEC, DECA, DECB
				case 0xb: // DEC illegal [hoglet67]
					  tmp1 = op_xdec(cpu, tmp1); break;
				case 0xc: tmp1 = op_inc(cpu, tmp1); break; // INC, INCA, INCB
				case 0xd: tmp1 = op_tst(cpu, tmp1); break; // TST, TSTA, TSTB
				case 0xe: // CLRA, CLRB illegal [hoglet67]
					tmp1 = op_xclr(cpu, tmp1); break;
				case 0xf: tmp1 = op_clr(cpu, tmp1); break; // CLR, CLRA, CLRB
				default: break;
				}
				switch (op & 0xf) {
				case 0xd: // TST
					NVMA_CYCLE;
					NVMA_CYCLE;
					break;
				default: // the rest need storing
					switch ((op >> 4) & 0xf) {
					default:
					case 0x0: case 0x6: case 0x7:
						NVMA_CYCLE;
						store_byte(cpu, ea, tmp1);
						break;
					case 0x4:
						REG_A = tmp1;
						peek_byte(cpu, REG_PC);
						break;
					case 0x5:
						REG_B = tmp1;
						peek_byte(cpu, REG_PC);
						break;
					}
				}
			} break;

			// 0x0e JMP direct
			// 0x6e JMP indexed
			// 0x7e JMP extended
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x0e: case 0x6e: case 0x7e:
			case 0x020e: case 0x026e: case 0x027e:
			case 0x030e: case 0x036e: case 0x037e: {
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
				cpu->state = mc6809_state_next_instruction;
				cpu->page = 0x200;
				continue;

			// 0x11 Page 3
			// 0x1110, 0x1111 Page 3
			case 0x0310:
			case 0x0311:
			case 0x11:
				cpu->state = mc6809_state_next_instruction;
				cpu->page = 0x300;
				continue;

			// 0x12 NOP inherent
			// 0x1b NOP inherent (illegal)
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x12:
			case 0x1b:
			case 0x0212:
			case 0x021b:
			case 0x0312:
			case 0x031b:
				peek_byte(cpu, REG_PC);
				break;

			// 0x13 SYNC inherent
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x13:
			case 0x0213:
			case 0x0313:
				peek_byte(cpu, REG_PC);
				cpu->nmi_active = cpu->nmi_latch;
				cpu->firq_active = cpu->firq_latch;
				cpu->irq_active = cpu->irq_latch;
				instruction_posthook(cpu);
				cpu->state = mc6809_state_sync;
				continue;

			// 0x14, 0x15 HCF? (illegal)
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x14: case 0x15: case 0xcd:
			case 0x0214: case 0x0215: case 0x02cd:
			case 0x0314: case 0x0315: case 0x03cd:
				cpu->state = mc6809_state_hcf;
				break;

			// 0x16 LBRA relative
			// 0x1016, 0x1116 - page 2/3 fallthrough, illegal
			// 0x108d, 0x118d - LBRA observed by darrena on discord
			case 0x16:
			case 0x0216: case 0x028d:
			case 0x0316: case 0x038d: {
				uint16_t ea = long_relative(cpu);
				REG_PC += ea;
				NVMA_CYCLE;
				NVMA_CYCLE;
			} break;

			// 0x17 LBSR relative
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x17:
			case 0x0217:
			case 0x0317: {
				uint16_t ea = long_relative(cpu);
				ea += REG_PC;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				push_s_word(cpu, REG_PC);
				REG_PC = ea;
			} break;

			// 0x18 Shift CC with mask inherent (illegal)
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			// Behaviour as defined in [hoglet67]
			case 0x18:
			case 0x0218:
			case 0x0318: {
				unsigned data = fetch_byte_notrace(cpu, REG_PC);
				REG_CC = (REG_CC & data) << 1;
				REG_CC |= (REG_CC >> 2) & 0x02;
				NVMA_CYCLE;
			} break;

			// 0x19 DAA inherent
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x19:
			case 0x0219:
			case 0x0319:
				REG_A = op_daa(cpu, REG_A);
				peek_byte(cpu, REG_PC);
				break;

			// 0x1a ORCC immediate
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x1a:
			case 0x021a:
			case 0x031a: {
				unsigned data = byte_immediate(cpu);
				REG_CC |= data;
				peek_byte(cpu, REG_PC);
			} break;

			// 0x1c ANDCC immediate
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x1c:
			case 0x021c:
			case 0x031c: {
				unsigned data = byte_immediate(cpu);
				REG_CC &= data;
				peek_byte(cpu, REG_PC);
			} break;

			// 0x1d SEX inherent
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x1d:
			case 0x021d:
			case 0x031d:
				REG_D = sex8(REG_B);
				CLR_NZ;
				SET_NZ16(REG_D);
				peek_byte(cpu, REG_PC);
				break;

			// 0x1e EXG immediate
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x1e:
			case 0x021e:
			case 0x031e: {
				uint16_t tmp1, tmp2;
				unsigned postbyte = byte_immediate(cpu);
				switch (postbyte >> 4) {
					case 0x0: tmp1 = REG_D; break;
					case 0x1: tmp1 = REG_X; break;
					case 0x2: tmp1 = REG_Y; break;
					case 0x3: tmp1 = REG_U; break;
					case 0x4: tmp1 = REG_S; break;
					case 0x5: tmp1 = REG_PC; break;
					case 0x8: tmp1 = REG_A | 0xff00; break;
					case 0x9: tmp1 = REG_B | 0xff00; break;
					case 0xa: tmp1 = (REG_CC << 8) | REG_CC; break;
					case 0xb: tmp1 = (REG_DP << 8) | REG_DP; break;
					default: tmp1 = 0xffff; break;
				}
				switch (postbyte & 0xf) {
					case 0x0: tmp2 = REG_D; REG_D = tmp1; break;
					case 0x1: tmp2 = REG_X; REG_X = tmp1; break;
					case 0x2: tmp2 = REG_Y; REG_Y = tmp1; break;
					case 0x3: tmp2 = REG_U; REG_U = tmp1; break;
					case 0x4: tmp2 = REG_S; REG_S = tmp1; break;
					case 0x5: tmp2 = REG_PC; REG_PC = tmp1; break;
					case 0x8: tmp2 = REG_A | 0xff00; REG_A = tmp1; break;
					case 0x9: tmp2 = REG_B | 0xff00; REG_B = tmp1; break;
					case 0xa: tmp2 = REG_CC | 0xff00; REG_CC = tmp1; break;
					case 0xb: tmp2 = REG_DP | 0xff00; REG_DP = tmp1; break;
					default: tmp2 = 0xffff; break;
				}
				switch (postbyte >> 4) {
					case 0x0: REG_D = tmp2; break;
					case 0x1: REG_X = tmp2; break;
					case 0x2: REG_Y = tmp2; break;
					case 0x3: REG_U = tmp2; break;
					case 0x4: REG_S = tmp2; break;
					case 0x5: REG_PC = tmp2; break;
					case 0x8: REG_A = tmp2; break;
					case 0x9: REG_B = tmp2; break;
					case 0xa: REG_CC = tmp2; break;
					case 0xb: REG_DP = tmp2; break;
					default: break;
				}
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
			} break;

			// 0x1f TFR immediate
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x1f:
			case 0x021f:
			case 0x031f: {
				uint16_t tmp1;
				unsigned postbyte = byte_immediate(cpu);
				switch (postbyte >> 4) {
					case 0x0: tmp1 = REG_D; break;
					case 0x1: tmp1 = REG_X; break;
					case 0x2: tmp1 = REG_Y; break;
					case 0x3: tmp1 = REG_U; break;
					case 0x4: tmp1 = REG_S; break;
					case 0x5: tmp1 = REG_PC; break;
					case 0x8: tmp1 = REG_A | 0xff00; break;
					case 0x9: tmp1 = REG_B | 0xff00; break;
					case 0xa: tmp1 = (REG_CC << 8) | REG_CC; break;
					case 0xb: tmp1 = (REG_DP << 8) | REG_DP; break;
					default: tmp1 = 0xffff; break;
				}
				switch (postbyte & 0xf) {
					case 0x0: REG_D = tmp1; break;
					case 0x1: REG_X = tmp1; break;
					case 0x2: REG_Y = tmp1; break;
					case 0x3: REG_U = tmp1; break;
					case 0x4: REG_S = tmp1; break;
					case 0x5: REG_PC = tmp1; break;
					case 0x8: REG_A = tmp1; break;
					case 0x9: REG_B = tmp1; break;
					case 0xa: REG_CC = tmp1; break;
					case 0xb: REG_DP = tmp1; break;
					default: break;
				}
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
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
			// 0x1030 LEAX indexed, illegal
			case 0x30:
			case 0x0230:
				REG_X = ea_indexed(cpu);
				CLR_Z;
				SET_Z16(REG_X);
				NVMA_CYCLE;
				break;

			// 0x31 LEAY indexed
			// 0x1031 LEAY indexed, illegal
			case 0x31:
			case 0x0231:
				REG_Y = ea_indexed(cpu);
				CLR_Z;
				SET_Z16(REG_Y);
				NVMA_CYCLE;
				break;

			// 0x32 LEAS indexed
			// 0x1032 LEAS indexed, illegal
			case 0x32:
			case 0x0232:
				REG_S = ea_indexed(cpu);
				NVMA_CYCLE;
				cpu->nmi_armed = 1;  // XXX: Really?
				break;

			// 0x33 LEAU indexed
			// 0x1033 LEAU indexed, illegal
			case 0x33:
			case 0x0233:
				REG_U = ea_indexed(cpu);
				NVMA_CYCLE;
				break;

			// 0x34 PSHS immediate
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x34:
			case 0x0234:
			case 0x0334:
				{
					unsigned postbyte = byte_immediate(cpu);
					NVMA_CYCLE;
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
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x35:
			case 0x0235:
			case 0x0335:
				{
					unsigned postbyte = byte_immediate(cpu);
					NVMA_CYCLE;
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
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x36:
			case 0x0236:
			case 0x0336:
				{
					unsigned postbyte = byte_immediate(cpu);
					NVMA_CYCLE;
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
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x37:
			case 0x0237:
			case 0x0337:
				{
					unsigned postbyte = byte_immediate(cpu);
					NVMA_CYCLE;
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

			// 0x38 ANDCC immediate, illegal
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x38:
			case 0x0238:
			case 0x0338: {
				unsigned data = byte_immediate(cpu);
				REG_CC &= data;
				peek_byte(cpu, REG_PC);
				/* Differs from legal 0x1c version by
				 * taking one more cycle: */
				NVMA_CYCLE;
			} break;

			// 0x39 RTS inherent
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x39:
			case 0x0239:
			case 0x0339:
				peek_byte(cpu, REG_PC);
				REG_PC = pull_s_word(cpu);
				NVMA_CYCLE;
				break;

			// 0x3a ABX inherent
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x3a:
			case 0x023a:
			case 0x033a:
				REG_X += REG_B;
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				break;

			// 0x3b RTI inherent
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x3b:
			case 0x023b:
			case 0x033b:
				peek_byte(cpu, REG_PC);
				REG_CC = pull_s_byte(cpu);
				if (REG_CC & CC_E) {
					REG_A = pull_s_byte(cpu);
					REG_B = pull_s_byte(cpu);
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
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x3c:
			case 0x023c:
			case 0x033c: {
				unsigned data = byte_immediate(cpu);
				REG_CC &= data;
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				stack_irq_registers(cpu);
				NVMA_CYCLE;
				cpu->state = mc6809_state_dispatch_irq;
			} break;

			// 0x3d MUL inherent
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x3d:
			case 0x023d:
			case 0x033d: {
				unsigned tmp = REG_A * REG_B;
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
				NVMA_CYCLE;
			} break;

			// 0x3e RESET inherent, illegal
			// [hoglet67] F and I not set
			case 0x3e:
				peek_byte(cpu, REG_PC);
				push_irq_registers(cpu);
				instruction_posthook(cpu);
				take_interrupt(cpu, 0, MC6809_INT_VEC_RESET);
				continue;

			// 0x3f SWI inherent
			case 0x3f:
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu);
				instruction_posthook(cpu);
				take_interrupt(cpu, CC_F|CC_I, MC6809_INT_VEC_SWI);
				continue;

			// 0x80 - 0xbf A register arithmetic ops
			// 0xc0 - 0xff B register arithmetic ops
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x80: case 0x81: case 0x82:
			case 0x84: case 0x85: case 0x86: case 0x87:
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
			case 0xc4: case 0xc5: case 0xc6: case 0xc7:
			case 0xc8: case 0xc9: case 0xca: case 0xcb:
			case 0xd0: case 0xd1: case 0xd2:
			case 0xd4: case 0xd5: case 0xd6:
			case 0xd8: case 0xd9: case 0xda: case 0xdb:
			case 0xe0: case 0xe1: case 0xe2:
			case 0xe4: case 0xe5: case 0xe6:
			case 0xe8: case 0xe9: case 0xea: case 0xeb:
			case 0xf0: case 0xf1: case 0xf2:
			case 0xf4: case 0xf5: case 0xf6:
			case 0xf8: case 0xf9: case 0xfa: case 0xfb:

			case 0x0280: case 0x0281: case 0x0282:
			case 0x0284: case 0x0285: case 0x0286: case 0x0287:
			case 0x0288: case 0x0289: case 0x028a: case 0x028b:
			case 0x0290: case 0x0291: case 0x0292:
			case 0x0294: case 0x0295: case 0x0296:
			case 0x0298: case 0x0299: case 0x029a: case 0x029b:
			case 0x02a0: case 0x02a1: case 0x02a2:
			case 0x02a4: case 0x02a5: case 0x02a6:
			case 0x02a8: case 0x02a9: case 0x02aa: case 0x02ab:
			case 0x02b0: case 0x02b1: case 0x02b2:
			case 0x02b4: case 0x02b5: case 0x02b6:
			case 0x02b8: case 0x02b9: case 0x02ba: case 0x02bb:
			case 0x02c0: case 0x02c1: case 0x02c2:
			case 0x02c4: case 0x02c5: case 0x02c6: case 0x02c7:
			case 0x02c8: case 0x02c9: case 0x02ca: case 0x02cb:
			case 0x02d0: case 0x02d1: case 0x02d2:
			case 0x02d4: case 0x02d5: case 0x02d6:
			case 0x02d8: case 0x02d9: case 0x02da: case 0x02db:
			case 0x02e0: case 0x02e1: case 0x02e2:
			case 0x02e4: case 0x02e5: case 0x02e6:
			case 0x02e8: case 0x02e9: case 0x02ea: case 0x02eb:
			case 0x02f0: case 0x02f1: case 0x02f2:
			case 0x02f4: case 0x02f5: case 0x02f6:
			case 0x02f8: case 0x02f9: case 0x02fa: case 0x02fb:

			case 0x0380: case 0x0381: case 0x0382:
			case 0x0384: case 0x0385: case 0x0386: case 0x0387:
			case 0x0388: case 0x0389: case 0x038a: case 0x038b:
			case 0x0390: case 0x0391: case 0x0392:
			case 0x0394: case 0x0395: case 0x0396:
			case 0x0398: case 0x0399: case 0x039a: case 0x039b:
			case 0x03a0: case 0x03a1: case 0x03a2:
			case 0x03a4: case 0x03a5: case 0x03a6:
			case 0x03a8: case 0x03a9: case 0x03aa: case 0x03ab:
			case 0x03b0: case 0x03b1: case 0x03b2:
			case 0x03b4: case 0x03b5: case 0x03b6:
			case 0x03b8: case 0x03b9: case 0x03ba: case 0x03bb:
			case 0x03c0: case 0x03c1: case 0x03c2:
			case 0x03c4: case 0x03c5: case 0x03c6: case 0x03c7:
			case 0x03c8: case 0x03c9: case 0x03ca: case 0x03cb:
			case 0x03d0: case 0x03d1: case 0x03d2:
			case 0x03d4: case 0x03d5: case 0x03d6:
			case 0x03d8: case 0x03d9: case 0x03da: case 0x03db:
			case 0x03e0: case 0x03e1: case 0x03e2:
			case 0x03e4: case 0x03e5: case 0x03e6:
			case 0x03e8: case 0x03e9: case 0x03ea: case 0x03eb:
			case 0x03f0: case 0x03f1: case 0x03f2:
			case 0x03f4: case 0x03f5: case 0x03f6:
			case 0x03f8: case 0x03f9: case 0x03fa: case 0x03fb: {

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
				case 0x7: tmp1 = op_discard(cpu, tmp1, tmp2); break; // illegal
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
				NVMA_CYCLE;
			} break;

			// 0x10c3, 0x10d3, 0x10e3, 0x10f3 XADDD, illegal [hoglet67]
			case 0x02c3: case 0x02d3: case 0x02e3: case 0x02f3: {
				unsigned tmp1 = REG_D;
				unsigned tmp2;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				(void)op_add16(cpu, tmp1, tmp2);
				NVMA_CYCLE;
			} break;

			// 0x11c3, 0x11d3, 0x11e3, 0x11f3 XADDU, illegal [hoglet67]
			case 0x03c3: case 0x03d3: case 0x03e3: case 0x03f3: {
				unsigned tmp1 = REG_U | 0xff00;
				unsigned tmp2;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				(void)op_add16(cpu, tmp1, tmp2);
				NVMA_CYCLE;
			} break;

			// 0x8d BSR
			// 0x9d, 0xad, 0xbd JSR
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x8d: case 0x9d: case 0xad: case 0xbd:
			case 0x029d: case 0x02ad: case 0x02bd:
			case 0x039d: case 0x03ad: case 0x03bd: {
				unsigned ea;
				switch ((op >> 4) & 3) {
				case 0: ea = short_relative(cpu); ea += REG_PC; NVMA_CYCLE; NVMA_CYCLE; NVMA_CYCLE; break;
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
			// 0x108e, 0x109e, 0x10ae, 0x10be LDY
			// 0x10ce, 0x10de, 0x10ee, 0x10fe LDS
			case 0x8e: case 0x9e: case 0xae: case 0xbe:
			case 0xcc: case 0xdc: case 0xec: case 0xfc:
			case 0xce: case 0xde: case 0xee: case 0xfe:
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
				case 0x020e: REG_Y = tmp1; break;
				case 0x024e: REG_S = tmp1; cpu->nmi_armed = 1; break;
				}
			} break;

			// 0x8f STX immediate, illegal
			// 0xcf STU immediate, illegal
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			// Illegal instruction only part working
			case 0x8f: case 0xcf:
			case 0x028f: case 0x02cf:
			case 0x038f: case 0x03cf: {
				unsigned tmp1;
				tmp1 = !(op & 0x40) ? REG_X : REG_U;
				(void)fetch_byte_notrace(cpu, REG_PC);
				REG_PC++;
				store_byte(cpu, REG_PC, tmp1);
				REG_PC++;
				CLR_NZV;
				REG_CC |= CC_N;
			} break;

			// 0x97, 0xa7, 0xb7 STA
			// 0xd7, 0xe7, 0xf7 STB
			// 0x10xx, 0x11xx - page 2/3 fallthrough, illegal
			case 0x97: case 0xa7: case 0xb7:
			case 0xd7: case 0xe7: case 0xf7:
			case 0x0297: case 0x02a7: case 0x02b7:
			case 0x02d7: case 0x02e7: case 0x02f7:
			case 0x0397: case 0x03a7: case 0x03b7:
			case 0x03d7: case 0x03e7: case 0x03f7: {
				uint16_t ea;
				uint8_t tmp1;
				tmp1 = !(op & 0x40) ? REG_A : REG_B;
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
			// 0x109f, 0x10af, 0x10bf STY
			// 0x10df, 0x10ef, 0x10ff STS
			case 0x9f: case 0xaf: case 0xbf:
			case 0xdd: case 0xed: case 0xfd:
			case 0xdf: case 0xef: case 0xff:
			case 0x029f: case 0x02af: case 0x02bf:
			case 0x02df: case 0x02ef: case 0x02ff: {
				uint16_t ea, tmp1;
				switch (op & 0x034e) {
				default:
				case 0x000e: tmp1 = REG_X; break;
				case 0x004c: tmp1 = REG_D; break;
				case 0x004e: tmp1 = REG_U; break;
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

			// 0x1020 - 0x102f long branches
			case 0x0220: case 0x0221: case 0x0222: case 0x0223:
			case 0x0224: case 0x0225: case 0x0226: case 0x0227:
			case 0x0228: case 0x0229: case 0x022a: case 0x022b:
			case 0x022c: case 0x022d: case 0x022e: case 0x022f: {
				unsigned tmp = word_immediate(cpu);
				if (branch_condition(cpu, op)) {
					REG_PC += tmp;
					NVMA_CYCLE;
				}
				NVMA_CYCLE;
			} break;

			// 0x103e SWI2 inherent, illegal
			case 0x023e:
				peek_byte(cpu, REG_PC);
				push_irq_registers(cpu);
				instruction_posthook(cpu);
				take_interrupt(cpu, 0, MC6809_INT_VEC_SWI2);
				continue;

			// 0x103f SWI2 inherent
			case 0x023f:
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu);
				instruction_posthook(cpu);
				take_interrupt(cpu, 0, MC6809_INT_VEC_SWI2);
				continue;

			// 0x113e FIRQ inherent, illegal [hoglet67]
			case 0x033e:
				peek_byte(cpu, REG_PC);
				push_irq_registers(cpu);
				instruction_posthook(cpu);
				take_interrupt(cpu, 0, MC6809_INT_VEC_FIRQ);
				continue;

			// 0x113f SWI3 inherent
			case 0x033f:
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu);
				instruction_posthook(cpu);
				take_interrupt(cpu, 0, MC6809_INT_VEC_SWI3);
				continue;

			// Illegal instruction
			default:
				NVMA_CYCLE;
				break;
			}

		} break;

		// Certain illegal instructions cause the CPU to lock up:
		case mc6809_state_hcf:
			NVMA_CYCLE;
			continue;

		// Not valid states any more:
		case mc6809_state_instruction_page_2:
		case mc6809_state_instruction_page_3:
			break;

		}

		cpu->nmi_active = cpu->nmi_latch;
		cpu->firq_active = cpu->firq_latch;
		cpu->irq_active = cpu->irq_latch;
		instruction_posthook(cpu);
		continue;

	} while (cpu->running);

}

static void mc6809_set_pc(void *sptr, unsigned pc) {
	struct MC6809 *cpu = sptr;
	MC6809_TRACE_INSTRUCTION(cpu);
	REG_PC = pc;
	cpu->state = mc6809_state_next_instruction;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Data reading & writing
 */

/* Wrap common fetches */

static uint8_t fetch_byte(struct MC6809 *cpu, uint16_t a) {
	uint8_t v = fetch_byte_notrace(cpu, a);
#ifdef TRACE
	if (UNLIKELY(logging.trace_cpu)) {
		mc6809_trace_byte(cpu->tracer, v, a);
	}
#endif
	return v;
}

static uint16_t fetch_word(struct MC6809 *cpu, uint16_t a) {
#ifndef TRACE
	return fetch_word_notrace(cpu, a);
#else
	if (LIKELY(!logging.trace_cpu)) {
		return fetch_word_notrace(cpu, a);
	}
	unsigned v0 = fetch_byte_notrace(cpu, a);
	mc6809_trace_byte(cpu->tracer, v0, a);
	unsigned v1 = fetch_byte_notrace(cpu, a+1);
	mc6809_trace_byte(cpu->tracer, v1, a+1);
	return (v0 << 8) | v1;
#endif
}

/* Compute effective address */

static uint16_t ea_direct(struct MC6809 *cpu) {
	unsigned ea = REG_DP << 8 | fetch_byte(cpu, REG_PC++);
	NVMA_CYCLE;
	return ea;
}

static uint16_t ea_extended(struct MC6809 *cpu) {
	unsigned ea = fetch_word(cpu, REG_PC);
	REG_PC += 2;
	NVMA_CYCLE;
	return ea;
}

static uint16_t ea_indexed(struct MC6809 *cpu) {
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
		return reg + sex5(postbyte & 0x1f);
	}
	switch (postbyte & 0x0f) {
		case 0x00: ea = reg; reg += 1; peek_byte(cpu, REG_PC); NVMA_CYCLE; NVMA_CYCLE; break;
		case 0x01: ea = reg; reg += 2; peek_byte(cpu, REG_PC); NVMA_CYCLE; NVMA_CYCLE; NVMA_CYCLE; break;
		case 0x02: reg -= 1; ea = reg; peek_byte(cpu, REG_PC); NVMA_CYCLE; NVMA_CYCLE; break;
		case 0x03: reg -= 2; ea = reg; peek_byte(cpu, REG_PC); NVMA_CYCLE; NVMA_CYCLE; NVMA_CYCLE; break;
		case 0x04: ea = reg; peek_byte(cpu, REG_PC); break;
		case 0x05: ea = reg + sex8(REG_B); peek_byte(cpu, REG_PC); NVMA_CYCLE; break;
		case 0x07: // illegal
		case 0x06: ea = reg + sex8(REG_A); peek_byte(cpu, REG_PC); NVMA_CYCLE; break;
		case 0x08: ea = byte_immediate(cpu); ea = sex8(ea) + reg; NVMA_CYCLE; break;
		case 0x09: ea = word_immediate(cpu); ea = ea + reg; NVMA_CYCLE; NVMA_CYCLE; NVMA_CYCLE; break;
		case 0x0a: ea = REG_PC | 0xff; break;
		case 0x0b: ea = reg + REG_D; peek_byte(cpu, REG_PC); peek_byte(cpu, REG_PC + 1); NVMA_CYCLE; NVMA_CYCLE; NVMA_CYCLE; break;
		case 0x0c: ea = byte_immediate(cpu); ea = sex8(ea) + REG_PC; NVMA_CYCLE; break;
		case 0x0d: ea = word_immediate(cpu); ea = ea + REG_PC; peek_byte(cpu, REG_PC); NVMA_CYCLE; NVMA_CYCLE; NVMA_CYCLE; break;
		case 0x0e: ea = 0xffff; break; // illegal
		case 0x0f: ea = word_immediate(cpu); NVMA_CYCLE; break;
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
	NVMA_CYCLE;
	push_s_word(cpu, REG_PC);
	push_s_word(cpu, REG_U);
	push_s_word(cpu, REG_Y);
	push_s_word(cpu, REG_X);
	push_s_byte(cpu, REG_DP);
	push_s_byte(cpu, REG_B);
	push_s_byte(cpu, REG_A);
	push_s_byte(cpu, REG_CC);
}

static void push_firq_registers(struct MC6809 *cpu) {
	NVMA_CYCLE;
	push_s_word(cpu, REG_PC);
	push_s_byte(cpu, REG_CC);
}

static void stack_irq_registers(struct MC6809 *cpu) {
	REG_CC |= CC_E;
	push_irq_registers(cpu);
}

static void stack_firq_registers(struct MC6809 *cpu) {
	REG_CC &= ~CC_E;
	push_firq_registers(cpu);
}

static void take_interrupt(struct MC6809 *cpu, uint8_t mask, uint16_t vec) {
	REG_CC |= mask;
	NVMA_CYCLE;
	cpu->state = mc6809_state_irq_reset_vector;
	REG_PC = fetch_word(cpu, vec);
	cpu->state = mc6809_state_label_a;
	NVMA_CYCLE;
}

static void instruction_posthook(struct MC6809 *cpu) {
	DELEGATE_SAFE_CALL(cpu->debug_cpu.instruction_posthook);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * ALU operations
 */

/* Illegal 6809 8-bit arithmetic operations */

static uint8_t op_discard(struct MC6809 *cpu, uint8_t a, uint8_t b) {
	(void)b;
	CLR_NZV;
	REG_CC |= CC_N;
	return a;
}

// Illegal version of DEC [hoglet67]
// Same as DEC, but modifies carry
static uint8_t op_xdec(struct MC6809 *cpu, uint8_t in) {
	unsigned out = in - 1;
	CLR_NZVC;
	SET_NZ8(out);
	if (out == 0x7f) REG_CC |= CC_V;
	if (in == 0) REG_CC |= CC_C;
	return out;
}

// Illegal version of CLR [hoglet67]
// Same as CLR, but C is unchanged
static uint8_t op_xclr(struct MC6809 *cpu, uint8_t in) {
	(void)in;
	CLR_NZV;
	REG_CC |= CC_Z;
	return 0;
}
