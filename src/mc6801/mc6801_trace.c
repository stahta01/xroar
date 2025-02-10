/** \file
 *
 *  \brief Motorola MC6801/6803 CPU tracing.
 *
 *  \copyright Copyright 2021-2025 Ciaran Anscomb
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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xalloc.h"

#include "events.h"
#include "logging.h"
#include "mc6801.h"
#include "mc6801_trace.h"

// Instruction types

enum {
	ILLEGAL = 0, INHERENT, WORD_IMMEDIATE, IMMEDIATE, EXTENDED, DIRECT,
	INDEXED, RELATIVE,
};

// Array of instructions.  A NULL mnemonic will be replaced with "*".

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
	{ NULL, ILLEGAL },
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
	{ NULL, ILLEGAL },
	{ "EORB", IMMEDIATE },
	{ "ADCB", IMMEDIATE },
	{ "ORAB", IMMEDIATE },
	{ "ADDB", IMMEDIATE },
	{ "LDD", WORD_IMMEDIATE },
	{ NULL, ILLEGAL },
	{ "LDX", WORD_IMMEDIATE },
	{ NULL, ILLEGAL },
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

// Interrupt vector names
static char const * const irq_names[8] = {
	"[SCI]", "[TOF]", "[OCF]", "[ICF]",
	"[IRQ1]", "[SWI]", "[NMI]", "[RESET]"
};

// Current state

struct mc6801_trace {
	struct MC6801 *cpu;
	event_ticks start_tick;
};

// Iterate over supplied data

struct byte_iter {
	unsigned nbytes;
	unsigned index;  // current index into bytes
	uint8_t *bytes;
};

#define sex8(v) ((int8_t)(v))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct mc6801_trace *mc6801_trace_new(struct MC6801 *cpu) {
	struct mc6801_trace *tracer = xmalloc(sizeof(*tracer));
	*tracer = (struct mc6801_trace){0};
	tracer->cpu = cpu;
	tracer->start_tick = event_current_tick;
	return tracer;
}

void mc6801_trace_free(struct mc6801_trace *tracer) {
	free(tracer);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void print_line_end(struct mc6801_trace *tracer);

void mc6801_trace_vector(struct mc6801_trace *tracer, uint16_t vec,
			 unsigned nbytes, uint8_t *bytes) {
	if (nbytes == 0)
		return;

	const char *name = irq_names[(vec & 15) >> 1];

	char bytes_string[(MC6801_MAX_TRACE_BYTES*2)+1];
	for (unsigned i = 0; i < nbytes; ++i) {
		snprintf(bytes_string + i*2, 3, "%02x", bytes[i]);
	}

	printf("%04x| %-12s%-24s", vec, bytes_string, name);
	print_line_end(tracer);
}

static unsigned next_byte(struct byte_iter *iter) {
	assert(iter->index < iter->nbytes);
	++iter->nbytes;
	return iter->bytes[iter->index++];
}

static unsigned next_word(struct byte_iter *iter) {
	unsigned v = next_byte(iter);
	return (v << 8) | next_byte(iter);
}

void mc6801_trace_instruction(struct mc6801_trace *tracer, uint16_t pc,
			      unsigned nbytes, uint8_t *bytes) {

	if (nbytes == 0)
		return;

	struct byte_iter iter = { .nbytes = nbytes, .bytes = bytes };

	unsigned ins = next_byte(&iter);
	const char *mnemonic = instructions[ins].mnemonic;
	if (!mnemonic) {
		mnemonic = "*";
	}
	int ins_type = instructions[ins].type;

	char operand_text[19];
	operand_text[0] = '\0';

	switch (ins_type) {
	default:
	case ILLEGAL: case INHERENT:
		break;

	case IMMEDIATE:
		snprintf(operand_text, sizeof(operand_text), "#$%02x", next_byte(&iter));
		break;

	case DIRECT:
		snprintf(operand_text, sizeof(operand_text), "<$%02x", next_byte(&iter));
		break;

	case WORD_IMMEDIATE:
		snprintf(operand_text, sizeof(operand_text), "#$%04x", next_word(&iter));
		break;

	case EXTENDED:
		snprintf(operand_text, sizeof(operand_text), "$%04x", next_word(&iter));
		break;

	case INDEXED:
		snprintf(operand_text, sizeof(operand_text), "$%02x,X", next_byte(&iter));
		break;

	case RELATIVE: {
		unsigned v = next_byte(&iter);
		v = (pc + iter.index + sex8(v)) & 0xffff;
		snprintf(operand_text, sizeof(operand_text), "$%04x", v);
	} break;

	}

	char bytes_string[(MC6801_MAX_TRACE_BYTES*2)+1];
	for (unsigned i = 0; i < iter.index; ++i) {
		snprintf(bytes_string + i*2, 3, "%02x", bytes[i]);
	}

	struct MC6801 *cpu = tracer->cpu;
	printf("%04x| %-12s%-6s%-18s", pc, bytes_string, mnemonic, operand_text);
	printf("cc=%02x a=%02x b=%02x x=%04x sp=%04x",
	       cpu->reg_cc | 0xc0, MC6801_REG_A(cpu), MC6801_REG_B(cpu),
	       cpu->reg_x, cpu->reg_sp);
	print_line_end(tracer);
}

static void print_line_end(struct mc6801_trace *tracer) {
	// XXX currently no way to reset start_tick when turning trace mode
	// on/off, so first instruction after switching on will have crazy dt.
	int dt = event_tick_delta(event_current_tick, tracer->start_tick);
	tracer->start_tick = event_current_tick;

	if (logging.trace_cpu_timing) {
		printf("  dt=%d", dt);
	}

	printf("\n");
	fflush(stdout);
}
