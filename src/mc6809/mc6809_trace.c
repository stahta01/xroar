/** \file
 *
 *  \brief Motorola MC6809 CPU tracing.
 *
 *  \copyright Copyright 2005-2024 Ciaran Anscomb
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

#include "events.h"
#include "logging.h"
#include "mc6809.h"
#include "mc6809_trace.h"

/* Instruction types.  PAGE0, PAGE2 and PAGE3 switch which page is selected. */

enum {
	PAGE0 = 0, PAGE2 = 1, PAGE3 = 2, ILLEGAL,
	INHERENT, WORD_IMMEDIATE, IMMEDIATE, EXTENDED,
	DIRECT, INDEXED, RELATIVE, LONG_RELATIVE,
	STACKS, STACKU, REGISTER, IRQVECTOR,
};

/* Three arrays of instructions, one for each of PAGE0, PAGE2 and PAGE3 */

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
		{ "*", PAGE2 },
		{ "*", PAGE3 },
		{ "NOP", INHERENT },
		{ "SYNC", INHERENT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LBRA", LONG_RELATIVE },
		{ "LBSR", LONG_RELATIVE },
		{ "*", ILLEGAL },
		{ "DAA", INHERENT },
		{ "ORCC", IMMEDIATE },
		{ "*", ILLEGAL },
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
		{ "*", ILLEGAL },
		{ "RTS", INHERENT },
		{ "ABX", INHERENT },
		{ "RTI", INHERENT },
		{ "CWAI", IMMEDIATE },
		{ "MUL", INHERENT },
		{ "*", ILLEGAL },
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
		{ "*", ILLEGAL },
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
		{ "*", ILLEGAL },
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
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1010 - 0x101F
		{ "*", PAGE2 },
		{ "*", PAGE2 },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
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
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
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
		{ "*", ILLEGAL },
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
		{ "*", ILLEGAL },
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
		{ "*", ILLEGAL },
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
		{ "*", ILLEGAL },
		{ "CLR*", EXTENDED },
		// 0x1080 - 0x108F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPD", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPY", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		{ "LDY", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		// 0x1090 - 0x109F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPD", DIRECT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPY", DIRECT },
		{ "*", ILLEGAL },
		{ "LDY", DIRECT },
		{ "STY", DIRECT },
		// 0x10A0 - 0x10AF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPD", INDEXED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPY", INDEXED },
		{ "*", ILLEGAL },
		{ "LDY", INDEXED },
		{ "STY", INDEXED },
		// 0x10B0 - 0x10BF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPD", EXTENDED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPY", EXTENDED },
		{ "*", ILLEGAL },
		{ "LDY", EXTENDED },
		{ "STY", EXTENDED },
		// 0x10C0 - 0x10CF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDS", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		// 0x10D0 - 0x10DF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDS", DIRECT },
		{ "STS", DIRECT },
		// 0x10E0 - 0x10EF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDS", INDEXED },
		{ "STS", INDEXED },
		// 0x10F0 - 0x10FF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "LDS", EXTENDED },
		{ "STS", EXTENDED }
	}, {

		// 0x1100 - 0x110F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1110 - 0x111F
		{ "*", PAGE3 },
		{ "*", PAGE3 },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1120 - 0x112F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1130 - 0x113F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "SWI3", INHERENT },
		// 0x1140 - 0x114F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1150 - 0x115F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1160 - 0x116F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1170 - 0x117F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1180 - 0x118F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPU", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPS", WORD_IMMEDIATE },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x1190 - 0x119F
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPU", DIRECT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPS", DIRECT },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x11A0 - 0x11AF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPU", INDEXED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPS", INDEXED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x11B0 - 0x11BF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPU", EXTENDED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "CMPS", EXTENDED },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x11C0 - 0x11CF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x11D0 - 0x11DF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x11E0 - 0x11EF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		// 0x11F0 - 0x11FF
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
		{ "*", ILLEGAL },
	}
};

/* The next byte is expected to be one of these, with the special exceptions of
 * WANT_PRINT, which indicates a trace line is complete. */

enum {
	WANT_INSTRUCTION,
	WANT_INSTRUCTION2,
	WANT_IRQ_VECTOR,
	WANT_IRQ_VECTOR2,  // second byte
	WANT_IDX_POSTBYTE,
	WANT_VALUE,
	WANT_PRINT
};

/* Sequences of expected bytes */

static int const state_list_irq[] = { WANT_IRQ_VECTOR2, WANT_PRINT };
static int const state_list_inherent[] = { WANT_PRINT };
static int const state_list_idx[] = { WANT_IDX_POSTBYTE };
static int const state_list_imm8[] = { WANT_VALUE, WANT_PRINT };
static int const state_list_imm16[] = { WANT_VALUE, WANT_VALUE, WANT_PRINT };

/* Indexed addressing modes */

enum {
	IDX_PI1, IDX_PI2, IDX_PD1, IDX_PD2,
	IDX_OFF0, IDX_OFFB, IDX_OFFA, IDX_OFFA_7,
	IDX_OFF8, IDX_OFF16, IDX_ILL_A, IDX_OFFD,
	IDX_PCR8, IDX_PCR16, IDX_ILL_E, IDX_EXT16,
	IDX_OFF5
};

/* Indexed mode format strings.  The leading and trailing %s account for the
 * optional brackets in indirect modes.  8-bit offsets include an extra %s to
 * indicate sign.  5-bit offsets are printed in decimal. */

static char const * const idx_fmts[17] = {
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
	"%s$%04x%s",
	"%s%d,%s%s",
};

/* Indexed mode may well fetch more data after initial postbyte */

static int const * const idx_state_lists[17] = {
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_inherent,
	state_list_imm8,
	state_list_imm16,
	state_list_inherent,
	state_list_inherent,
	state_list_imm8,
	state_list_imm16,
	state_list_inherent,
	state_list_imm16,
	state_list_inherent,
};

/* Names */

// Inter-register operation postbyte
static char const * const tfr_regs[16] = {
	"D", "X", "Y", "U", "S", "PC", "*", "*",
	"A", "B", "CC", "DP", "*", "*", "*", "*"
};

// Indexed addressing postbyte
static char const * const idx_regs[4] = { "X", "Y", "U", "S" };

// Interrupt vector names
static char const * const irq_names[8] = {
	"[?]", "[SWI3]", "[SWI2]", "[FIRQ]",
	"[IRQ]", "[SWI]", "[NMI]", "[RESET]"
};

/* Current state */

// Maximum number of bytes to hold in buffer for printing.  Note that illegal
// runs of 0x10 or 0x11 could overflow this, but the instruction decode should
// still be ok.
#define MAX_NBYTES 5

struct mc6809_trace {
	struct MC6809 *cpu;

	int state;
	event_ticks start_tick;
	int page;
	uint16_t instr_pc;
	// Note: nbytes == pc_nbytes for most things, but an illegal run of > 1
	// page byte will advance pc_nbytes but not nbytes.
	unsigned nbytes;
	unsigned pc_nbytes;
	uint8_t bytes[MAX_NBYTES];

	const char *mnemonic;
	char operand_text[19];

	int ins_type;
	const int *state_list;
	uint32_t value;
	int idx_mode;
	const char *idx_reg;
	_Bool idx_indirect;
};

static void reset_state(struct mc6809_trace *);
static void print_record(struct mc6809_trace *);

#define STACK_PRINT(t,r,c) do { \
		if (c) { strcat((t)->operand_text, r ","); } \
		else { strcat((t)->operand_text, r); } \
	} while (0)

#define sex5(v) ((int)((v) & 0x0f) - (int)((v) & 0x10))
#define sex8(v) ((int8_t)(v))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct mc6809_trace *mc6809_trace_new(struct MC6809 *cpu) {
	struct mc6809_trace *tracer = xmalloc(sizeof(*tracer));
	*tracer = (struct mc6809_trace){0};
	tracer->cpu = cpu;
	tracer->start_tick = event_current_tick;
	reset_state(tracer);
	return tracer;
}

void mc6809_trace_free(struct mc6809_trace *tracer) {
	free(tracer);
}

void mc6809_trace_reset(struct mc6809_trace *tracer) {
	reset_state(tracer);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Called at each timing checkpoint

void mc6809_trace_vector(struct mc6809_trace *tracer) {
	print_record(tracer);
	tracer->state = WANT_IRQ_VECTOR;
}

void mc6809_trace_instruction(struct mc6809_trace *tracer) {
	print_record(tracer);
	tracer->state = WANT_INSTRUCTION;
}

/* Called for each memory read */

void mc6809_trace_byte(struct mc6809_trace *tracer, uint8_t byte, uint16_t pc) {
	struct MC6809 *cpu = tracer->cpu;

	if (cpu->state == mc6809_state_next_instruction && tracer->state == WANT_INSTRUCTION) {
		tracer->instr_pc = pc;
	} else if (cpu->state == mc6809_state_irq_reset_vector && tracer->state == WANT_IRQ_VECTOR) {
		tracer->mnemonic = irq_names[(pc & 15) >> 1];
		tracer->instr_pc = pc;
	}

	if (tracer->state == WANT_PRINT) {
		return;
	}

	// Record byte if it follows current PC.  Note: nbytes not
	// incremented here: individual states will bump it if necessary.
	if (pc == (tracer->instr_pc + tracer->pc_nbytes)) {
		if (tracer->nbytes < MAX_NBYTES) {
			tracer->bytes[tracer->nbytes] = byte;
		}
	}

	switch (tracer->state) {

		// Instruction fetch
		case WANT_INSTRUCTION:
		case WANT_INSTRUCTION2:
			tracer->value = 0;
			tracer->mnemonic = instructions[tracer->page][byte].mnemonic;
			tracer->ins_type = instructions[tracer->page][byte].type;
			switch (tracer->ins_type) {
				// Change page
				case PAGE2: case PAGE3:
					tracer->state = WANT_INSTRUCTION2;
					tracer->page = tracer->ins_type;
					tracer->bytes[0] = (tracer->ins_type == PAGE2) ? 0x10 : 0x11;
					tracer->nbytes = 0;
					break;
				// Otherwise use an appropriate state list:
				default: case ILLEGAL: case INHERENT:
					tracer->state_list = state_list_inherent;
					break;
				case IMMEDIATE: case DIRECT: case RELATIVE:
				case STACKS: case STACKU: case REGISTER:
					tracer->state_list = state_list_imm8;
					break;
				case INDEXED:
					tracer->state_list = state_list_idx;
					break;
				case WORD_IMMEDIATE: case EXTENDED:
				case LONG_RELATIVE:
					tracer->state_list = state_list_imm16;
					break;
			}
			++tracer->nbytes;
			++tracer->pc_nbytes;
			break;

		// First byte of an IRQ vector
		case WANT_IRQ_VECTOR:
			tracer->value = byte;
			tracer->ins_type = IRQVECTOR;
			tracer->state_list = state_list_irq;
			++tracer->nbytes;
			++tracer->pc_nbytes;
			break;

		// Building a value byte by byte
		case WANT_VALUE:
		case WANT_IRQ_VECTOR2:
			tracer->value = (tracer->value << 8) | byte;
			++tracer->nbytes;
			++tracer->pc_nbytes;
			break;

		// Indexed postbyte - record relevant details
		case WANT_IDX_POSTBYTE:
			tracer->idx_reg = idx_regs[(byte>>5)&3];
			tracer->idx_indirect = byte & 0x10;
			tracer->idx_mode = byte & 0x0f;
			if ((byte & 0x80) == 0) {
				tracer->idx_indirect = 0;
				tracer->idx_mode = IDX_OFF5;
				tracer->value = byte & 0x1f;
			} else if (byte == 0x8f || byte == 0x90) {
				tracer->idx_reg = "W";
				tracer->idx_mode = IDX_OFF0;
			} else if (byte == 0xaf || byte == 0xb0) {
				tracer->idx_reg = "W";
				tracer->idx_mode = IDX_OFF16;
			} else if (byte == 0xcf || byte == 0xd0) {
				tracer->idx_reg = "W";
				tracer->idx_mode = IDX_PI2;
			} else if (byte == 0xef || byte == 0xf0) {
				tracer->idx_reg = "W";
				tracer->idx_mode = IDX_PD2;
			}
			tracer->state_list = idx_state_lists[tracer->idx_mode];
			++tracer->nbytes;
			++tracer->pc_nbytes;
			break;

		default:
			break;
	}

	// Get next state from state list
	if (tracer->state_list) {
		tracer->state = *(tracer->state_list++);
	}

	if (tracer->state != WANT_PRINT) {
		return;
	}

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

		case STACKS: {
			unsigned postbyte = tracer->value;
			if (postbyte & 0x01) { STACK_PRINT(tracer, "CC", postbyte & 0xfe); }
			if (postbyte & 0x02) { STACK_PRINT(tracer, "A",  postbyte & 0xfc); }
			if (postbyte & 0x04) { STACK_PRINT(tracer, "B",  postbyte & 0xf8); }
			if (postbyte & 0x08) { STACK_PRINT(tracer, "DP", postbyte & 0xf0); }
			if (postbyte & 0x10) { STACK_PRINT(tracer, "X",  postbyte & 0xe0); }
			if (postbyte & 0x20) { STACK_PRINT(tracer, "Y",  postbyte & 0xc0); }
			if (postbyte & 0x40) { STACK_PRINT(tracer, "U",  postbyte & 0x80); }
			if (postbyte & 0x80) { STACK_PRINT(tracer, "PC", 0); }
		} break;

		case STACKU: {
			unsigned postbyte = tracer->value;
			if (postbyte & 0x01) { STACK_PRINT(tracer, "CC", postbyte & 0xfe); }
			if (postbyte & 0x02) { STACK_PRINT(tracer, "A",  postbyte & 0xfc); }
			if (postbyte & 0x04) { STACK_PRINT(tracer, "B",  postbyte & 0xf8); }
			if (postbyte & 0x08) { STACK_PRINT(tracer, "DP", postbyte & 0xf0); }
			if (postbyte & 0x10) { STACK_PRINT(tracer, "X",  postbyte & 0xe0); }
			if (postbyte & 0x20) { STACK_PRINT(tracer, "Y",  postbyte & 0xc0); }
			if (postbyte & 0x40) { STACK_PRINT(tracer, "S",  postbyte & 0x80); }
			if (postbyte & 0x80) { STACK_PRINT(tracer, "PC", 0); }
		} break;

		case REGISTER:
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "%s,%s",
					tfr_regs[(tracer->value>>4)&15],
					tfr_regs[tracer->value&15]);
			break;

		case INDEXED:
			{
			const char *pre = tracer->idx_indirect ? "[" : "";
			const char *post = tracer->idx_indirect ? "]" : "";
			int value8 = sex8(tracer->value);
			int value5 = sex5(tracer->value);

			switch (tracer->idx_mode) {
			default:
			case IDX_PI1: case IDX_PI2: case IDX_PD1: case IDX_PD2:
			case IDX_OFF0: case IDX_OFFB: case IDX_OFFA: case IDX_OFFA_7:
			case IDX_OFFD:
				snprintf(tracer->operand_text, sizeof(tracer->operand_text), idx_fmts[tracer->idx_mode], pre, tracer->idx_reg, post);
				break;
			case IDX_OFF5:
				snprintf(tracer->operand_text, sizeof(tracer->operand_text), idx_fmts[tracer->idx_mode], pre, value5, tracer->idx_reg, post);
				break;
			case IDX_OFF8:
				snprintf(tracer->operand_text, sizeof(tracer->operand_text), idx_fmts[tracer->idx_mode], pre, (value8<0)?"-":"", (value8<0)?-value8:value8, tracer->idx_reg, post);
				break;
			case IDX_OFF16:
				snprintf(tracer->operand_text, sizeof(tracer->operand_text), idx_fmts[tracer->idx_mode], pre, tracer->value, tracer->idx_reg, post);
				break;
			case IDX_PCR8:
				snprintf(tracer->operand_text, sizeof(tracer->operand_text), idx_fmts[tracer->idx_mode], pre, (uint16_t)(tracer->instr_pc + tracer->pc_nbytes + value8), post);
				break;
			case IDX_PCR16:
				snprintf(tracer->operand_text, sizeof(tracer->operand_text), idx_fmts[tracer->idx_mode], pre, (uint16_t)(tracer->instr_pc + tracer->pc_nbytes + tracer->value), post);
				break;
			case IDX_EXT16:
				snprintf(tracer->operand_text, sizeof(tracer->operand_text), idx_fmts[tracer->idx_mode], pre, tracer->value, post);
				break;
			case IDX_ILL_A: case IDX_ILL_E:
				snprintf(tracer->operand_text, sizeof(tracer->operand_text), idx_fmts[tracer->idx_mode], pre, post);
				break;
			}

			} break;

		case RELATIVE:
			pc = (pc + 1 + sex8(tracer->value)) & 0xffff;
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "$%04x", pc);
			break;

		case LONG_RELATIVE:
			pc = (pc + 1 + tracer->value) & 0xffff;
			snprintf(tracer->operand_text, sizeof(tracer->operand_text), "$%04x", pc);
			break;

		case IRQVECTOR:
			break;

		default:
			break;
	}

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void reset_state(struct mc6809_trace *tracer) {
	tracer->state = WANT_PRINT;
	tracer->page = PAGE0;
	tracer->instr_pc = 0;
	tracer->nbytes = 0;
	tracer->pc_nbytes = 0;
	tracer->mnemonic = "*";
	strcpy(tracer->operand_text, "*");

	tracer->ins_type = PAGE0;
	tracer->state_list = NULL;
	tracer->idx_mode = 0;
	tracer->idx_reg = "";
	tracer->idx_indirect = 0;
}

static void print_record(struct mc6809_trace *tracer) {
	struct MC6809 *cpu = tracer->cpu;

	if (tracer->nbytes == 0) {
		// Nothing to print.
		return;
	}

	// XXX currently no way to reset start_tick when turning trace mode
	// on/off, so first instruction after switching on will have crazy dt.

	int dt = event_tick_delta(event_current_tick, tracer->start_tick);
	tracer->start_tick = event_current_tick;

	char bytes_string[(MAX_NBYTES*2)+1];
	for (unsigned i = 0; i < tracer->nbytes; i++) {
		snprintf(bytes_string + i*2, 3, "%02x", tracer->bytes[i]);
	}

	if (tracer->ins_type == IRQVECTOR) {
		printf("%04x| %-12s%-24s", tracer->instr_pc, bytes_string, tracer->mnemonic);
	} else {
		printf("%04x| %-12s%-6s%-18s", tracer->instr_pc, bytes_string, tracer->mnemonic, tracer->operand_text);
		printf("  cc=%02x a=%02x b=%02x dp=%02x "
		       "x=%04x y=%04x u=%04x s=%04x",
		       cpu->reg_cc, MC6809_REG_A(cpu), MC6809_REG_B(cpu), cpu->reg_dp,
		       cpu->reg_x, cpu->reg_y, cpu->reg_u, cpu->reg_s);
	}

	if (logging.trace_cpu_timing) {
		printf("  dt=%d", dt);
	}

	printf("\n");
	fflush(stdout);
	reset_state(tracer);
}
