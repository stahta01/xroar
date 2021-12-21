/** \file
 *
 *  \brief Motorola MC6801/6803 CPU tracing.
 *
 *  \copyright Copyright 2021 Ciaran Anscomb
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

#include "top-config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "mc6801.h"
#include "mc6801_trace.h"

// Instruction types.  No PAGE2, PAGE3 for this CPU!

enum {
	PAGE0 = 0, ILLEGAL,
	INHERENT, WORD_IMMEDIATE, IMMEDIATE, EXTENDED,
	DIRECT, INDEXED, RELATIVE,
	IRQVECTOR,
};

/* Three arrays of instructions, one for each of PAGE0, PAGE2 and PAGE3 */

static struct {
	const char *mnemonic;
	int type;
} const instructions[256] = {

	// 0x00 - 0x0F
	{ "CLRB*", INHERENT },
	{ "NOP", INHERENT },
	{ "SEXA*", INHERENT },
	{ "SETA*", INHERENT },
	{ "LSRD", INHERENT },
	{ "ASLD", INHERENT },
	{ "TAP", INHERENT },
	{ "TPA", INHERENT },
	{ "INX", INHERENT },
	{ "DEX", INHERENT },
	{ "CLV", INHERENT },
	{ "SEV", INHERENT },
	{ "CLC", INHERENT },
	{ "SEC", INHERENT },
	{ "CLI", INHERENT },
	{ "SEI", INHERENT },
	// 0x10 - 0x1F
	{ "SBA", INHERENT },
	{ "CBA", INHERENT },
	{ "SCBA*", INHERENT },
	{ "S1BA*", INHERENT },
	{ "TXAB*", INHERENT },
	{ "TCBA*", INHERENT },
	{ "TAB", INHERENT },
	{ "TBA", INHERENT },
	{ "ABA*", INHERENT },
	{ "DAA", INHERENT },
	{ "ABA*", INHERENT },
	{ "ABA", INHERENT },
	{ "TCAB*", INHERENT },
	{ "TCBA*", INHERENT },
	{ "TBA*", INHERENT },
	{ "TBAC*", INHERENT },
	// 0x20 - 0x2F
	{ "BRA", RELATIVE },
	{ "BRN", RELATIVE },
	{ "BHI", RELATIVE },
	{ "BLS", RELATIVE },
	{ "BCC", RELATIVE },
	{ "BCS", RELATIVE },
	{ "BNE", RELATIVE },
	{ "BEQ", RELATIVE },
	{ "BVC", RELATIVE },
	{ "BVS", RELATIVE },
	{ "BPL", RELATIVE },
	{ "BMI", RELATIVE },
	{ "BGE", RELATIVE },
	{ "BLT", RELATIVE },
	{ "BGT", RELATIVE },
	{ "BLE", RELATIVE },
	// 0x30 - 0x3F
	{ "TSX", INHERENT },
	{ "INS", INHERENT },
	{ "PULA", INHERENT },
	{ "PULB", INHERENT },
	{ "DES", INHERENT },
	{ "TXS", INHERENT },
	{ "PSHA", INHERENT },
	{ "PSHB", INHERENT },
	{ "PULX", INHERENT },
	{ "RTS", INHERENT },
	{ "ABX", INHERENT },
	{ "RTI", INHERENT },
	{ "PSHX", INHERENT },
	{ "MUL", INHERENT },
	{ "WAI", INHERENT },
	{ "SWI", INHERENT },
	// 0x40 - 0x4F
	{ "NEGA", INHERENT },
	{ "TSTA*", INHERENT },
	{ "NGCA*", INHERENT },
	{ "COMA", INHERENT },
	{ "LSRA", INHERENT },
	{ "LSRA*", INHERENT },
	{ "RORA", INHERENT },
	{ "ASRA", INHERENT },
	{ "LSLA", INHERENT },
	{ "ROLA", INHERENT },
	{ "DECA", INHERENT },
	{ "DECA*", INHERENT },
	{ "INCA", INHERENT },
	{ "TSTA", INHERENT },
	{ "T", INHERENT },
	{ "CLRA", INHERENT },
	// 0x50 - 0x5F
	{ "NEGB", INHERENT },
	{ "TSTB*", INHERENT },
	{ "NGCB*", INHERENT },
	{ "COMB", INHERENT },
	{ "LSRB", INHERENT },
	{ "LSRB*", INHERENT },
	{ "RORB", INHERENT },
	{ "ASRB", INHERENT },
	{ "LSLB", INHERENT },
	{ "ROLB", INHERENT },
	{ "DECB", INHERENT },
	{ "DECB*", INHERENT },
	{ "INCB", INHERENT },
	{ "TSTB", INHERENT },
	{ "T", INHERENT },
	{ "CLRB", INHERENT },
	// 0x60 - 0x6F
	{ "NEG", INDEXED },
	{ "TST*", INDEXED },
	{ "NGC*", INDEXED },
	{ "COM", INDEXED },
	{ "LSR", INDEXED },
	{ "LSR*", INDEXED },
	{ "ROR", INDEXED },
	{ "ASR", INDEXED },
	{ "LSL", INDEXED },
	{ "ROL", INDEXED },
	{ "DEC", INDEXED },
	{ "DEC*", INDEXED },
	{ "INC", INDEXED },
	{ "TST", INDEXED },
	{ "JMP", INDEXED },
	{ "CLR", INDEXED },
	// 0x70 - 0x7F
	{ "NEG", EXTENDED },
	{ "TST*", EXTENDED },
	{ "NGC*", EXTENDED },
	{ "COM", EXTENDED },
	{ "LSR", EXTENDED },
	{ "LSR*", EXTENDED },
	{ "ROR", EXTENDED },
	{ "ASR", EXTENDED },
	{ "LSL", EXTENDED },
	{ "ROL", EXTENDED },
	{ "DEC", EXTENDED },
	{ "DEC*", EXTENDED },
	{ "INC", EXTENDED },
	{ "TST", EXTENDED },
	{ "JMP", EXTENDED },
	{ "CLR", EXTENDED },

	// 0x80 - 0x8F
	{ "SUBA", IMMEDIATE },
	{ "CMPA", IMMEDIATE },
	{ "SBCA", IMMEDIATE },
	{ "SUBD", WORD_IMMEDIATE },
	{ "ANDA", IMMEDIATE },
	{ "BITA", IMMEDIATE },
	{ "LDAA", IMMEDIATE },
	{ "DISCRD*", IMMEDIATE },
	{ "EORA", IMMEDIATE },
	{ "ADCA", IMMEDIATE },
	{ "ORAA", IMMEDIATE },
	{ "ADDA", IMMEDIATE },
	{ "CPX", WORD_IMMEDIATE },
	{ "BSR", RELATIVE },
	{ "LDS", WORD_IMMEDIATE },
	{ "*", ILLEGAL },
	// 0x90 - 0x9F
	{ "SUBA", DIRECT },
	{ "CMPA", DIRECT },
	{ "SBCA", DIRECT },
	{ "SUBD", DIRECT },
	{ "ANDA", DIRECT },
	{ "BITA", DIRECT },
	{ "LDAA", DIRECT },
	{ "STAA", DIRECT },
	{ "EORA", DIRECT },
	{ "ADCA", DIRECT },
	{ "ORAA", DIRECT },
	{ "ADDA", DIRECT },
	{ "CPX", DIRECT },
	{ "JSR", DIRECT },
	{ "LDS", DIRECT },
	{ "STS", DIRECT },
	// 0xA0 - 0xAF
	{ "SUBA", INDEXED },
	{ "CMPA", INDEXED },
	{ "SBCA", INDEXED },
	{ "SUBD", INDEXED },
	{ "ANDA", INDEXED },
	{ "BITA", INDEXED },
	{ "LDAA", INDEXED },
	{ "STAA", INDEXED },
	{ "EORA", INDEXED },
	{ "ADCA", INDEXED },
	{ "ORAA", INDEXED },
	{ "ADDA", INDEXED },
	{ "CPX", INDEXED },
	{ "JSR", INDEXED },
	{ "LDS", INDEXED },
	{ "STS", INDEXED },
	// 0xB0 - 0xBF
	{ "SUBA", EXTENDED },
	{ "CMPA", EXTENDED },
	{ "SBCA", EXTENDED },
	{ "SUBD", EXTENDED },
	{ "ANDA", EXTENDED },
	{ "BITA", EXTENDED },
	{ "LDAA", EXTENDED },
	{ "STAA", EXTENDED },
	{ "EORA", EXTENDED },
	{ "ADCA", EXTENDED },
	{ "ORAA", EXTENDED },
	{ "ADDA", EXTENDED },
	{ "CPX", EXTENDED },
	{ "JSR", EXTENDED },
	{ "LDS", EXTENDED },
	{ "STS", EXTENDED },
	// 0xC0 - 0xCF
	{ "SUBB", IMMEDIATE },
	{ "CMPB", IMMEDIATE },
	{ "SBCB", IMMEDIATE },
	{ "ADDD", WORD_IMMEDIATE },
	{ "ANDB", IMMEDIATE },
	{ "BITB", IMMEDIATE },
	{ "LDAB", IMMEDIATE },
	{ "*", ILLEGAL },
	{ "EORB", IMMEDIATE },
	{ "ADCB", IMMEDIATE },
	{ "ORAB", IMMEDIATE },
	{ "ADDB", IMMEDIATE },
	{ "LDD", WORD_IMMEDIATE },
	{ "*", ILLEGAL },
	{ "LDX", WORD_IMMEDIATE },
	{ "*", ILLEGAL },
	// 0xD0 - 0xDF
	{ "SUBB", DIRECT },
	{ "CMPB", DIRECT },
	{ "SBCB", DIRECT },
	{ "ADDD", DIRECT },
	{ "ANDB", DIRECT },
	{ "BITB", DIRECT },
	{ "LDAB", DIRECT },
	{ "STAB", DIRECT },
	{ "EORB", DIRECT },
	{ "ADCB", DIRECT },
	{ "ORAB", DIRECT },
	{ "ADDB", DIRECT },
	{ "LDD", DIRECT },
	{ "STD", DIRECT },
	{ "LDX", DIRECT },
	{ "STX", DIRECT },
	// 0xE0 - 0xEF
	{ "SUBB", INDEXED },
	{ "CMPB", INDEXED },
	{ "SBCB", INDEXED },
	{ "ADDD", INDEXED },
	{ "ANDB", INDEXED },
	{ "BITB", INDEXED },
	{ "LDAB", INDEXED },
	{ "STAB", INDEXED },
	{ "EORB", INDEXED },
	{ "ADCB", INDEXED },
	{ "ORAB", INDEXED },
	{ "ADDB", INDEXED },
	{ "LDD", INDEXED },
	{ "STD", INDEXED },
	{ "LDX", INDEXED },
	{ "STX", INDEXED },
	// 0xF0 - 0xFF
	{ "SUBB", EXTENDED },
	{ "CMPB", EXTENDED },
	{ "SBCB", EXTENDED },
	{ "ADDD", EXTENDED },
	{ "ANDB", EXTENDED },
	{ "BITB", EXTENDED },
	{ "LDAB", EXTENDED },
	{ "STAB", EXTENDED },
	{ "EORB", EXTENDED },
	{ "ADCB", EXTENDED },
	{ "ORAB", EXTENDED },
	{ "ADDB", EXTENDED },
	{ "LDD", EXTENDED },
	{ "STD", EXTENDED },
	{ "LDX", EXTENDED },
	{ "STX", EXTENDED }
};

/* The next byte is expected to be one of these, with special exceptions:
 * WANT_PRINT - expecting trace_print to be called
 * WANT_NOTHING - expecting a byte that is to be ignored */

enum {
	WANT_INSTRUCTION,
	WANT_IRQ_VECTOR,
	WANT_VALUE,
	WANT_PRINT,
	WANT_NOTHING
};

/* Sequences of expected bytes */

static int const state_list_irq[] = { WANT_VALUE, WANT_PRINT };
static int const state_list_inherent[] = { WANT_PRINT };
static int const state_list_idx[] = { WANT_VALUE, WANT_PRINT };
static int const state_list_imm8[] = { WANT_VALUE, WANT_PRINT };
static int const state_list_imm16[] = { WANT_VALUE, WANT_VALUE, WANT_PRINT };

/* Names */

// Interrupt vector names
static char const * const irq_names[8] = {
	"[SCI]", "[TOF]", "[OCF]", "[ICF]",
	"[IRQ1]", "[SWI]", "[NMI]", "[RESET]"
};

/* Current state */

#define BYTES_BUF_SIZE 5

struct mc6801_trace {
	struct MC6801 *cpu;

	int state;
	uint16_t instr_pc;
	int bytes_count;
	uint8_t bytes_buf[BYTES_BUF_SIZE];

	const char *mnemonic;
	char operand_text[19];

	int ins_type;
	const int *state_list;
	uint32_t value;
	int idx_mode;
	const char *idx_reg;
	_Bool idx_indirect;
};

static void reset_state(struct mc6801_trace *tracer);
static void trace_print_short(struct mc6801_trace *tracer);

#define STACK_PRINT(t,r) do { \
		if (not_first) { strcat((t)->operand_text, "," r); } \
		else { strcat((t)->operand_text, r); not_first = 1; } \
	} while (0)

#define sex5(v) ((int)((v) & 0x0f) - (int)((v) & 0x10))
#define sex8(v) ((int8_t)(v))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct mc6801_trace *mc6801_trace_new(struct MC6801 *cpu) {
	struct mc6801_trace *tracer = xmalloc(sizeof(*tracer));
	*tracer = (struct mc6801_trace){0};
	tracer->cpu = cpu;
	reset_state(tracer);
	return tracer;
}

void mc6801_trace_free(struct mc6801_trace *tracer) {
	free(tracer);
}

void mc6801_trace_reset(struct mc6801_trace *tracer) {
	reset_state(tracer);
	mc6801_trace_irq(tracer, 0xfffe);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void reset_state(struct mc6801_trace *tracer) {
	tracer->state = WANT_INSTRUCTION;
	tracer->instr_pc = 0;
	tracer->bytes_count = 0;
	tracer->mnemonic = "*";
	strcpy(tracer->operand_text, "*");

	tracer->ins_type = PAGE0;
	tracer->state_list = NULL;
	tracer->idx_mode = 0;
	tracer->idx_reg = "";
	tracer->idx_indirect = 0;
}

/* Called for each memory read */

void mc6801_trace_byte(struct mc6801_trace *tracer, uint8_t byte, uint16_t pc) {

	// Record PC of instruction
	if (tracer->bytes_count == 0) {
		tracer->instr_pc = pc;
	}

	// Record byte if considered part of instruction
	if (tracer->bytes_count < BYTES_BUF_SIZE && tracer->state != WANT_PRINT && tracer->state != WANT_NOTHING) {
		tracer->bytes_buf[tracer->bytes_count++] = byte;
	}

	switch (tracer->state) {

		// Instruction fetch
		default:
		case WANT_INSTRUCTION:
			tracer->value = 0;
			tracer->state_list = NULL;
			tracer->mnemonic = instructions[byte].mnemonic;
			tracer->ins_type = instructions[byte].type;
			switch (tracer->ins_type) {
				// Otherwise use an appropriate state list:
				default: case ILLEGAL: case INHERENT:
					tracer->state_list = state_list_inherent;
					break;
				case IMMEDIATE: case DIRECT: case RELATIVE:
					tracer->state_list = state_list_imm8;
					break;
				case INDEXED:
					tracer->state_list = state_list_idx;
					break;
				case WORD_IMMEDIATE: case EXTENDED:
					tracer->state_list = state_list_imm16;
					break;
			}
			break;

		// First byte of an IRQ vector
		case WANT_IRQ_VECTOR:
			tracer->value = byte;
			tracer->ins_type = IRQVECTOR;
			tracer->state_list = state_list_irq;
			break;

		// Building a value byte by byte
		case WANT_VALUE:
			tracer->value = (tracer->value << 8) | byte;
			break;

		// Expecting CPU code to call trace_print
		case WANT_PRINT:
			tracer->state_list = NULL;
			return;

		// This byte is to be ignored (used following IRQ vector fetch)
		case WANT_NOTHING:
			break;
	}

	// Get next state from state list
	if (tracer->state_list)
		tracer->state = *(tracer->state_list++);

	if (tracer->state != WANT_PRINT)
		return;

	// If the next state is WANT_PRINT, we're done with the instruction, so
	// prep the operand text for printing.

	tracer->state_list = NULL;

	tracer->operand_text[0] = '\0';
	switch (tracer->ins_type) {
		case ILLEGAL: case INHERENT:
			break;

		case IMMEDIATE:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "#$%02x", tracer->value);
			break;

		case DIRECT:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "<$%02x", tracer->value);
			break;

		case WORD_IMMEDIATE:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "#$%04x", tracer->value);
			break;

		case EXTENDED:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "$%04x", tracer->value);
			break;

		case INDEXED:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "$%02x,X", tracer->value);
			break;

		case RELATIVE:
			pc = (pc + 1 + sex8(tracer->value)) & 0xffff;
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "$%04x", pc);
			break;

		// CPU code will not call trace_print after IRQ vector fetch
		// and before the next instruction, therefore the state list
		// for IRQ vectors skips an expected dummy byte, and this
		// prints the trace line early.

		case IRQVECTOR:
			trace_print_short(tracer);
			printf("\n");
			fflush(stdout);
			break;

		default:
			break;
	}
}

/* Called just before an IRQ vector fetch */

void mc6801_trace_irq(struct mc6801_trace *tracer, int vector) {
	reset_state(tracer);
	tracer->state = WANT_IRQ_VECTOR;
	tracer->bytes_count = 0;
	tracer->mnemonic = irq_names[(vector & 15) >> 1];
}

/* Called after each instruction */

void mc6801_trace_print(struct mc6801_trace *tracer) {
	if (tracer->state != WANT_PRINT) return;
	trace_print_short(tracer);
	struct MC6801 *cpu = tracer->cpu;
	printf("cc=%02x a=%02x b=%02x x=%04x sp=%04x\n",
	       cpu->reg_cc | 0xc0, MC6801_REG_A(cpu), MC6801_REG_B(cpu),
	       cpu->reg_x, cpu->reg_sp);
	fflush(stdout);
	reset_state(tracer);
}

static void trace_print_short(struct mc6801_trace *tracer) {
	char bytes_string[(BYTES_BUF_SIZE*2)+1];
	if (tracer->bytes_count == 0) return;
	for (int i = 0; i < tracer->bytes_count; i++) {
		snprintf(bytes_string + i*2, 3, "%02x", tracer->bytes_buf[i]);
	}
	printf("%04x| %-12s%-8s%-20s", tracer->instr_pc, bytes_string, tracer->mnemonic, tracer->operand_text);
	reset_state(tracer);
}
