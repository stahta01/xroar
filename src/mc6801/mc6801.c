/** \file
 *
 *  \brief Motorola MC6801/6803 CPUs.
 *
 *  \copyright Copyright 2021-2024 Ciaran Anscomb
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
 *  -  MC6801/6803 data sheet, Motorola
 *
 *  -  MC6801 8-Bit Single-Chip Microcomputer Reference Manual, Motorola
 *
 *  Thanks to:
 *
 *  -  Simon Jonassen, for an interrupt test case that winkled out a problem in
 *     the RTI instruction.
 *
 *  -  Greg Dionne, for lots of details about illegal instructions, from an
 *     original list by James Tamer.
 *
 *  -  Ron Klein, for running tests on real hardware and sending me pictures of
 *     the results.
 *
 *  Lots of warnings:
 *
 *  This implementation is very much INCOMPLETE.
 *
 *  Focus is to emulate what's needed of an MC6803 within a Tandy MC-10.
 *
 *  Interface does _not_ reflect the multiple-use nature of 680[13] ports.
 *
 *  May eat your dog.  Well, maybe just your stick insects.
 */

#include "top-config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "array.h"
#include "delegate.h"
#include "xalloc.h"

#include "logging.h"
#include "mc6801.h"
#include "part.h"
#include "serialise.h"

#ifdef TRACE
#include "mc6801_trace.h"
#endif

static const struct ser_struct ser_struct_mc6801_port[] = {
	SER_ID_STRUCT_ELEM(1, struct MC6801_port, out_source),
	SER_ID_STRUCT_ELEM(2, struct MC6801_port, out_sink),
	SER_ID_STRUCT_ELEM(3, struct MC6801_port, in_source),
	SER_ID_STRUCT_ELEM(4, struct MC6801_port, in_sink),
};

static const struct ser_struct_data mc6801_port_ser_struct_data = {
	.elems = ser_struct_mc6801_port,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mc6801_port),
};

#define MC6801_SER_REG (13)
#define MC6801_SER_RAM (14)

static const struct ser_struct ser_struct_mc6801[] = {
	SER_ID_STRUCT_ELEM(1, struct MC6801, nmi),
	SER_ID_STRUCT_ELEM(2, struct MC6801, irq1),
	SER_ID_STRUCT_ELEM(3, struct MC6801, D),

	SER_ID_STRUCT_SUBSTRUCT(4, struct MC6801, port1, &mc6801_port_ser_struct_data),
	SER_ID_STRUCT_SUBSTRUCT(5, struct MC6801, port2, &mc6801_port_ser_struct_data),

	SER_ID_STRUCT_ELEM(6, struct MC6801, state),
	SER_ID_STRUCT_ELEM(7, struct MC6801, running),

	SER_ID_STRUCT_ELEM(8, struct MC6801, reg_cc),
	SER_ID_STRUCT_ELEM(9, struct MC6801, reg_d),
	SER_ID_STRUCT_ELEM(10, struct MC6801, reg_x),
	SER_ID_STRUCT_ELEM(11, struct MC6801, reg_sp),
	SER_ID_STRUCT_ELEM(12, struct MC6801, reg_pc),

	SER_ID_STRUCT_UNHANDLED(MC6801_SER_REG),
	SER_ID_STRUCT_UNHANDLED(MC6801_SER_RAM),

	SER_ID_STRUCT_ELEM(15, struct MC6801, itmp),
	SER_ID_STRUCT_ELEM(16, struct MC6801, nmi_latch),
	SER_ID_STRUCT_ELEM(17, struct MC6801, nmi_active),
	SER_ID_STRUCT_ELEM(18, struct MC6801, irq1_latch),
	SER_ID_STRUCT_ELEM(19, struct MC6801, irq1_active),
	SER_ID_STRUCT_ELEM(20, struct MC6801, irq2_latch),
	SER_ID_STRUCT_ELEM(21, struct MC6801, irq2_active),
	SER_ID_STRUCT_ELEM(22, struct MC6801, ICF),
	SER_ID_STRUCT_ELEM(23, struct MC6801, OCF),
	SER_ID_STRUCT_ELEM(24, struct MC6801, TOF),

	SER_ID_STRUCT_ELEM(25, struct MC6801, counter),
	SER_ID_STRUCT_ELEM(26, struct MC6801, output_compare),
	SER_ID_STRUCT_ELEM(27, struct MC6801, output_compare_inhibit),

	SER_ID_STRUCT_ELEM(28, struct MC6801, ICF_read),
	SER_ID_STRUCT_ELEM(29, struct MC6801, OCF_read),
	SER_ID_STRUCT_ELEM(30, struct MC6801, TOF_read),
	SER_ID_STRUCT_ELEM(31, struct MC6801, counter_lsb_buf),

	SER_ID_STRUCT_ELEM(32, struct MC6801, is_6801),
};

static _Bool mc6801_read_elem(void *sptr, struct ser_handle *sh, int tag);
static _Bool mc6801_write_elem(void *sptr, struct ser_handle *sh, int tag);

static const struct ser_struct_data mc6801_ser_struct_data = {
	.elems = ser_struct_mc6801,
	.num_elems = ARRAY_N_ELEMENTS(ser_struct_mc6801),
	.read_elem = mc6801_read_elem,
	.write_elem = mc6801_write_elem,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

extern inline void MC6801_NMI_SET(struct MC6801 *cpu, _Bool val);
extern inline void MC6801_IRQ1_SET(struct MC6801 *cpu, _Bool val);

// External interface

static unsigned mc6801_get_pc(void *sptr);
static void mc6801_set_pc(void *sptr, unsigned pc);

static void mc6801_reset(struct MC6801 *cpu);
static void mc6801_run(struct MC6801 *cpu);

// Wrap common fetches

static uint8_t fetch_byte(struct MC6801 *cpu, uint16_t a);
static uint16_t fetch_word(struct MC6801 *cpu, uint16_t a);
static uint8_t fetch_byte_notrace(struct MC6801 *cpu, uint16_t a);
static uint16_t fetch_word_notrace(struct MC6801 *cpu, uint16_t a);
static void store_byte(struct MC6801 *cpu, uint16_t a, uint8_t d);
#define peek_byte(c,a) ((void)fetch_byte_notrace(c,a))
#define NVMA_CYCLE (peek_byte(cpu, 0xffff))

// Compute effective address

static uint16_t ea_direct(struct MC6801 *cpu);
static uint16_t ea_extended(struct MC6801 *cpu);
static uint16_t ea_indexed(struct MC6801 *cpu);

// Interrupt handling

static void push_s_byte(struct MC6801 *cpu, uint8_t v);
static void push_s_word(struct MC6801 *cpu, uint16_t v);
static uint8_t pull_s_byte(struct MC6801 *cpu);
static uint16_t pull_s_word(struct MC6801 *cpu);

static void stack_irq_registers(struct MC6801 *cpu);
static void take_interrupt(struct MC6801 *cpu, uint16_t vec);
static void instruction_posthook(struct MC6801 *cpu);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Register handling macros

#define REG_CC (cpu->reg_cc)
#define REG_A (MC6801_REG_A(cpu))
#define REG_B (MC6801_REG_B(cpu))
#define REG_D (cpu->reg_d)
#define REG_X (cpu->reg_x)
#define REG_SP (cpu->reg_sp)
#define REG_PC (cpu->reg_pc)

// Condition code register macros

#define CC_H (0x20)
#define CC_I (0x10)
#define CC_N (0x08)
#define CC_Z (0x04)
#define CC_V (0x02)
#define CC_C (0x01)

// Common operations

#define STRUCT_CPU struct MC6801

#include "mc680x/mc680x_ops.c"

// Internal memory-mapped registers

#define REG_TCSR (cpu->reg[8])

#define TCSR_ICF  (0x80)
#define TCSR_OCF  (0x40)
#define TCSR_TOF  (0x20)
#define TCSR_EICI (0x10)
#define TCSR_EOCI (0x08)
#define TCSR_ETOI (0x04)
#define TCSR_IEDG (0x02)
#define TCSR_OLVL (0x01)

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// MC6801/6803 part creation

static struct part *mc6801_allocate(void);
static void mc6801_initialise(struct part *p, void *options);
static _Bool mc6801_finish(struct part *p);
static void mc6801_free(struct part *p);

static _Bool mc6801_is_a(struct part *p, const char *name);

static const struct partdb_entry_funcs mc6801_funcs = {
	.allocate = mc6801_allocate,
	.initialise = mc6801_initialise,
	.finish = mc6801_finish,
	.free = mc6801_free,

	.ser_struct_data = &mc6801_ser_struct_data,

	.is_a = mc6801_is_a,
};

const struct partdb_entry mc6801_part = { .name = "MC6801", .description = "Motorola | MC6801 CPU", .funcs = &mc6801_funcs };
const struct partdb_entry mc6803_part = { .name = "MC6803", .description = "Motorola | MC6803 CPU", .funcs = &mc6801_funcs };

static struct part *mc6801_allocate(void) {
	struct MC6801 *cpu = part_new(sizeof(*cpu));
	struct part *p = &cpu->debug_cpu.part;

	*cpu = (struct MC6801){0};

	cpu->debug_cpu.get_pc = DELEGATE_AS0(unsigned, mc6801_get_pc, cpu);
	cpu->debug_cpu.set_pc = DELEGATE_AS1(void, unsigned, mc6801_set_pc, cpu);

	cpu->reset = mc6801_reset;
	cpu->run = mc6801_run;
	cpu->mem_cycle = DELEGATE_DEFAULT2(void, bool, uint16);

#ifdef TRACE
	// Tracing
	cpu->tracer = mc6801_trace_new(cpu);
#endif

	cpu->port1.in_sink = 0xff;
	cpu->port2.in_sink = 0xff;

	return p;
}

static void mc6801_initialise(struct part *p, void *options) {
        struct MC6801 *cpu = (struct MC6801 *)p;
	cpu->is_6801 = options && (strcmp((char *)options, "6801") == 0);
	mc6801_reset(cpu);
}

static _Bool mc6801_finish(struct part *p) {
	struct MC6801 *cpu = (struct MC6801 *)p;
	if (cpu->is_6801 && !cpu->rom) {
		cpu->rom = xzalloc(2048);
		cpu->rom_size = 2048;
	}
	return 1;
}

static void mc6801_free(struct part *p) {
	struct MC6801 *cpu = (struct MC6801 *)p;
#ifdef TRACE
	if (cpu->tracer) {
		mc6801_trace_free(cpu->tracer);
	}
#endif
	if (cpu->rom)
		free(cpu->rom);
}

static _Bool mc6801_read_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct MC6801 *cpu = sptr;
	switch (tag) {
	case MC6801_SER_REG:
		ser_read(sh, cpu->reg, sizeof(cpu->reg));
		break;
	case MC6801_SER_RAM:
		ser_read(sh, cpu->ram, sizeof(cpu->ram));
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool mc6801_write_elem(void *sptr, struct ser_handle *sh, int tag) {
	struct MC6801 *cpu = sptr;
	switch (tag) {
	case MC6801_SER_REG:
		ser_write(sh, tag, cpu->reg, sizeof(cpu->reg));
		break;
	case MC6801_SER_RAM:
		ser_write(sh, tag, cpu->ram, sizeof(cpu->ram));
		break;
	default:
		return 0;
	}
	return 1;
}

static _Bool mc6801_is_a(struct part *p, const char *name) {
	if (!p)
		return 0;
	if (strcmp(name, "DEBUG-CPU") == 0)
		return 1;
	struct MC6801 *cpu = (struct MC6801 *)p;
	if (cpu->is_6801 && strcmp(name, "MC6801") == 0)
		return 1;
	return (!cpu->is_6801 && strcmp(name, "MC6803") == 0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static unsigned mc6801_get_pc(void *sptr) {
        struct MC6801 *cpu = sptr;
        return cpu->reg_pc;
}

static void mc6801_reset(struct MC6801 *cpu) {
	cpu->state = mc6801_state_reset;
}

// Run CPU while cpu->running is true.

static void mc6801_run(struct MC6801 *cpu) {

	do {

		switch (cpu->state) {

		case mc6801_state_reset:
			cpu->itmp = CC_I;
			REG_CC |= CC_I;
			cpu->nmi = cpu->nmi_latch = cpu->nmi_active = 0;
			cpu->irq1_latch = cpu->irq1_active = 0;
			cpu->irq2_latch = cpu->irq2_active = 0;
			cpu->ICF = cpu->ICF_read = 0;
			cpu->OCF = cpu->OCF_read = 0;
			cpu->TOF = cpu->TOF_read = 0;
			cpu->counter = 0;
			cpu->counter_lsb_buf = 0;
			cpu->output_compare = 0xffff;
#ifdef TRACE
			if (UNLIKELY(logging.trace_cpu)) {
				mc6801_trace_irq(cpu->tracer, MC6801_INT_VEC_RESET);
			}
#endif
			REG_PC = fetch_word(cpu, MC6801_INT_VEC_RESET);
			NVMA_CYCLE;
			cpu->state = mc6801_state_label_a;
			continue;

		case mc6801_state_label_a:
			if (UNLIKELY(cpu->nmi_active)) {
				REG_CC = (REG_CC & ~CC_I) | cpu->itmp;
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu);
				peek_byte(cpu, REG_SP);
				cpu->state = mc6801_state_dispatch_irq;
				continue;
			}
			if (UNLIKELY(!(REG_CC & CC_I) && (cpu->irq1_active || cpu->irq2_active))) {
				REG_CC = (REG_CC & ~CC_I) | cpu->itmp;
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu);
				peek_byte(cpu, REG_SP);
				cpu->state = mc6801_state_dispatch_irq;
				continue;
			}
			REG_CC = (REG_CC & ~CC_I) | cpu->itmp;
			cpu->state = mc6801_state_next_instruction;
			// Instruction fetch hook called here so that machine
			// can be stopped beforehand.
			DELEGATE_SAFE_CALL(cpu->debug_cpu.instruction_hook);
			continue;

		case mc6801_state_wai:
			peek_byte(cpu, REG_SP);
			if (cpu->nmi_active) {
				REG_CC = (REG_CC & ~CC_I) | cpu->itmp;
				cpu->state = mc6801_state_dispatch_irq;
				continue;
			}
			if (!(REG_CC & CC_I) && (cpu->irq1_active || cpu->irq2_active)) {
				REG_CC = (REG_CC & ~CC_I) | cpu->itmp;
				cpu->state = mc6801_state_dispatch_irq;
				continue;
			}
			continue;

		case mc6801_state_dispatch_irq:
			if (cpu->nmi_active) {
				cpu->nmi_active = cpu->nmi = cpu->nmi_latch = 0;
				take_interrupt(cpu, MC6801_INT_VEC_NMI);
				cpu->state = mc6801_state_label_a;
				continue;
			}
			if (cpu->irq1_active && !(REG_CC & CC_I)) {
				take_interrupt(cpu, MC6801_INT_VEC_IRQ1);
				cpu->state = mc6801_state_label_a;
				continue;
			}
			if (cpu->ICF && !(REG_CC & CC_I) && (REG_TCSR & TCSR_EICI)) {
				take_interrupt(cpu, MC6801_INT_VEC_ICF);
				cpu->state = mc6801_state_label_a;
				continue;
			}
			if (cpu->OCF && !(REG_CC & CC_I) && (REG_TCSR & TCSR_EOCI)) {
				take_interrupt(cpu, MC6801_INT_VEC_OCF);
				cpu->state = mc6801_state_label_a;
				continue;
			}
			if (cpu->TOF && !(REG_CC & CC_I) && (REG_TCSR & TCSR_ETOI)) {
				take_interrupt(cpu, MC6801_INT_VEC_TOF);
				cpu->state = mc6801_state_label_a;
				continue;
			}

			// "In the absence of any interrupt, the priority
			// encoder will always select $FFFO:FFFI (SCI
			// interrupt)."

			take_interrupt(cpu, MC6801_INT_VEC_SCI);
			cpu->state = mc6801_state_label_a;
			continue;

		case mc6801_state_next_instruction:
			{
			unsigned op;
			cpu->state = mc6801_state_label_a;
			// Fetch op-code and process
			op = byte_immediate(cpu);
			switch (op) {

			// 0x00 CLB illegal - clear, no flags
			case 0x00:
				REG_B = 0;
				peek_byte(cpu, REG_PC);
				break;

			// 0x01 NOP inherent
			case 0x01:
				peek_byte(cpu, REG_PC);
				break;

			// 0x02 SEXA illegal
			case 0x02:
				REG_A = (REG_CC & CC_C) ? 0xff : 0;
				peek_byte(cpu, REG_PC);
				break;

			// 0x03 SETA illegal
			case 0x03:
				REG_A = 0xff;
				peek_byte(cpu, REG_PC);
				break;

			// 0x04 LSRD inherent
			case 0x04:
				REG_D = op_lsr16_v(cpu, REG_D);
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				break;

			// 0x05 ASLD inherent
			case 0x05:
				REG_D = op_asl16(cpu, REG_D);
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				break;

			// 0x06 TAP inherent
			case 0x06:
				REG_CC = 0xc0 | REG_A | CC_I;
				cpu->itmp = REG_A & CC_I;
				if (cpu->itmp) {
					cpu->irq1_latch = cpu->irq2_latch = 0;
				}
				peek_byte(cpu, REG_PC);
				break;

			// 0x07 TPA inherent
			case 0x07:
				REG_A = 0xc0 | REG_CC;
				peek_byte(cpu, REG_PC);
				break;

			// 0x08 INX inherent
			case 0x08:
				REG_X++;
				CLR_Z;
				SET_Z16(REG_X);
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				break;

			// 0x09 DEX inherent
			case 0x09:
				REG_X--;
				CLR_Z;
				SET_Z16(REG_X);
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				break;

			// 0x0a CLV inherent
			case 0x0a:
				REG_CC &= ~CC_V;
				peek_byte(cpu, REG_PC);
				break;

			// 0x0b SEV inherent
			case 0x0b:
				REG_CC |= CC_V;
				peek_byte(cpu, REG_PC);
				break;

			// 0x0c CLC inherent
			case 0x0c:
				REG_CC &= ~CC_C;
				peek_byte(cpu, REG_PC);
				break;

			// 0x0d SEC inherent
			case 0x0d:
				REG_CC |= CC_C;
				peek_byte(cpu, REG_PC);
				break;

			// 0x0e CLI inherent
			case 0x0e:
				cpu->itmp = 0;
				peek_byte(cpu, REG_PC);
				break;

			// 0x0f SEI inherent
			case 0x0f:
				REG_CC |= CC_I;
				cpu->itmp = CC_I;
				cpu->irq1_latch = cpu->irq2_latch = 0;
				peek_byte(cpu, REG_PC);
				break;

			// 0x10 SBA inherent
			case 0x10:
				REG_A = op_sub(cpu, REG_A, REG_B);
				peek_byte(cpu, REG_PC);
				break;

			// 0x11 CBA inherent
			case 0x11:
				(void)op_sub(cpu, REG_A, REG_B);
				peek_byte(cpu, REG_PC);
				break;

			// 0x12 SCBA inherent, illegal; A = A - B - C
			case 0x12:
				REG_A = op_sbc(cpu, REG_A, REG_B);
				peek_byte(cpu, REG_PC);
				break;

			// 0x13 S1BA inherent, illegal; A = A - B - 1
			case 0x13:
				{
					unsigned out = REG_A - REG_B - 1;
					CLR_NZVC;
					SET_NZVC8(REG_A, REG_B, out);
					REG_A = out;
					peek_byte(cpu, REG_PC);
				}
				break;

			// 0x14 TCAB inherent, illegal; B = A - 1
			// 0x1c TCAB inherent, illegal; B = A - 1
			case 0x14:
			case 0x1c:
				{
					unsigned out = REG_A - 1;
					CLR_NZV;
					SET_NZ8(out);
					SET_V8(REG_A, 1, out);
					REG_B = out;
					peek_byte(cpu, REG_PC);
				}
				break;

			// 0x15 TCBA inherent, illegal; A = B - 1
			case 0x15:
				{
					unsigned out = REG_B - 1;
					CLR_NZV;
					SET_NZ8(out);
					SET_V8(REG_B, 1, out);
					REG_A = out;
					peek_byte(cpu, REG_PC);
				}
				break;

			// 0x16 TAB inherent
			// 0x1e TAB inherent, illegal
			case 0x16:
			case 0x1e:
				REG_B = REG_A;
				CLR_NZV;
				SET_NZ8(REG_B);
				peek_byte(cpu, REG_PC);
				break;

			// 0x17 TBA inherent
			case 0x17:
				REG_A = REG_B;
				CLR_NZV;
				SET_NZ8(REG_A);
				peek_byte(cpu, REG_PC);
				break;

			// 0x18 ABA inherent, illegal
			// 0x1a ABA inherent, illegal
			case 0x18:
			case 0x1a:
				REG_A = op_add_nzv(cpu, REG_A, REG_B);
				peek_byte(cpu, REG_PC);
				break;

			// 0x19 DAA inherent
			case 0x19:
				REG_A = op_daa_v(cpu, REG_A);
				peek_byte(cpu, REG_PC);
				break;

			// 0x1b ABA inherent
			case 0x1b:
				REG_A = op_add(cpu, REG_A, REG_B);
				peek_byte(cpu, REG_PC);
				break;

			// 0x1d TCBA inherent, illegal; A = B - 1
			case 0x1d:
				{
					unsigned out = REG_B - 1;
					CLR_NZVC;
					SET_NZ8(out);
					SET_V8(REG_B, 1, out);
					// CC.C inverted!
					REG_CC |= (~out >> 8) & CC_C;
					REG_A = out;
					peek_byte(cpu, REG_PC);
				}
				break;

			// 0x1f TBAC inherent, illegal; A = B, set C
			case 0x1f:
				REG_A = REG_B;
				CLR_NZV;
				SET_NZ8(REG_B);
				REG_CC |= CC_C;
				peek_byte(cpu, REG_PC);
				break;

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

			// 0x30 TSX inherent
			case 0x30:
				REG_X = REG_SP + 1;
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				break;

			// 0x31 INS inherent
			case 0x31:
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_SP++);
				break;

			// 0x32 PULA inherent
			case 0x32:
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_SP++);
				REG_A = fetch_byte_notrace(cpu, REG_SP);
				break;

			// 0x33 PULB inherent
			case 0x33:
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_SP++);
				REG_B = fetch_byte_notrace(cpu, REG_SP);
				break;

			// 0x34 DES inherent
			case 0x34:
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_SP--);
				break;

			// 0x35 TXS inherent
			case 0x35:
				REG_SP = REG_X - 1;
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				break;

			// 0x36 PSHA inherent
			case 0x36:
				peek_byte(cpu, REG_PC);
				store_byte(cpu, REG_SP--, REG_A);
				break;

			// 0x37 PSHB inherent
			case 0x37:
				peek_byte(cpu, REG_PC);
				store_byte(cpu, REG_SP--, REG_B);
				break;

			// 0x38 PULX inherent
			case 0x38:
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_SP++);
				REG_X = fetch_byte_notrace(cpu, REG_SP++) << 8;
				REG_X |= fetch_byte_notrace(cpu, REG_SP);
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
				NVMA_CYCLE;
				break;

			// 0x3b RTI inherent
			case 0x3b:
				peek_byte(cpu, REG_PC);
				peek_byte(cpu, REG_SP);
				// no point tracking the 1-cycle delay for ITMP->I here
				REG_CC = pull_s_byte(cpu);
				cpu->itmp = REG_CC & CC_I;
				REG_B = pull_s_byte(cpu);
				REG_A = pull_s_byte(cpu);
				REG_X = pull_s_word(cpu);
				REG_PC = pull_s_word(cpu);
				break;

			// 0x3c PSHX inherent
			case 0x3c:
				peek_byte(cpu, REG_PC);
				store_byte(cpu, REG_SP--, REG_X & 0xff);
				store_byte(cpu, REG_SP--, REG_X >> 8);
				break;

			// 0x3d MUL inherent
			case 0x3d: {
				unsigned tmp = REG_A * REG_B;
				REG_D = tmp;
				if (tmp & 0x80)
					REG_CC |= CC_C;
				else
					REG_CC &= ~CC_C;
				peek_byte(cpu, REG_PC);
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
				NVMA_CYCLE;
			} break;

			// 0x3e WAI inherent
			case 0x3e:
				REG_CC = (REG_CC & ~CC_I) | cpu->itmp;
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu);
				instruction_posthook(cpu);
				cpu->state = mc6801_state_wai;
				continue;

			// 0x3f SWI inherent
			case 0x3f:
				REG_CC = (REG_CC & ~CC_I) | cpu->itmp;
				peek_byte(cpu, REG_PC);
				stack_irq_registers(cpu);
				peek_byte(cpu, REG_SP);
				instruction_posthook(cpu);
				take_interrupt(cpu, MC6801_INT_VEC_SWI);
				cpu->state = mc6801_state_label_a;
				continue;

			// 0x40 - 0x4f inherent A register ops
			// 0x50 - 0x5f inherent B register ops
			// 0x60 - 0x6f indexed mode ops
			// 0x70 - 0x7f extended mode ops
			// NOTE: illegal indexed and extended instructions
			// appear to behave differently.  Needs investigating.
			case 0x40: case 0x41: case 0x42: case 0x43:
			case 0x44: case 0x45: case 0x46: case 0x47:
			case 0x48: case 0x49: case 0x4a: case 0x4b:
			case 0x4c: case 0x4d: case 0x4f:
			case 0x50: case 0x51: case 0x52: case 0x53:
			case 0x54: case 0x55: case 0x56: case 0x57:
			case 0x58: case 0x59: case 0x5a: case 0x5b:
			case 0x5c: case 0x5d: case 0x5f:
			case 0x60: case 0x61: case 0x62: case 0x63:
			case 0x64: case 0x65: case 0x66: case 0x67:
			case 0x68: case 0x69: case 0x6a: case 0x6b:
			case 0x6c: case 0x6d: case 0x6f:
			case 0x70: case 0x71: case 0x72: case 0x73:
			case 0x74: case 0x75: case 0x76: case 0x77:
			case 0x78: case 0x79: case 0x7a: case 0x7b:
			case 0x7c: case 0x7d: case 0x7f: {
				uint16_t ea;
				unsigned tmp1;
				switch ((op >> 4) & 0xf) {
				case 0x4: ea = 0; tmp1 = REG_A; break;
				case 0x5: ea = 0; tmp1 = REG_B; break;
				case 0x6: ea = ea_indexed(cpu); tmp1 = fetch_byte_notrace(cpu, ea); break;
				case 0x7: ea = ea_extended(cpu); tmp1 = fetch_byte_notrace(cpu, ea); break;
				default: ea = tmp1 = 0; break;
				}
				switch (op & 0xf) {
				case 0x0: tmp1 = op_neg(cpu, tmp1); break; // NEG, NEGA, NEGB
				case 0x1: tmp1 = op_ngt(cpu, tmp1); break; // NGT*,NGTA*,NGTB*
				case 0x2: tmp1 = op_ngc(cpu, tmp1); break; // NGC*,NGCA*,NGCB*
				case 0x3: tmp1 = op_com(cpu, tmp1); break; // COM, COMA, COMB
				case 0x5: // LSR illegal
				case 0x4: tmp1 = op_lsr_v(cpu, tmp1); break; // LSR, LSRA, LSRB
				case 0x6: tmp1 = op_ror_v(cpu, tmp1); break; // ROR, RORA, RORB
				case 0x7: tmp1 = op_asr_v(cpu, tmp1); break; // ASR, ASRA, ASRB
				case 0x8: tmp1 = op_asl(cpu, tmp1); break; // ASL, ASLA, ASLB
				case 0x9: tmp1 = op_rol(cpu, tmp1); break; // ROL, ROLA, ROLB
				case 0xa: tmp1 = op_dec(cpu, tmp1); break; // DEC, DECA, DECB
				case 0xb: tmp1 = op_dec_nc(cpu, tmp1); break; // DC*, DCA*, DCB*
				case 0xc: tmp1 = op_inc(cpu, tmp1); break; // INC, INCA, INCB
				case 0xd: tmp1 = op_tst_c(cpu, tmp1); break; // TST, TSTA, TSTB
				case 0xf: tmp1 = op_clr(cpu, tmp1); break; // CLR, CLRA, CLRB
				default: break;
				}
				switch (op & 0xf) {
				case 0xd: // TST
					switch ((op >> 4) & 0xf) {
					case 0x0: case 0x6: case 0x7:
						NVMA_CYCLE;
						break;
					default:
						break;
					}
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

			// 0x4e, 0x5e T (HCF)
			case 0x4e:
			case 0x5e:
				cpu->state = mc6801_state_hcf;
				break;

			// 0x6e JMP indexed
			// 0x7e JMP extended
			case 0x6e: case 0x7e: {
				unsigned ea;
				switch ((op >> 4) & 0xf) {
				case 0x6: ea = ea_indexed(cpu); break;
				case 0x7: ea = ea_extended(cpu); break;
				default: ea = 0; break;
				}
				REG_PC = ea;
			} break;

			// 0x80 - 0xbf A register arithmetic ops
			// 0xc0 - 0xff B register arithmetic ops
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
				case 0x6: tmp1 = op_ld(cpu, 0, tmp2); break; // LDAA, LDAB
				case 0x7: tmp1 = op_ld(cpu, 0xff, 0xff); break;  // I87*, IC7*
				case 0x8: tmp1 = op_eor(cpu, tmp1, tmp2); break; // EORA, EORB
				case 0x9: tmp1 = op_adc(cpu, tmp1, tmp2); break; // ADCA, ADCB
				case 0xa: tmp1 = op_or(cpu, tmp1, tmp2); break; // ORA, ORB
				case 0xb: tmp1 = op_add(cpu, tmp1, tmp2); break; // ADDA, ADDB
				default: break;
				}
				// TODO verify behaviour of 0x87, 0xc7
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

			// 0x8c, 0x9c, 0xac, 0xbc CPX
			case 0x8c: case 0x9c: case 0xac: case 0xbc: {
				unsigned tmp2;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				(void)op_sub16(cpu, REG_X, tmp2);
				NVMA_CYCLE;
			} break;

			// 0x8d BSR
			// 0x9d, 0xad, 0xbd JSR
			case 0x8d: case 0x9d: case 0xad: case 0xbd: {
				unsigned ea;
				switch ((op >> 4) & 3) {
				case 0: ea = short_relative(cpu); ea += REG_PC; NVMA_CYCLE; break;
				case 1: ea = ea_direct(cpu); break;
				case 2: ea = ea_indexed(cpu); break;
				case 3: ea = ea_extended(cpu); break;
				default: ea = 0; break;
				}
				peek_byte(cpu, ea);
				push_s_word(cpu, REG_PC);
				REG_PC = ea;
			} break;

			// 0x8e, 0x9e, 0xae, 0xbe LDS
			// 0xcc, 0xdc, 0xec, 0xfc LDD
			// 0xce, 0xde, 0xee, 0xfe LDX
			case 0x8e: case 0x9e: case 0xae: case 0xbe:
			case 0xcc: case 0xdc: case 0xec: case 0xfc:
			case 0xce: case 0xde: case 0xee: case 0xfe: {
				unsigned tmp1, tmp2;
				switch ((op >> 4) & 3) {
				case 0: tmp2 = word_immediate(cpu); break;
				case 1: tmp2 = word_direct(cpu); break;
				case 2: tmp2 = word_indexed(cpu); break;
				case 3: tmp2 = word_extended(cpu); break;
				default: tmp2 = 0; break;
				}
				tmp1 = op_ld16(cpu, 0, tmp2);
				switch (op & 0x4e) {
				default:
				case 0x0e: REG_SP = tmp1; break;
				case 0x4c: REG_D = tmp1; break;
				case 0x4e: REG_X = tmp1; break;
				}
			} break;

			// 0x8f XXX illegal
			// 0xcd, 0xcf XXX illegal
			// TODO: behaviour not tested, this just gets the cycle
			// count right
			case 0x8f:
			case 0xcd: case 0xcf: {
				unsigned tmp1;
				(void)word_immediate(cpu);
				switch (op & 0x4e) {
				default:
				case 0x0e: tmp1 = REG_SP; break;
				case 0x4c: tmp1 = REG_D; break;
				case 0x4e: tmp1 = REG_X; break;
				}
				CLR_NZV;
				SET_NZ16(tmp1);
			} break;

			// 0x97, 0xa7, 0xb7 STAA
			// 0xd7, 0xe7, 0xf7 STAB
			case 0x97: case 0xa7: case 0xb7:
			case 0xd7: case 0xe7: case 0xf7: {
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

			// 0x9f, 0xaf, 0xbf STS
			// 0xdd, 0xed, 0xfd STD
			// 0xdf, 0xef, 0xff STX
			case 0x9f: case 0xaf: case 0xbf:
			case 0xdd: case 0xed: case 0xfd:
			case 0xdf: case 0xef: case 0xff: {
				uint16_t ea, tmp1;
				switch (op & 0x4e) {
				default:
				case 0x0e: tmp1 = REG_SP; break;
				case 0x4c: tmp1 = REG_D; break;
				case 0x4e: tmp1 = REG_X; break;
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

			// Illegal instruction
			default:
				NVMA_CYCLE;
				break;
			}

			break;
			}

		// Certain illegal instructions cause the CPU to lock up:
		case mc6801_state_hcf:
			NVMA_CYCLE;
			continue;

		}

		cpu->nmi_active = cpu->nmi_latch;
		cpu->irq1_active = cpu->irq1_latch;
		cpu->irq2_active = cpu->irq2_latch;
		instruction_posthook(cpu);
		continue;

	} while (cpu->running);

}

static void mc6801_set_pc(void *sptr, unsigned pc) {
	struct MC6801 *cpu = sptr;
	REG_PC = pc;
	cpu->state = mc6801_state_next_instruction;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Data reading & writing

// Wrap common fetches

static uint8_t fetch_byte_notrace(struct MC6801 *cpu, uint16_t a) {
	cpu->nmi_latch |= cpu->nmi;
	cpu->irq1_latch = cpu->irq1;
	cpu->counter++;
	if (cpu->counter == 0xffff) {
		cpu->TOF = TCSR_TOF;
		if (REG_TCSR & TCSR_ETOI)
			cpu->irq2_latch = 1;
	}
	if (!cpu->output_compare_inhibit && cpu->counter == cpu->output_compare) {
		cpu->OCF = TCSR_OCF;
		if (REG_TCSR & TCSR_EOCI)
			cpu->irq2_latch = 1;
	}
	cpu->output_compare_inhibit = 0;

	// XXX This should be mode-dependent.  The lower address bits are
	// shared with the data bus, and so it is these address values that may
	// remain on the data bus if left floating during a read.
	cpu->D = a & 0xff;

	if (a < 0x0020) {
		DELEGATE_CALL(cpu->mem_cycle, 1, 0xffff);
		switch (a) {
		case MC6801_REG_P1DDR:
		case MC6801_REG_P2DDR:
			cpu->D = 0xff;
			break;
		case MC6801_REG_P1DR:
			cpu->D = (cpu->reg[MC6801_REG_P1DR] & cpu->reg[MC6801_REG_P1DDR]) | ((cpu->port1.out_source | cpu->port1.in_source) & cpu->port1.out_sink & cpu->port1.in_sink & ~cpu->reg[MC6801_REG_P1DDR]);
			break;
		case MC6801_REG_P2DR:
			DELEGATE_SAFE_CALL(cpu->port2.preread);
			cpu->D = ((cpu->reg[MC6801_REG_P2DR] & cpu->reg[MC6801_REG_P2DDR]) | ((cpu->port2.out_source | cpu->port2.in_source) & cpu->port2.out_sink & cpu->port2.in_sink & ~cpu->reg[MC6801_REG_P2DDR])) & 0x1f;
			break;
		case MC6801_REG_TCSR:
			cpu->ICF_read = cpu->ICF;
			cpu->OCF_read = cpu->OCF;
			cpu->TOF_read = cpu->TOF;
			cpu->D = cpu->ICF | cpu->OCF | cpu->TOF | (cpu->reg[MC6801_REG_TCSR] & 0x1f);
			break;
		case MC6801_REG_CRMSB:
			cpu->D = cpu->counter >> 8;
			cpu->counter_lsb_buf = cpu->counter & 0xff;
			if (cpu->TOF_read) {
				cpu->TOF = cpu->TOF_read = 0;
			}
			break;
		case MC6801_REG_CRLSB:
			cpu->D = cpu->counter_lsb_buf;
			break;
		default:
			cpu->D = cpu->reg[a];
			break;
		}
		return cpu->D;
	}
	if (a >= 0x0080 && a < 0x0100) {
		DELEGATE_CALL(cpu->mem_cycle, 1, 0xffff);
		cpu->D = cpu->ram[a & 0x7f];
		return cpu->D;
	}
	DELEGATE_CALL(cpu->mem_cycle, 1, a);
	return cpu->D;
}

static void store_byte(struct MC6801 *cpu, uint16_t a, uint8_t d) {
	cpu->nmi_latch |= cpu->nmi;
	cpu->irq1_latch = cpu->irq1;
	cpu->counter++;
	if (cpu->counter == 0xffff) {
		cpu->TOF = TCSR_TOF;
		if (REG_TCSR & TCSR_ETOI)
			cpu->irq2_latch = 1;
	}
	if (!cpu->output_compare_inhibit && cpu->counter == cpu->output_compare) {
		cpu->OCF = TCSR_OCF;
		if (REG_TCSR & TCSR_EOCI)
			cpu->irq2_latch = 1;
	}
	cpu->output_compare_inhibit = 0;

	if (a < 0x0020) {
		cpu->reg[a] = d;
		switch (a) {
		case MC6801_REG_P1DDR:
		case MC6801_REG_P1DR:
			cpu->port1.out_source = cpu->reg[MC6801_REG_P1DR] & cpu->reg[MC6801_REG_P1DDR];
			cpu->port1.out_sink = cpu->reg[MC6801_REG_P1DR] | ~cpu->reg[MC6801_REG_P1DDR];
			DELEGATE_SAFE_CALL(cpu->port1.postwrite);
			break;
		case MC6801_REG_P2DDR:
		case MC6801_REG_P2DR:
			cpu->port2.out_source = cpu->reg[MC6801_REG_P2DR] & cpu->reg[MC6801_REG_P2DDR] & 0x1f;
			cpu->port2.out_sink = cpu->reg[MC6801_REG_P2DR] | ~cpu->reg[MC6801_REG_P2DDR] | 0xe0;
			DELEGATE_SAFE_CALL(cpu->port2.postwrite);
			break;
		case MC6801_REG_CRMSB:
			cpu->counter = 0xfff8;
			break;
		case MC6801_REG_OCMSB:
			cpu->output_compare_inhibit = 1;
			// fall through
		case MC6801_REG_OCLSB:
			cpu->output_compare = (cpu->reg[MC6801_REG_OCMSB] << 8) | cpu->reg[MC6801_REG_OCLSB];
			if (cpu->OCF_read) {
				cpu->OCF = cpu->OCF_read = 0;
			}
			break;
		default:
			break;
		}
		DELEGATE_CALL(cpu->mem_cycle, 1, 0xffff);
		return;
	}
	if (a >= 0x0080 && a < 0x0100) {
		cpu->ram[a & 0x7f] = d;
		DELEGATE_CALL(cpu->mem_cycle, 1, 0xffff);
		return;
	}
	cpu->D = d;
	DELEGATE_CALL(cpu->mem_cycle, 0, a);
}

static uint16_t fetch_word_notrace(struct MC6801 *cpu, uint16_t a) {
	unsigned v = fetch_byte_notrace(cpu, a) << 8;
	return v | fetch_byte_notrace(cpu, a+1);
}

static uint8_t fetch_byte(struct MC6801 *cpu, uint16_t a) {
	uint8_t v = fetch_byte_notrace(cpu, a);
#ifdef TRACE
	if (UNLIKELY(logging.trace_cpu)) {
		mc6801_trace_byte(cpu->tracer, v, a);
	}
#endif
	return v;
}

static uint16_t fetch_word(struct MC6801 *cpu, uint16_t a) {
#ifndef TRACE
	return fetch_word_notrace(cpu, a);
#else
	if (LIKELY(!logging.trace_cpu)) {
		return fetch_word_notrace(cpu, a);
	}
	unsigned v0 = fetch_byte_notrace(cpu, a);
	mc6801_trace_byte(cpu->tracer, v0, a);
	unsigned v1 = fetch_byte_notrace(cpu, a+1);
	mc6801_trace_byte(cpu->tracer, v1, a+1);
	return (v0 << 8) | v1;
#endif
}

// Compute effective address

static uint16_t ea_direct(struct MC6801 *cpu) {
	unsigned ea = fetch_byte(cpu, REG_PC++);
	return ea;
}

static uint16_t ea_extended(struct MC6801 *cpu) {
	unsigned ea = fetch_word(cpu, REG_PC);
	REG_PC += 2;
	return ea;
}

static uint16_t ea_indexed(struct MC6801 *cpu) {
	unsigned ea = REG_X + fetch_byte(cpu, REG_PC++);
	NVMA_CYCLE;
	return ea;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Interrupt handling

static void push_s_byte(struct MC6801 *cpu, uint8_t v) {
	store_byte(cpu, REG_SP--, v);
}

static void push_s_word(struct MC6801 *cpu, uint16_t v) {
	store_byte(cpu, REG_SP--, v);
	store_byte(cpu, REG_SP--, v >> 8);
}

static uint8_t pull_s_byte(struct MC6801 *cpu) {
	return fetch_byte(cpu, ++REG_SP);
}

static uint16_t pull_s_word(struct MC6801 *cpu) {
	unsigned val = fetch_byte(cpu, ++REG_SP);
	return (val << 8) | fetch_byte(cpu, ++REG_SP);
}

static void stack_irq_registers(struct MC6801 *cpu) {
	push_s_word(cpu, REG_PC);
	push_s_word(cpu, REG_X);
	push_s_byte(cpu, REG_A);
	push_s_byte(cpu, REG_B);
	push_s_byte(cpu, REG_CC);
}

static void take_interrupt(struct MC6801 *cpu, uint16_t vec) {
#ifdef TRACE
	if (UNLIKELY(logging.trace_cpu)) {
		mc6801_trace_irq(cpu->tracer, vec);
	}
#endif
	REG_CC |= CC_I;
	cpu->itmp = CC_I;
	cpu->nmi_latch = cpu->irq1_latch = cpu->irq2_latch = 0;
	REG_PC = fetch_word(cpu, vec);
}

static void instruction_posthook(struct MC6801 *cpu) {
#ifdef TRACE
	if (UNLIKELY(logging.trace_cpu)) {
		mc6801_trace_print(cpu->tracer);
	}
#endif
	DELEGATE_SAFE_CALL(cpu->debug_cpu.instruction_posthook);
}
