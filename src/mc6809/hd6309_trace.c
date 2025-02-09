/** \file
 *
 *  \brief Hitach HD6309 CPU tracing.
 *
 *  \copyright Copyright 2012-2025 Ciaran Anscomb
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
#include "hd6309.h"
#include "hd6309_trace.h"

// Instruction types

enum {
	ILLEGAL = 0, INHERENT, WORD_IMMEDIATE, IMMEDIATE, EXTENDED, DIRECT,
	INDEXED, RELATIVE, LONG_RELATIVE, STACKS, STACKU, REGISTER, IRQVECTOR,
	QUAD_IMMEDIATE, MEMBIT, INMEM_DIRECT, INMEM_INDEXED, INMEM_EXTENDED,
	TFMPP, TFMMM, TFMP0, TFM0P
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
		{ "OIM", INMEM_DIRECT },
		{ "AIM", INMEM_DIRECT },
		{ "COM", DIRECT },
		{ "LSR", DIRECT },
		{ "EIM", INMEM_DIRECT },
		{ "ROR", DIRECT },
		{ "ASR", DIRECT },
		{ "LSL", DIRECT },
		{ "ROL", DIRECT },
		{ "DEC", DIRECT },
		{ "TIM", INMEM_DIRECT },
		{ "INC", DIRECT },
		{ "TST", DIRECT },
		{ "JMP", DIRECT },
		{ "CLR", DIRECT },
		// 0x10 - 0x1F
		{ NULL, ILLEGAL },  // Page byte - handled explicitly
		{ NULL, ILLEGAL },  // Page byte - handled explicitly
		{ "NOP", INHERENT },
		{ "SYNC", INHERENT },
		{ "SEXW", INHERENT },
		{ NULL, ILLEGAL },
		{ "LBRA", LONG_RELATIVE },
		{ "LBSR", LONG_RELATIVE },
		{ NULL, ILLEGAL },
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
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "COMA", INHERENT },
		{ "LSRA", INHERENT },
		{ NULL, ILLEGAL },
		{ "RORA", INHERENT },
		{ "ASRA", INHERENT },
		{ "LSLA", INHERENT },
		{ "ROLA", INHERENT },
		{ "DECA", INHERENT },
		{ NULL, ILLEGAL },
		{ "INCA", INHERENT },
		{ "TSTA", INHERENT },
		{ NULL, ILLEGAL },
		{ "CLRA", INHERENT },
		// 0x50 - 0x5F
		{ "NEGB", INHERENT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "COMB", INHERENT },
		{ "LSRB", INHERENT },
		{ NULL, ILLEGAL },
		{ "RORB", INHERENT },
		{ "ASRB", INHERENT },
		{ "LSLB", INHERENT },
		{ "ROLB", INHERENT },
		{ "DECB", INHERENT },
		{ NULL, ILLEGAL },
		{ "INCB", INHERENT },
		{ "TSTB", INHERENT },
		{ NULL, ILLEGAL },
		{ "CLRB", INHERENT },
		// 0x60 - 0x6F
		{ "NEG", INDEXED },
		{ "OIM", INMEM_INDEXED },
		{ "AIM", INMEM_INDEXED },
		{ "COM", INDEXED },
		{ "LSR", INDEXED },
		{ "EIM", INMEM_INDEXED },
		{ "ROR", INDEXED },
		{ "ASR", INDEXED },
		{ "LSL", INDEXED },
		{ "ROL", INDEXED },
		{ "DEC", INDEXED },
		{ "TIM", INMEM_INDEXED },
		{ "INC", INDEXED },
		{ "TST", INDEXED },
		{ "JMP", INDEXED },
		{ "CLR", INDEXED },
		// 0x70 - 0x7F
		{ "NEG", EXTENDED },
		{ "OIM", INMEM_EXTENDED },
		{ "AIM", INMEM_EXTENDED },
		{ "COM", EXTENDED },
		{ "LSR", EXTENDED },
		{ "EIM", INMEM_EXTENDED },
		{ "ROR", EXTENDED },
		{ "ASR", EXTENDED },
		{ "LSL", EXTENDED },
		{ "ROL", EXTENDED },
		{ "DEC", EXTENDED },
		{ "TIM", INMEM_EXTENDED },
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
		{ NULL, ILLEGAL },
		{ "EORA", IMMEDIATE },
		{ "ADCA", IMMEDIATE },
		{ "ORA", IMMEDIATE },
		{ "ADDA", IMMEDIATE },
		{ "CMPX", WORD_IMMEDIATE },
		{ "BSR", RELATIVE },
		{ "LDX", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
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
		{ NULL, ILLEGAL },
		{ "EORB", IMMEDIATE },
		{ "ADCB", IMMEDIATE },
		{ "ORB", IMMEDIATE },
		{ "ADDB", IMMEDIATE },
		{ "LDD", WORD_IMMEDIATE },
		{ "LDQ", QUAD_IMMEDIATE },
		{ "LDU", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
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
		{ NULL, ILLEGAL },
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
		{ "ADDR", REGISTER },
		{ "ADCR", REGISTER },
		{ "SUBR", REGISTER },
		{ "SBCR", REGISTER },
		{ "ANDR", REGISTER },
		{ "ORR", REGISTER },
		{ "EORR", REGISTER },
		{ "CMPR", REGISTER },
		{ "PSHSW", INHERENT },
		{ "PULSW", INHERENT },
		{ "PSHUW", INHERENT },
		{ "PULUW", INHERENT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "SWI2", INHERENT },
		// 0x1040 - 0x104F
		{ "NEGD", INHERENT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "COMD", INHERENT },
		{ "LSRD", INHERENT },
		{ NULL, ILLEGAL },
		{ "RORD", INHERENT },
		{ "ASRD", INHERENT },
		{ "LSLD", INHERENT },
		{ "ROLD", INHERENT },
		{ "DECD", INHERENT },
		{ NULL, ILLEGAL },
		{ "INCD", INHERENT },
		{ "TSTD", INHERENT },
		{ NULL, ILLEGAL },
		{ "CLRD", INHERENT },
		// 0x1050 - 0x105F
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "COMW", ILLEGAL },
		{ "LSRW", ILLEGAL },
		{ NULL, ILLEGAL },
		{ "RORW", ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "ROLW", ILLEGAL },
		{ "DECW", ILLEGAL },
		{ NULL, ILLEGAL },
		{ "INCW", ILLEGAL },
		{ "TSTW", ILLEGAL },
		{ NULL, ILLEGAL },
		{ "CLRW", ILLEGAL },
		// 0x1060 - 0x106F
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
		// 0x1070 - 0x107F
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
		// 0x1080 - 0x108F
		{ "SUBW", WORD_IMMEDIATE },
		{ "CMPW", WORD_IMMEDIATE },
		{ "SBCD", WORD_IMMEDIATE },
		{ "CMPD", WORD_IMMEDIATE },
		{ "ANDD", WORD_IMMEDIATE },
		{ "BITD", WORD_IMMEDIATE },
		{ "LDW", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
		{ "EORD", WORD_IMMEDIATE },
		{ "ADCD", WORD_IMMEDIATE },
		{ "ORD", WORD_IMMEDIATE },
		{ "ADDW", WORD_IMMEDIATE },
		{ "CMPY", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
		{ "LDY", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
		// 0x1090 - 0x109F
		{ "SUBW", DIRECT },
		{ "CMPW", DIRECT },
		{ "SBCD", DIRECT },
		{ "CMPD", DIRECT },
		{ "ANDD", DIRECT },
		{ "BITD", DIRECT },
		{ "LDW", DIRECT },
		{ "STW", DIRECT },
		{ "EORD", DIRECT },
		{ "ADCD", DIRECT },
		{ "ORD", DIRECT },
		{ "ADDW", DIRECT },
		{ "CMPY", DIRECT },
		{ NULL, ILLEGAL },
		{ "LDY", DIRECT },
		{ "STY", DIRECT },
		// 0x10A0 - 0x10AF
		{ "SUBW", INDEXED },
		{ "CMPW", INDEXED },
		{ "SBCD", INDEXED },
		{ "CMPD", INDEXED },
		{ "ANDD", INDEXED },
		{ "BITD", INDEXED },
		{ "LDW", INDEXED },
		{ "STW", INDEXED },
		{ "EORD", INDEXED },
		{ "ADCD", INDEXED },
		{ "ORD", INDEXED },
		{ "ADDW", INDEXED },
		{ "CMPY", INDEXED },
		{ NULL, ILLEGAL },
		{ "LDY", INDEXED },
		{ "STY", INDEXED },
		// 0x10B0 - 0x10BF
		{ "SUBW", EXTENDED },
		{ "CMPW", EXTENDED },
		{ "SBCD", EXTENDED },
		{ "CMPD", EXTENDED },
		{ "ANDD", EXTENDED },
		{ "BITD", EXTENDED },
		{ "LDW", EXTENDED },
		{ "STW", EXTENDED },
		{ "EORD", EXTENDED },
		{ "ADCD", EXTENDED },
		{ "ORD", EXTENDED },
		{ "ADDW", EXTENDED },
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
		{ "LDQ", DIRECT },
		{ "STQ", DIRECT },
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
		{ "LDQ", INDEXED },
		{ "STQ", INDEXED },
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
		{ "LDQ", EXTENDED },
		{ "STQ", EXTENDED },
		{ "LDS", EXTENDED },
		{ "STS", EXTENDED }
	}, {

		// 0x1100 - 0x110F
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
		{ "BAND", MEMBIT },
		{ "BIAND", MEMBIT },
		{ "BOR", MEMBIT },
		{ "BIOR", MEMBIT },
		{ "BEOR", MEMBIT },
		{ "BIEOR", MEMBIT },
		{ "LDBT", MEMBIT },
		{ "STBT", MEMBIT },
		{ "TFM", TFMPP },
		{ "TFM", TFMMM },
		{ "TFM", TFMP0 },
		{ "TFM", TFM0P },
		{ "BITMD", IMMEDIATE },
		{ "LDMD", IMMEDIATE },
		{ NULL, ILLEGAL },
		{ "SWI3", INHERENT },
		// 0x1140 - 0x114F
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "COME", INHERENT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "DECE", INHERENT },
		{ NULL, ILLEGAL },
		{ "INCE", INHERENT },
		{ "TSTE", INHERENT },
		{ NULL, ILLEGAL },
		{ "CLRE", INHERENT },
		// 0x1150 - 0x115F
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "COMF", INHERENT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "DECF", INHERENT },
		{ NULL, ILLEGAL },
		{ "INCF", INHERENT },
		{ "TSTF", INHERENT },
		{ NULL, ILLEGAL },
		{ "CLRF", INHERENT },
		// 0x1160 - 0x116F
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
		// 0x1170 - 0x117F
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
		// 0x1180 - 0x118F
		{ "SUBE", IMMEDIATE },
		{ "CMPE", IMMEDIATE },
		{ NULL, ILLEGAL },
		{ "CMPU", WORD_IMMEDIATE },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDE", IMMEDIATE },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "ADDE", IMMEDIATE },
		{ "CMPS", WORD_IMMEDIATE },
		{ "DIVD", IMMEDIATE },
		{ "DIVQ", WORD_IMMEDIATE },
		{ "MULD", WORD_IMMEDIATE },
		// 0x1190 - 0x119F
		{ "SUBE", DIRECT },
		{ "CMPE", DIRECT },
		{ NULL, ILLEGAL },
		{ "CMPU", DIRECT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDE", DIRECT },
		{ "STE", DIRECT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "ADDE", DIRECT },
		{ "CMPS", DIRECT },
		{ "DIVD", DIRECT },
		{ "DIVQ", DIRECT },
		{ "MULD", DIRECT },
		// 0x11A0 - 0x11AF
		{ "SUBE", INDEXED },
		{ "CMPE", INDEXED },
		{ NULL, ILLEGAL },
		{ "CMPU", INDEXED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDE", INDEXED },
		{ "STE", INDEXED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "ADDE", INDEXED },
		{ "CMPS", INDEXED },
		{ "DIVD", INDEXED },
		{ "DIVQ", INDEXED },
		{ "MULD", INDEXED },
		// 0x11B0 - 0x11BF
		{ "SUBE", EXTENDED },
		{ "CMPE", EXTENDED },
		{ NULL, ILLEGAL },
		{ "CMPU", EXTENDED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDE", EXTENDED },
		{ "STE", EXTENDED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "ADDE", EXTENDED },
		{ "CMPS", EXTENDED },
		{ "DIVD", EXTENDED },
		{ "DIVQ", EXTENDED },
		{ "MULD", EXTENDED },
		// 0x11C0 - 0x11CF
		{ "SUBF", IMMEDIATE },
		{ "CMPF", IMMEDIATE },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDF", IMMEDIATE },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "ADDF", IMMEDIATE },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x11D0 - 0x11DF
		{ "SUBF", DIRECT },
		{ "CMPF", DIRECT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDF", DIRECT },
		{ "STF", DIRECT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "ADDF", DIRECT },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x11E0 - 0x11EF
		{ "SUBF", INDEXED },
		{ "CMPF", INDEXED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDF", INDEXED },
		{ "STF", INDEXED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "ADDF", INDEXED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		// 0x11F0 - 0x11FF
		{ "SUBF", EXTENDED },
		{ "CMPF", EXTENDED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "LDF", EXTENDED },
		{ "STF", EXTENDED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ "ADDF", EXTENDED },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
		{ NULL, ILLEGAL },
	}
};

// Indexed addressing modes

enum {
	IDX_PI1, IDX_PI2, IDX_PD1, IDX_PD2,
	IDX_OFF0, IDX_OFFB, IDX_OFFA, IDX_OFFE,
	IDX_OFF8, IDX_OFF16, IDX_OFFF, IDX_OFFD,
	IDX_PCR8, IDX_PCR16, IDX_OFFW, IDX_EXT16
};

// Indexed mode format strings (excluding 5-bit offset).  The leading and
// trailing %s account for the optional brackets in indirect modes.  8-bit
// offsets include an extra %s to indicate sign.

static char const * const idx_fmts[17] = {
	"%s,%s+%s",
	"%s,%s++%s",
	"%s,-%s%s",
	"%s,--%s%s",
	"%s,%s%s",
	"%sB,%s%s",
	"%sA,%s%s",
	"%sE,%s%s",
	"%s%s$%02x,%s%s",
	"%s$%04x,%s%s",
	"%sF,%s%s",
	"%sD,%s%s",
	"%s<$%04x,PCR%s",
	"%s>$%04x,PCR%s",
	"%sW,%s%s",
	"%s$%04x%s"
};

// TFM instruction format strings

static char const * const tfm_fmts[4] = {
	"%s+,%s+",
	"%s-,%s-",
	"%s+,%s",
	"%s,%s+"
};

// Inter-register operation postbyte
static char const * const tfr_regs[16] = {
	"D", "X", "Y", "U", "S", "PC", "W", "V",
	"A", "B", "CC", "DP", "0", "0", "E", "F"
};

// Indexed addressing postbyte registers
static char const * const idx_regs[4] = { "X", "Y", "U", "S" };

// Memory with bit postbyte
static char const * const membit_regs[4] = { "CC", "A", "B", "*" };

// Interrupt vector names
static char const * const irq_names[8] = {
	"[ILLEGAL]", "[SWI3]", "[SWI2]", "[FIRQ]",
	"[IRQ]", "[SWI]", "[NMI]", "[RESET]"
};

// Current state

struct hd6309_trace {
	struct HD6309 *hcpu;
	event_ticks start_tick;
};

// Iterate over supplied data

struct byte_iter {
	unsigned nbytes;
	unsigned index;
	uint8_t *bytes;
};

// Helper functions

static unsigned next_byte(struct byte_iter *iter);
static unsigned next_word(struct byte_iter *iter);

static char *stack_operand(char *dst, char *dend, unsigned *postbyte, const char *r);

#define sex5(v) ((int)((v) & 0x0f) - (int)((v) & 0x10))
#define sex8(v) ((int8_t)(v))

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct hd6309_trace *hd6309_trace_new(struct HD6309 *hcpu) {
	struct hd6309_trace *tracer = xmalloc(sizeof(*tracer));
	*tracer = (struct hd6309_trace){0};
	tracer->hcpu = hcpu;
	tracer->start_tick = event_current_tick;
	return tracer;
}

void hd6309_trace_free(struct hd6309_trace *tracer) {
	free(tracer);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Called at each timing checkpoint

static void print_line_end(struct hd6309_trace *tracer);

void hd6309_trace_vector(struct hd6309_trace *tracer, uint16_t vec,
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

void hd6309_trace_instruction(struct hd6309_trace *tracer, uint16_t pc,
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

	char operand_text[24];
	operand_text[0] = '\0';

	unsigned im_value = 0;
	if (ins_type == INMEM_DIRECT ||
	    ins_type == INMEM_INDEXED ||
	    ins_type == INMEM_EXTENDED) {
		im_value = next_byte(&iter);
	}

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
			 tfr_regs[(postbyte>>4)&15],
			 tfr_regs[postbyte&15]);
	} break;

	case INDEXED:
	case INMEM_INDEXED: {
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

			// Overrides for bolted-on 6309 indexed modes:
			if (postbyte == 0x8f || postbyte == 0x90) {
				idx_reg = "W";
				idx_mode = IDX_OFF0;
			} else if (postbyte == 0xaf || postbyte == 0xb0) {
				idx_reg = "W";
				idx_mode = IDX_OFF16;
			} else if (postbyte == 0xcf || postbyte == 0xd0) {
				idx_reg = "W";
				idx_mode = IDX_PI2;
			} else if (postbyte == 0xef || postbyte == 0xf0) {
				idx_reg = "W";
				idx_mode = IDX_PD2;
			}

			const char *fmt = idx_fmts[idx_mode];
			char tmp_text[16];
			tmp_text[0] = '\0';

			switch (idx_mode) {
			default:
			case IDX_PI1: case IDX_PI2: case IDX_PD1: case IDX_PD2:
			case IDX_OFF0: case IDX_OFFB: case IDX_OFFA: case IDX_OFFE:
			case IDX_OFFF: case IDX_OFFD: case IDX_OFFW:
				snprintf(tmp_text, sizeof(tmp_text), fmt, pre, idx_reg, post);
				break;
			case IDX_OFF8: {
				unsigned uv = next_byte(&iter);
				int v = sex8(uv);
				snprintf(tmp_text, sizeof(tmp_text), fmt, pre, (v<0)?"-":"", (v<0)?-v:v, idx_reg, post);
			} break;
			case IDX_OFF16: {
				unsigned uv = next_word(&iter);
				snprintf(tmp_text, sizeof(tmp_text), fmt, pre, uv, idx_reg, post);
			} break;
			case IDX_PCR8: {
				unsigned uv = next_byte(&iter);
				int v = sex8(uv);
				snprintf(tmp_text, sizeof(tmp_text), fmt, pre, (uint16_t)(pc + iter.index + v), post);
			} break;
			case IDX_PCR16: {
				unsigned uv = next_word(&iter);
				snprintf(tmp_text, sizeof(tmp_text), fmt, pre, (uint16_t)(pc + iter.index + uv), post);
			} break;
			case IDX_EXT16: {
				unsigned uv = next_word(&iter);
				snprintf(tmp_text, sizeof(tmp_text), fmt, pre, uv, post);
			} break;
			}

			if (ins_type == INDEXED) {
				snprintf(operand_text, sizeof(operand_text), "%s", tmp_text);
			} else {
				snprintf(operand_text, sizeof(operand_text), "#$%02x,%s", im_value, tmp_text);
			}
		}
	} break;

	case RELATIVE: {
		unsigned uv = next_byte(&iter);
		uv = (pc + iter.index + sex8(uv)) & 0xffff;
		snprintf(operand_text, sizeof(operand_text), "$%04x", uv);
	} break;

	case LONG_RELATIVE: {
		unsigned uv = next_word(&iter);
		uv = (pc + iter.index + uv) & 0xffff;
		snprintf(operand_text, sizeof(operand_text), "$%04x", uv);
	} break;

	case QUAD_IMMEDIATE: {
		unsigned uv0 = next_word(&iter);
		unsigned uv1 = next_word(&iter);
		snprintf(operand_text, sizeof(operand_text), "#$%04x%04x", uv0, uv1);
	} break;

	case INMEM_DIRECT: {
		unsigned uv = next_byte(&iter);
		snprintf(operand_text, sizeof(operand_text), "#$%02x,<$%02x", im_value, uv);
	} break;

	case INMEM_EXTENDED: {
		unsigned uv = next_word(&iter);
		snprintf(operand_text, sizeof(operand_text), "#$%02x,$%04x", im_value, uv);
	} break;

	case TFMPP: case TFMMM: case TFMP0: case TFM0P: {
		const char *tfm_fmt = tfm_fmts[ins & 3];
		unsigned postbyte = next_byte(&iter);
		if ((postbyte >> 4) > 4 || (postbyte & 15) > 4)
			mnemonic = "TFM*";
		snprintf(operand_text, sizeof(operand_text), tfm_fmt,
			 tfr_regs[(postbyte>>4)&15],
			 tfr_regs[postbyte&15]);
	} break;

	case MEMBIT: {
		unsigned postbyte = next_byte(&iter);
		unsigned uv = next_byte(&iter);
		const char *membit_reg = membit_regs[(postbyte>>6) & 3];
		unsigned membit_sbit = (postbyte>>3) & 7;
		unsigned membit_dbit = postbyte & 7;
		snprintf(operand_text, sizeof(operand_text), "%s,%u,%u,<$%02x",
			 membit_reg, membit_sbit, membit_dbit, uv);
	} break;

	}

	char bytes_string[(MC6809_MAX_TRACE_BYTES*2)+1];
	for (unsigned i = 0; i < iter.index; i++) {
		snprintf(bytes_string + i*2, 3, "%02x", bytes[i]);
	}

	struct HD6309 *hcpu = tracer->hcpu;
	struct MC6809 *cpu = &hcpu->mc6809;
	printf("%04x| %-12s%-6s%-18s", pc, bytes_string, mnemonic, operand_text);
	printf("  cc=%02x a=%02x b=%02x e=%02x "
	       "f=%02x dp=%02x x=%04x y=%04x "
	       "u=%04x s=%04x v=%04x",
	       cpu->reg_cc, MC6809_REG_A(cpu), MC6809_REG_B(cpu), HD6309_REG_E(hcpu),
	       HD6309_REG_F(hcpu), cpu->reg_dp, cpu->reg_x, cpu->reg_y,
	       cpu->reg_u, cpu->reg_s, hcpu->reg_v);
	print_line_end(tracer);
}

static void print_line_end(struct hd6309_trace *tracer) {
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

static unsigned next_byte(struct byte_iter *iter) {
	assert(iter->index < iter->nbytes);
	return iter->bytes[iter->index++];
}

static unsigned next_word(struct byte_iter *iter) {
	unsigned v = next_byte(iter) << 8;
	v |= next_byte(iter);
	return v;
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
