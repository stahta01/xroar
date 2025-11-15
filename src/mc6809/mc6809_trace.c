/** \file
 *
 *  \brief Motorola MC6809 CPU tracing.
 *
 *  \copyright Copyright 2005-2025 Ciaran Anscomb
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

#include "pl-string.h"
#include "xalloc.h"

#include "events.h"
#include "logging.h"
#include "mc6809.h"
#include "mc6809_trace.h"

// Instruction types

enum {
	ILLEGAL = 0, INHERENT, WORD_IMMEDIATE, IMMEDIATE, EXTENDED, DIRECT,
	INDEXED, RELATIVE, LONG_RELATIVE, STACKS, STACKU, REGISTER,
};

// Three arrays of instructions, one for each page.  A NULL mnemonic will be
// replaced with "*".

static struct {
	const char *mnemonic;
	int type;
} const instructions[3][256] = {
	{
		// 0x00 - 0x0F
		{ "NEG", DIRECT },
		{ "NEG*", DIRECT },
		{ "NGC*", DIRECT },
		{ "COM", DIRECT },
		{ "LSR", DIRECT },
		{ "LSR*", DIRECT },
		{ "ROR", DIRECT },
		{ "ASR", DIRECT },
		{ "LSL", DIRECT },
		{ "ROL", DIRECT },
		{ "DEC", DIRECT },
		{ "DEC*", DIRECT },
		{ "INC", DIRECT },
		{ "TST", DIRECT },
		{ "JMP", DIRECT },
		{ "CLR", DIRECT },
		// 0x10 - 0x1F
		{ NULL, ILLEGAL },  // Page byte - handled explicitly
		{ NULL, ILLEGAL },  // Page byte - handled explicitly
		{ "NOP", INHERENT },
		{ "SYNC", INHERENT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LBRA", LONG_RELATIVE },
		{ "LBSR", LONG_RELATIVE },
		{ "LACC*", ILLEGAL },  // "LSACC"
		{ "DAA", INHERENT },
		{ "ORCC", IMMEDIATE },
		{ NULL, ILLEGAL },
		{ "ANDCC", IMMEDIATE },
		{ "SEX", INHERENT },
		{ "EXG", REGISTER },
		{ "TFR", REGISTER },
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
		{ "LEAX", INDEXED },
		{ "LEAY", INDEXED },
		{ "LEAS", INDEXED },
		{ "LEAU", INDEXED },
		{ "PSHS", STACKS },
		{ "PULS", STACKS },
		{ "PSHU", STACKU },
		{ "PULU", STACKU },
		{ NULL, ILLEGAL },
		{ "RTS", INHERENT },
		{ "ABX", INHERENT },
		{ "RTI", INHERENT },
		{ "CWAI", IMMEDIATE },
		{ "MUL", INHERENT },
		{ NULL, ILLEGAL },
		{ "SWI", INHERENT },
		// 0x40 - 0x4F
		{ "NEGA", INHERENT },
		{ "NEGA*", INHERENT },
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
		{ "CLA2*", INHERENT },  // "CLRA2"
		{ "CLRA", INHERENT },
		// 0x50 - 0x5F
		{ "NEGB", INHERENT },
		{ "NEGB*", INHERENT },
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
		{ "CLB2*", INHERENT },  // "CLRB2"
		{ "CLRB", INHERENT },
		// 0x60 - 0x6F
		{ "NEG", INDEXED },
		{ "NEG*", INDEXED },
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
		{ "NEG*", EXTENDED },
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
		{ "LDA", IMMEDIATE },
		{ "DSCA*", IMMEDIATE },
		{ "EORA", IMMEDIATE },
		{ "ADCA", IMMEDIATE },
		{ "ORA", IMMEDIATE },
		{ "ADDA", IMMEDIATE },
		{ "CMPX", WORD_IMMEDIATE },
		{ "BSR", RELATIVE },
		{ "LDX", WORD_IMMEDIATE },
		{ "STXI*", IMMEDIATE },
		// 0x90 - 0x9F
		{ "SUBA", DIRECT },
		{ "CMPA", DIRECT },
		{ "SBCA", DIRECT },
		{ "SUBD", DIRECT },
		{ "ANDA", DIRECT },
		{ "BITA", DIRECT },
		{ "LDA", DIRECT },
		{ "STA", DIRECT },
		{ "EORA", DIRECT },
		{ "ADCA", DIRECT },
		{ "ORA", DIRECT },
		{ "ADDA", DIRECT },
		{ "CMPX", DIRECT },
		{ "JSR", DIRECT },
		{ "LDX", DIRECT },
		{ "STX", DIRECT },
		// 0xA0 - 0xAF
		{ "SUBA", INDEXED },
		{ "CMPA", INDEXED },
		{ "SBCA", INDEXED },
		{ "SUBD", INDEXED },
		{ "ANDA", INDEXED },
		{ "BITA", INDEXED },
		{ "LDA", INDEXED },
		{ "STA", INDEXED },
		{ "EORA", INDEXED },
		{ "ADCA", INDEXED },
		{ "ORA", INDEXED },
		{ "ADDA", INDEXED },
		{ "CMPX", INDEXED },
		{ "JSR", INDEXED },
		{ "LDX", INDEXED },
		{ "STX", INDEXED },
		// 0xB0 - 0xBF
		{ "SUBA", EXTENDED },
		{ "CMPA", EXTENDED },
		{ "SBCA", EXTENDED },
		{ "SUBD", EXTENDED },
		{ "ANDA", EXTENDED },
		{ "BITA", EXTENDED },
		{ "LDA", EXTENDED },
		{ "STA", EXTENDED },
		{ "EORA", EXTENDED },
		{ "ADCA", EXTENDED },
		{ "ORA", EXTENDED },
		{ "ADDA", EXTENDED },
		{ "CMPX", EXTENDED },
		{ "JSR", EXTENDED },
		{ "LDX", EXTENDED },
		{ "STX", EXTENDED },
		// 0xC0 - 0xCF
		{ "SUBB", IMMEDIATE },
		{ "CMPB", IMMEDIATE },
		{ "SBCB", IMMEDIATE },
		{ "ADDD", WORD_IMMEDIATE },
		{ "ANDB", IMMEDIATE },
		{ "BITB", IMMEDIATE },
		{ "LDB", IMMEDIATE },
		{ "DSCB*", IMMEDIATE },
		{ "EORB", IMMEDIATE },
		{ "ADCB", IMMEDIATE },
		{ "ORB", IMMEDIATE },
		{ "ADDB", IMMEDIATE },
		{ "LDD", WORD_IMMEDIATE },
		{ "HCF*", INHERENT },
		{ "LDU", WORD_IMMEDIATE },
		{ "STUI*", IMMEDIATE },
		// 0xD0 - 0xDF
		{ "SUBB", DIRECT },
		{ "CMPB", DIRECT },
		{ "SBCB", DIRECT },
		{ "ADDD", DIRECT },
		{ "ANDB", DIRECT },
		{ "BITB", DIRECT },
		{ "LDB", DIRECT },
		{ "STB", DIRECT },
		{ "EORB", DIRECT },
		{ "ADCB", DIRECT },
		{ "ORB", DIRECT },
		{ "ADDB", DIRECT },
		{ "LDD", DIRECT },
		{ "STD", DIRECT },
		{ "LDU", DIRECT },
		{ "STU", DIRECT },
		// 0xE0 - 0xEF
		{ "SUBB", INDEXED },
		{ "CMPB", INDEXED },
		{ "SBCB", INDEXED },
		{ "ADDD", INDEXED },
		{ "ANDB", INDEXED },
		{ "BITB", INDEXED },
		{ "LDB", INDEXED },
		{ "STB", INDEXED },
		{ "EORB", INDEXED },
		{ "ADCB", INDEXED },
		{ "ORB", INDEXED },
		{ "ADDB", INDEXED },
		{ "LDD", INDEXED },
		{ "STD", INDEXED },
		{ "LDU", INDEXED },
		{ "STU", INDEXED },
		// 0xF0 - 0xFF
		{ "SUBB", EXTENDED },
		{ "CMPB", EXTENDED },
		{ "SBCB", EXTENDED },
		{ "ADDD", EXTENDED },
		{ "ANDB", EXTENDED },
		{ "BITB", EXTENDED },
		{ "LDB", EXTENDED },
		{ "STB", EXTENDED },
		{ "EORB", EXTENDED },
		{ "ADCB", EXTENDED },
		{ "ORB", EXTENDED },
		{ "ADDB", EXTENDED },
		{ "LDD", EXTENDED },
		{ "STD", EXTENDED },
		{ "LDU", EXTENDED },
		{ "STU", EXTENDED }
	}, {

		// 0x1000 - 0x100F
		{ "NEG*", DIRECT },
		{ "NEG*", DIRECT },
		{ "NGC*", DIRECT },
		{ "COM*", DIRECT },
		{ "LSR*", DIRECT },
		{ "LSR*", DIRECT },
		{ "ROR*", DIRECT },
		{ "ASR*", DIRECT },
		{ "LSL*", DIRECT },
		{ "ROL*", DIRECT },
		{ "DEC*", DIRECT },
		{ "DEC*", DIRECT },
		{ "INC*", DIRECT },
		{ "TST*", DIRECT },
		{ NULL, ILLEGAL },
		{ "CLR*", DIRECT },
		// 0x1010 - 0x101F
		{ NULL, ILLEGAL },  // Page byte - handled explicitly
		{ NULL, ILLEGAL },  // Page byte - handled explicitly
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x1020 - 0x102F
		{ "LBRA*", LONG_RELATIVE },
		{ "LBRN", LONG_RELATIVE },
		{ "LBHI", LONG_RELATIVE },
		{ "LBLS", LONG_RELATIVE },
		{ "LBCC", LONG_RELATIVE },
		{ "LBCS", LONG_RELATIVE },
		{ "LBNE", LONG_RELATIVE },
		{ "LBEQ", LONG_RELATIVE },
		{ "LBVC", LONG_RELATIVE },
		{ "LBVS", LONG_RELATIVE },
		{ "LBPL", LONG_RELATIVE },
		{ "LBMI", LONG_RELATIVE },
		{ "LBGE", LONG_RELATIVE },
		{ "LBLT", LONG_RELATIVE },
		{ "LBGT", LONG_RELATIVE },
		{ "LBLE", LONG_RELATIVE },
		// 0x1030 - 0x103F
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "SWI2", INHERENT },

		// 0x1040 - 0x104F
		{ "NEGA*", INHERENT },
		{ "NEGA*", INHERENT },
		{ "NGCA*", INHERENT },
		{ "COMA*", INHERENT },
		{ "LSRA*", INHERENT },
		{ "LSRA*", INHERENT },
		{ "RORA*", INHERENT },
		{ "ASRA*", INHERENT },
		{ "LSLA*", INHERENT },
		{ "ROLA*", INHERENT },
		{ "DECA*", INHERENT },
		{ "DECA*", INHERENT },
		{ "INCA*", INHERENT },
		{ "TSTA*", INHERENT },
		{ "CLA2*", INHERENT },  // "CLRA2"
		{ "CLRA*", INHERENT },
		// 0x1050 - 0x105F
		{ "NEGB*", INHERENT },
		{ "NEGB*", INHERENT },
		{ "NGCB*", INHERENT },
		{ "COMB*", INHERENT },
		{ "LSRB*", INHERENT },
		{ "LSRB*", INHERENT },
		{ "RORB*", INHERENT },
		{ "ASRB*", INHERENT },
		{ "LSLB*", INHERENT },
		{ "ROLB*", INHERENT },
		{ "DECB*", INHERENT },
		{ "DECB*", INHERENT },
		{ "INCB*", INHERENT },
		{ "TSTB*", INHERENT },
		{ "CLB2*", INHERENT },  // "CLRB2"
		{ "CLRB*", INHERENT },
		// 0x1060 - 0x106F
		{ "NEG*", INDEXED },
		{ "NEG*", INDEXED },
		{ "NGC*", INDEXED },
		{ "COM*", INDEXED },
		{ "LSR*", INDEXED },
		{ "LSR*", INDEXED },
		{ "ROR*", INDEXED },
		{ "ASR*", INDEXED },
		{ "LSL*", INDEXED },
		{ "ROL*", INDEXED },
		{ "DEC*", INDEXED },
		{ "DEC*", INDEXED },
		{ "INC*", INDEXED },
		{ "TST*", INDEXED },
		{ NULL, ILLEGAL },
		{ "CLR*", INDEXED },
		// 0x1070 - 0x107F
		{ "NEG*", EXTENDED },
		{ "NEG*", EXTENDED },
		{ "NGC*", EXTENDED },
		{ "COM*", EXTENDED },
		{ "LSR*", EXTENDED },
		{ "LSR*", EXTENDED },
		{ "ROR*", EXTENDED },
		{ "ASR*", EXTENDED },
		{ "LSL*", EXTENDED },
		{ "ROL*", EXTENDED },
		{ "DEC*", EXTENDED },
		{ "DEC*", EXTENDED },
		{ "INC*", EXTENDED },
		{ "TST*", EXTENDED },
		{ NULL, ILLEGAL },
		{ "CLR*", EXTENDED },
		// 0x1080 - 0x108F
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPD", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPY", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
		{ "LDY", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
		// 0x1090 - 0x109F
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPD", DIRECT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPY", DIRECT },
		{ NULL, ILLEGAL },
		{ "LDY", DIRECT },
		{ "STY", DIRECT },
		// 0x10A0 - 0x10AF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPD", INDEXED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPY", INDEXED },
		{ NULL, ILLEGAL },
		{ "LDY", INDEXED },
		{ "STY", INDEXED },
		// 0x10B0 - 0x10BF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPD", EXTENDED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPY", EXTENDED },
		{ NULL, ILLEGAL },
		{ "LDY", EXTENDED },
		{ "STY", EXTENDED },
		// 0x10C0 - 0x10CF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDS", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
		// 0x10D0 - 0x10DF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDS", DIRECT },
		{ "STS", DIRECT },
		// 0x10E0 - 0x10EF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDS", INDEXED },
		{ "STS", INDEXED },
		// 0x10F0 - 0x10FF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDS", EXTENDED },
		{ "STS", EXTENDED }
	}, {

		// 0x1100 - 0x110F
		{ "NEG*", DIRECT },
		{ "NEG*", DIRECT },
		{ "NGC*", DIRECT },
		{ "COM*", DIRECT },
		{ "LSR*", DIRECT },
		{ "LSR*", DIRECT },
		{ "ROR*", DIRECT },
		{ "ASR*", DIRECT },
		{ "LSL*", DIRECT },
		{ "ROL*", DIRECT },
		{ "DEC*", DIRECT },
		{ "DEC*", DIRECT },
		{ "INC*", DIRECT },
		{ "TST*", DIRECT },
		{ NULL, ILLEGAL },
		{ "CLR*", DIRECT },
		// 0x1110 - 0x111F
		{ NULL, ILLEGAL },  // Page byte - handled explicitly
		{ NULL, ILLEGAL },  // Page byte - handled explicitly
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x1120 - 0x112F
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x1130 - 0x113F
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "SWI3", INHERENT },
		// 0x1140 - 0x114F
		{ "NEGA*", INHERENT },
		{ "NEGA*", INHERENT },
		{ "NGCA*", INHERENT },
		{ "COMA*", INHERENT },
		{ "LSRA*", INHERENT },
		{ "LSRA*", INHERENT },
		{ "RORA*", INHERENT },
		{ "ASRA*", INHERENT },
		{ "LSLA*", INHERENT },
		{ "ROLA*", INHERENT },
		{ "DECA*", INHERENT },
		{ "DECA*", INHERENT },
		{ "INCA*", INHERENT },
		{ "TSTA*", INHERENT },
		{ "CLA2*", INHERENT },  // "CLRA2"
		{ "CLRA*", INHERENT },
		// 0x1150 - 0x115F
		{ "NEGB*", INHERENT },
		{ "NEGB*", INHERENT },
		{ "NGCB*", INHERENT },
		{ "COMB*", INHERENT },
		{ "LSRB*", INHERENT },
		{ "LSRB*", INHERENT },
		{ "RORB*", INHERENT },
		{ "ASRB*", INHERENT },
		{ "LSLB*", INHERENT },
		{ "ROLB*", INHERENT },
		{ "DECB*", INHERENT },
		{ "DECB*", INHERENT },
		{ "INCB*", INHERENT },
		{ "TSTB*", INHERENT },
		{ "CLB2*", INHERENT },  // "CLRB2"
		{ "CLRB*", INHERENT },
		// 0x1160 - 0x116F
		{ "NEG*", INDEXED },
		{ "NEG*", INDEXED },
		{ "NGC*", INDEXED },
		{ "COM*", INDEXED },
		{ "LSR*", INDEXED },
		{ "LSR*", INDEXED },
		{ "ROR*", INDEXED },
		{ "ASR*", INDEXED },
		{ "LSL*", INDEXED },
		{ "ROL*", INDEXED },
		{ "DEC*", INDEXED },
		{ "DEC*", INDEXED },
		{ "INC*", INDEXED },
		{ "TST*", INDEXED },
		{ NULL, ILLEGAL },
		{ "CLR*", INDEXED },
		// 0x1170 - 0x117F
		{ "NEG*", EXTENDED },
		{ "NEG*", EXTENDED },
		{ "NGC*", EXTENDED },
		{ "COM*", EXTENDED },
		{ "LSR*", EXTENDED },
		{ "LSR*", EXTENDED },
		{ "ROR*", EXTENDED },
		{ "ASR*", EXTENDED },
		{ "LSL*", EXTENDED },
		{ "ROL*", EXTENDED },
		{ "DEC*", EXTENDED },
		{ "DEC*", EXTENDED },
		{ "INC*", EXTENDED },
		{ "TST*", EXTENDED },
		{ NULL, ILLEGAL },
		{ "CLR*", EXTENDED },
		// 0x1180 - 0x118F
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPU", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPS", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x1190 - 0x119F
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPU", DIRECT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPS", DIRECT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x11A0 - 0x11AF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPU", INDEXED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPS", INDEXED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x11B0 - 0x11BF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPU", EXTENDED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CMPS", EXTENDED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x11C0 - 0x11CF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x11D0 - 0x11DF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x11E0 - 0x11EF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x11F0 - 0x11FF
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
	}
};

// Indexed addressing modes

enum {
	IDX_PI1, IDX_PI2, IDX_PD1, IDX_PD2,
	IDX_OFF0, IDX_OFFB, IDX_OFFA, IDX_OFFA_7,
	IDX_OFF8, IDX_OFF16, IDX_ILL_A, IDX_OFFD,
	IDX_PCR8, IDX_PCR16, IDX_ILL_E, IDX_EXT16
};

// Indexed mode format strings (excluding 5-bit offset).  The leading and
// trailing %s account for the optional brackets in indirect modes.  8-bit
// offsets include an extra %s to indicate sign.

static char const * const idx_fmts[16] = {
	"%s,%s+%s",
	"%s,%s++%s",
	"%s,-%s%s",
	"%s,--%s%s",
	"%s,%s%s",
	"%sB,%s%s",
	"%sA,%s%s",
	"%s,%s *%s",
	"%s%s$%02x,%s%s",
	"%s$%04x,%s%s",
	"%s*%s",
	"%sD,%s%s",
	"%s<$%04x,PCR%s",
	"%s>$%04x,PCR%s",
	"%s*%s",
	"%s$%04x%s"
};

// Inter-register operation postbyte
static char const * const tfr_regs[16] = {
	"D", "X", "Y", "U", "S", "PC", "*", "*",
	"A", "B", "CC", "DP", "*", "*", "*", "*"
};

// Indexed addressing postbyte registers
static char const * const idx_regs[4] = { "X", "Y", "U", "S" };

// Interrupt vector names
static char const * const irq_names[8] = {
	"[?]", "[SWI3]", "[SWI2]", "[FIRQ]",
	"[IRQ]", "[SWI]", "[NMI]", "[RESET]"
};

// Current state

struct mc6809_trace {
	struct MC6809 *cpu;
	event_ticks start_tick;
};

// Iterate over supplied data

struct byte_iter {
	unsigned nbytes;
	unsigned index;  // current index into bytes
	uint8_t *bytes;
};

// Helper functions

static unsigned next_byte(struct byte_iter *iter);
static unsigned next_word(struct byte_iter *iter);

static char *stack_operand(char *dst, char *dend, unsigned *postbyte, const char *r);

#define sex5(v) ((int)((v) & 0x0f) - (int)((v) & 0x10))
#define sex8(v) ((int8_t)(v))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct mc6809_trace *mc6809_trace_new(struct MC6809 *cpu) {
	struct mc6809_trace *tracer = xmalloc(sizeof(*tracer));
	*tracer = (struct mc6809_trace){0};
	tracer->cpu = cpu;
	tracer->start_tick = event_current_tick;
	return tracer;
}

void mc6809_trace_free(struct mc6809_trace *tracer) {
	free(tracer);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void print_line_end(struct mc6809_trace *tracer);

void mc6809_trace_vector(struct mc6809_trace *tracer, uint16_t vec,
			 unsigned nbytes, uint8_t *bytes) {
	if (nbytes == 0)
		return;

	const char *name = irq_names[(vec & 15) >> 1];

	char bytes_string[(MC6809_MAX_TRACE_BYTES*2)+1];
	for (unsigned i = 0; i < nbytes; ++i) {
		snprintf(bytes_string + i*2, 3, "%02x", bytes[i]);
	}

	printf("%04x| %-12s%-24s", vec, bytes_string, name);
	print_line_end(tracer);
}

void mc6809_trace_instruction(struct mc6809_trace *tracer, uint16_t pc,
			      unsigned nbytes, uint8_t *bytes) {

	if (nbytes == 0)
		return;

	struct byte_iter iter = { .nbytes = nbytes, .bytes = bytes };

	// CPU code will ensure we are only presented with one - the first -
	// page byte, even though they can be chained indefinitely.
	int page = 0;
	if (bytes[0] == 0x10) {
		page = 1;
		(void)next_byte(&iter);
	} else if (bytes[0] == 0x11) {
		page = 2;
		(void)next_byte(&iter);
	}

	unsigned ins = next_byte(&iter);
	const char *mnemonic = instructions[page][ins].mnemonic;
	if (!mnemonic) {
		mnemonic = "*";
	}
	int ins_type = instructions[page][ins].type;

	// Longest operand is full stack push/pull: "CC,A,B,DP,X,Y,U,PC" , 18
	// characters.  Round up.
	char operand_text[24];
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

	case STACKS:
	case STACKU: {
		unsigned postbyte = next_byte(&iter);
		char *buf = operand_text;
		char *bufend = buf + sizeof(operand_text) - 1;
		const char *reg6 = (ins_type == STACKS) ? "U" : "S";
		buf = stack_operand(buf, bufend, &postbyte, "CC");
		buf = stack_operand(buf, bufend, &postbyte, "A");
		buf = stack_operand(buf, bufend, &postbyte, "B");
		buf = stack_operand(buf, bufend, &postbyte, "DP");
		buf = stack_operand(buf, bufend, &postbyte, "X");
		buf = stack_operand(buf, bufend, &postbyte, "Y");
		buf = stack_operand(buf, bufend, &postbyte, reg6);
		buf = stack_operand(buf, bufend, &postbyte, "PC");
	} break;

	case REGISTER: {
		unsigned postbyte = next_byte(&iter);
		snprintf(operand_text, sizeof(operand_text), "%s,%s",
			 tfr_regs[(postbyte>>4)&15], tfr_regs[postbyte&15]);
	} break;

	case INDEXED: {
		unsigned postbyte = next_byte(&iter);
		const char *idx_reg = idx_regs[(postbyte >> 5) & 3];

		if ((postbyte & 0x80) == 0) {
			// 5-bit offsets considered separately
			snprintf(operand_text, sizeof(operand_text), "%d,%s", sex5(postbyte), idx_reg);
		} else {
			_Bool idx_indirect = postbyte & 0x10;
			unsigned idx_mode = postbyte & 0x0f;
			const char *pre = idx_indirect ? "[" : "";
			const char *post = idx_indirect ? "]" : "";
			const char *fmt = idx_fmts[idx_mode];

			switch (idx_mode) {
				// Anything but 5-bit offsets
			default:
			case IDX_PI1: case IDX_PI2: case IDX_PD1: case IDX_PD2:
			case IDX_OFF0: case IDX_OFFB: case IDX_OFFA: case IDX_OFFA_7:
			case IDX_OFFD:
				snprintf(operand_text, sizeof(operand_text), fmt, pre, idx_reg, post);
				break;
			case IDX_OFF8: {
				unsigned uvalue8 = next_byte(&iter);
				int value8 = sex8(uvalue8);
				snprintf(operand_text, sizeof(operand_text), fmt, pre, (value8<0)?"-":"", (value8<0)?-value8:value8, idx_reg, post);
			} break;
			case IDX_OFF16: {
				unsigned uvalue16 = next_word(&iter);
				snprintf(operand_text, sizeof(operand_text), fmt, pre, uvalue16, idx_reg, post);
			} break;
			case IDX_PCR8: {
				unsigned uvalue8 = next_byte(&iter);
				int value8 = sex8(uvalue8);
				snprintf(operand_text, sizeof(operand_text), fmt, pre, (uint16_t)(pc + iter.index + value8), post);
			} break;
			case IDX_PCR16: {
				unsigned uvalue16 = next_word(&iter);
				snprintf(operand_text, sizeof(operand_text), fmt, pre, (uint16_t)(pc + iter.index + uvalue16), post);
			} break;
			case IDX_EXT16: {
				unsigned uvalue16 = next_word(&iter);
				snprintf(operand_text, sizeof(operand_text), fmt, pre, uvalue16, post);
			} break;
			case IDX_ILL_A: case IDX_ILL_E:
				snprintf(operand_text, sizeof(operand_text), fmt, pre, post);
				break;
			}
		}

	} break;

	case RELATIVE: {
		unsigned v = next_byte(&iter);
		v = (pc + iter.index + sex8(v)) & 0xffff;
		snprintf(operand_text, sizeof(operand_text), "$%04x", v);
	} break;

	case LONG_RELATIVE: {
		unsigned v = next_word(&iter);
		v = (pc + iter.index + v) & 0xffff;
		snprintf(operand_text, sizeof(operand_text), "$%04x", v);
	} break;

	}

	char bytes_string[(MC6809_MAX_TRACE_BYTES*2)+1];
	for (unsigned i = 0; i < iter.index; ++i) {
		snprintf(bytes_string + i*2, 3, "%02x", bytes[i]);
	}

	struct MC6809 *cpu = tracer->cpu;
	printf("%04x| %-12s%-6s%-18s", pc, bytes_string, mnemonic, operand_text);
	printf("  cc=%02x a=%02x b=%02x dp=%02x "
	       "x=%04x y=%04x u=%04x s=%04x",
	       cpu->reg_cc, MC6809_REG_A(cpu), MC6809_REG_B(cpu), cpu->reg_dp,
	       cpu->reg_x, cpu->reg_y, cpu->reg_u, cpu->reg_s);
	print_line_end(tracer);
}

static void print_line_end(struct mc6809_trace *tracer) {
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Fetch next byte from an array, maintaining an index

static unsigned next_byte(struct byte_iter *iter) {
	assert(iter->index < iter->nbytes);
	++iter->nbytes;
	return iter->bytes[iter->index++];
}

// Fetch next word from an array using next_byte()

static unsigned next_word(struct byte_iter *iter) {
	unsigned v = next_byte(iter);
	return (v << 8) | next_byte(iter);
}

// Helper for stack ops.  Uses pl_estrcpy() to append register name r.  Shifts
// *postbyte one bit to the right and also appends a comma if non-zero bits
// remain.  As pl_estrcpy(), returns pointer to new nul terminator at end of
// string or NULL if it ran out of space.

static char *stack_operand(char *dst, char *dend, unsigned *postbyte, const char *r) {
	_Bool pr = *postbyte & 1;
	*postbyte >>= 1;
	if (pr) {
		dst = pl_estrcpy(dst, dend, r);
		if (*postbyte) {
			dst = pl_estrcpy(dst, dend, ",");
		}
	}
	return dst;
}
