/** \file
 *
 *  \brief Motorola MC680x-compatible operations.
 *
 *  \copyright Copyright 2003-2021 Ciaran Anscomb
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
 *  This file is included directly into other source files.  It provides
 *  common functions across 680x ISA CPUs.
 *
 *  STRUCT_CPU should be a macro defining the type of the CPU struct.
 *
 *  REG_CC should be a macro aliasing the condition code register.
 *
 *  CC_H, CC_N, etc. should be macros defined as appropriate bit within REG_CC.
 */

// Not all ops will be used by all cores, so ignore unused-function warnings
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

// Condition code operations

#define CLR_HNZVC ( REG_CC &= ~(CC_H|CC_N|CC_Z|CC_V|CC_C) )
#define CLR_NZ    ( REG_CC &= ~(CC_N|CC_Z) )
#define CLR_NZV   ( REG_CC &= ~(CC_N|CC_Z|CC_V) )
#define CLR_NZVC  ( REG_CC &= ~(CC_N|CC_Z|CC_V|CC_C) )
#define CLR_Z     ( REG_CC &= ~(CC_Z) )
#define CLR_NZC   ( REG_CC &= ~(CC_N|CC_Z|CC_C) )
#define CLR_NVC   ( REG_CC &= ~(CC_N|CC_V|CC_C) )
#define CLR_ZC    ( REG_CC &= ~(CC_Z|CC_C) )

#define SET_Z8(r)         ( (!((r)&0xff)) ? (REG_CC |= CC_Z) : 0 )
#define SET_Z16(r)        ( (!((r)&0xffff)) ? (REG_CC |= CC_Z) : 0 )
#define SET_N8(r)         ( REG_CC |= (((r) >> 4) & CC_N) )
#define SET_N16(r)        ( REG_CC |= (((r) >> 12) & CC_N) )
#define SET_H(a,b,r)      ( REG_CC |= ((((a)^(b)^(r))<<1) & CC_H) )
#define SET_C8(r)         ( REG_CC |= (((r)>>8) & CC_C) )
#define SET_C16(r)        ( REG_CC |= (((r)>>16) & CC_C) )
#define SET_V8(a,b,r)     ( REG_CC |= ((((a)^(b)^(r)^((r)>>1))>>6) & CC_V) )
#define SET_V16(a,b,r)    ( REG_CC |= ((((a)^(b)^(r)^((r)>>1))>>14) & CC_V) )
#define SET_NZ8(r)        ( SET_N8(r), SET_Z8((r)&0xff) )
#define SET_NZ16(r)       ( SET_N16(r), SET_Z16((r)&0xffff) )
#define SET_NZC8(r)       ( SET_N8(r), SET_Z8((r)&0xff), SET_C8(r) )
#define SET_NZC16(r)      ( SET_N16(r), SET_Z16((r)&0xffff), SET_C16(r) )
#define SET_NZV8(a,b,r)   ( SET_N8(r), SET_Z8((r)&0xff), SET_V8(a,b,r) )
#define SET_NZVC8(a,b,r)  ( SET_N8(r), SET_Z8((r)&0xff), SET_V8(a,b,r), SET_C8(r) )
#define SET_NZVC16(a,b,r) ( SET_N16(r), SET_Z16((r)&0xffff), SET_V16(a,b,r), SET_C16(r) )

// Various utility functions

// Sign extend 5 bits into 16 bits

static uint16_t sex5(unsigned v) {
	return (v & 0x0f) - (v & 0x10);
}

// Sign extend 8 bits into 16 bits

static uint16_t sex8(uint8_t v) {
	return (int16_t)(*((int8_t *)&v));
}

// Read & write various addressing modes.  Note: ea_*() functions should be
// defined by the core (as they vary wildly).

static uint8_t byte_immediate(STRUCT_CPU *cpu) {
	return fetch_byte(cpu, REG_PC++);
}

static uint8_t byte_direct(STRUCT_CPU *cpu) {
	unsigned ea = ea_direct(cpu);
	return fetch_byte(cpu, ea);
}

static uint8_t byte_extended(STRUCT_CPU *cpu) {
	unsigned ea = ea_extended(cpu);
	return fetch_byte(cpu, ea);
}

static uint8_t byte_indexed(STRUCT_CPU *cpu) {
	unsigned ea = ea_indexed(cpu);
	return fetch_byte(cpu, ea);
}

static uint16_t word_immediate(STRUCT_CPU *cpu) {
	unsigned v = fetch_byte(cpu, REG_PC++) << 8;
	return v | fetch_byte(cpu, REG_PC++);
}

#define long_relative word_immediate

static uint16_t word_direct(STRUCT_CPU *cpu) {
	unsigned ea = ea_direct(cpu);
	return fetch_word(cpu, ea);
}

static uint16_t word_extended(STRUCT_CPU *cpu) {
	unsigned ea = ea_extended(cpu);
	return fetch_word(cpu, ea);
}

static uint16_t word_indexed(STRUCT_CPU *cpu) {
	unsigned ea = ea_indexed(cpu);
	return fetch_word(cpu, ea);
}

static uint16_t short_relative(STRUCT_CPU *cpu) {
	return sex8(byte_immediate(cpu));
}

// 8-bit inherent operations

static uint8_t op_neg(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = ~in + 1;
	CLR_NZVC;
	SET_NZVC8(0, in, out);
	return out;
}

// Illegal op in 6801/6803.  Tests like NEG, but doesn't store result.
static uint8_t op_ngt(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = ~in + 1;
	CLR_NZVC;
	SET_NZVC8(0, in, out);
	return in;
}

// Illegal op.  Invert and add !C, i.e. NEG if carry clear, else COM.
static uint8_t op_ngc(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = ~in + (~REG_CC & 1);
	CLR_NZVC;
	SET_NZVC8(0, in, out);
	return out;
}

static uint8_t op_com(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = ~in;
	CLR_NZV;
	SET_NZ8(out);
	REG_CC |= CC_C;
	return out;
}

// This vesion of LSR used in 6809
static uint8_t op_lsr(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = (in >> 1) | ((in & 1) << 8);
	CLR_NZC;
	SET_NZC8(out);
	return out;
}

// This vesion of LSR used in 6801/6803
static uint8_t op_lsr_v(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = (in >> 1) | ((in & 1) << 8);
	CLR_NZVC;
	SET_NZVC8(in, in, out);
	return out;
}

// This vesion of ROR used in 6809
static uint8_t op_ror(STRUCT_CPU *cpu, uint8_t in) {
	unsigned inx = in | ((REG_CC & 1) << 8);
	unsigned out = (inx >> 1) | ((inx & 1) << 8);
	CLR_NZC;
	SET_NZC8(out);
	return out;
}

// This vesion of ROR used in 6801/6803
static uint8_t op_ror_v(STRUCT_CPU *cpu, uint8_t in) {
	unsigned inx = in | ((REG_CC & 1) << 8);
	unsigned out = (inx >> 1) | ((inx & 1) << 8);
	CLR_NZVC;
	SET_NZVC8(inx, inx, out);
	return out;
}

// This vesion of ASR used in 6809
static uint8_t op_asr(STRUCT_CPU *cpu, uint8_t in) {
	unsigned inx = in | ((in & 0x80) << 1);
	unsigned out = (inx >> 1) | ((inx & 1) << 8);
	CLR_NZC;
	SET_NZC8(out);
	return out;
}

// This vesion of ASR used in 6801/6803
static uint8_t op_asr_v(STRUCT_CPU *cpu, uint8_t in) {
	unsigned inx = in | ((in & 0x80) << 1);
	unsigned out = (inx >> 1) | ((inx & 1) << 8);
	CLR_NZVC;
	SET_NZVC8(inx, inx, out);
	return out;
}

static uint8_t op_asl(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = in << 1;
	CLR_NZVC;
	SET_NZVC8(in, in, out);
	return out;
}

static uint8_t op_rol(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = (in << 1) | (REG_CC & 1);
	CLR_NZVC;
	SET_NZVC8(in, in, out);
	return out;
}

static uint8_t op_dec(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = in - 1;
	CLR_NZV;
	SET_NZ8(out);
	if (out == 0x7f) REG_CC |= CC_V;
	return out;
}

static uint8_t op_inc(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = in + 1;
	CLR_NZV;
	SET_NZ8(out);
	if (out == 0x80) REG_CC |= CC_V;
	return out;
}

// This version of TST used in 6809
static uint8_t op_tst(STRUCT_CPU *cpu, uint8_t in) {
	CLR_NZV;
	SET_NZ8(in);
	return in;
}

// This version of TST used in 6801/6803
static uint8_t op_tst_c(STRUCT_CPU *cpu, uint8_t in) {
	CLR_NZVC;
	SET_NZ8(in);
	return in;
}

static uint8_t op_clr(STRUCT_CPU *cpu, uint8_t in) {
	(void)in;
	CLR_NVC;
	REG_CC |= CC_Z;
	return 0;
}

// This version of DAA used in 6809
static uint8_t op_daa(STRUCT_CPU *cpu, uint8_t in) {
	unsigned out = 0;
	if ((in & 0x0f) >= 0x0a || REG_CC & CC_H) out |= 0x06;
	if (in >= 0x90 && (in & 0x0f) >= 0x0a) out |= 0x60;
	if (in >= 0xa0 || REG_CC & CC_C) out |= 0x60;
	out += in;
	// CC.C NOT cleared, only set if appropriate
	CLR_NZV;
	SET_NZC8(out);
	return out;
}

// This version of DAA used in 6801/6803
static uint8_t op_daa_v(STRUCT_CPU *cpu, uint8_t in) {
	unsigned add = 0;
	if ((in & 0x0f) >= 0x0a || REG_CC & CC_H) add |= 0x06;
	if (in >= 0x90 && (in & 0x0f) >= 0x0a) add |= 0x60;
	if (in >= 0xa0 || REG_CC & CC_C) add |= 0x60;
	unsigned out = in + add;
	// CC.C NOT cleared, only set if appropriate
	CLR_NZV;
	SET_NZVC8(in, add, out);
	return out;
}

// 8-bit arithmetic operations

static uint8_t op_sub(STRUCT_CPU *cpu, uint8_t a, uint8_t b) {
	unsigned out = a - b;
	CLR_NZVC;
	SET_NZVC8(a, b, out);
	return out;
}

static uint8_t op_sbc(STRUCT_CPU *cpu, uint8_t a, uint8_t b) {
	unsigned out = a - b - (REG_CC & CC_C);
	CLR_NZVC;
	SET_NZVC8(a, b, out);
	return out;
}

static uint8_t op_and(STRUCT_CPU *cpu, uint8_t a, uint8_t b) {
	unsigned out = a & b;
	CLR_NZV;
	SET_NZ8(out);
	return out;
}

static uint8_t op_ld(STRUCT_CPU *cpu, uint8_t a, uint8_t b) {
	(void)a;
	CLR_NZV;
	SET_NZ8(b);
	return b;
}

static uint8_t op_eor(STRUCT_CPU *cpu, uint8_t a, uint8_t b) {
	unsigned out = a ^ b;
	CLR_NZV;
	SET_NZ8(out);
	return out;
}

static uint8_t op_adc(STRUCT_CPU *cpu, uint8_t a, uint8_t b) {
	unsigned out = a + b + (REG_CC & CC_C);
	CLR_HNZVC;
	SET_NZVC8(a, b, out);
	SET_H(a, b, out);
	return out;
}

static uint8_t op_or(STRUCT_CPU *cpu, uint8_t a, uint8_t b) {
	unsigned out = a | b;
	CLR_NZV;
	SET_NZ8(out);
	return out;
}

static uint8_t op_add(STRUCT_CPU *cpu, uint8_t a, uint8_t b) {
	unsigned out = a + b;
	CLR_HNZVC;
	SET_NZVC8(a, b, out);
	SET_H(a, b, out);
	return out;
}

// Illegal op in 6801/6803.  Same as op_add(), but don't affect H or C.
static uint8_t op_add_nzv(STRUCT_CPU *cpu, uint8_t a, uint8_t b) {
	unsigned out = a + b;
	CLR_NZV;
	SET_NZV8(a, b, out);
	return out;
}

// 16-bit inherent operations

static uint16_t op_neg16(STRUCT_CPU *cpu, uint16_t in) {
	unsigned out = ~in + 1;
	CLR_NZVC;
	SET_NZVC16(0, in, out);
	return out;
}

static uint16_t op_com16(STRUCT_CPU *cpu, uint16_t in) {
	unsigned out = ~in;
	CLR_NZV;
	SET_NZ16(out);
	REG_CC |= CC_C;
	return out;
}

// This vesion of LSR16 used in 6309
static uint16_t op_lsr16(STRUCT_CPU *cpu, uint16_t in) {
	unsigned out = (in >> 1) | ((in & 1) << 16);
	CLR_NZC;
	SET_NZC16(out);
	return out;
}

// This vesion of LSR16 used in 6801/6803
static uint16_t op_lsr16_v(STRUCT_CPU *cpu, uint16_t in) {
	unsigned out = (in >> 1) | ((in & 1) << 16);
	CLR_NZVC;
	SET_NZVC16(in, in, out);
	return out;
}

static uint16_t op_ror16(STRUCT_CPU *cpu, uint16_t in) {
	unsigned inx = in | ((REG_CC & 1) << 16);
	unsigned out = (inx >> 1) | ((inx & 1) << 16);
	CLR_NZC;
	SET_NZC16(out);
	return out;
}

static uint16_t op_asr16(STRUCT_CPU *cpu, uint16_t in) {
	unsigned inx = in | ((in & 0x8000) << 1);
	unsigned out = (inx >> 1) | ((inx & 1) << 16);
	CLR_NZC;
	SET_NZC16(out);
	return out;
}

static uint16_t op_asl16(STRUCT_CPU *cpu, uint16_t in) {
	unsigned out = in << 1;
	CLR_NZVC;
	SET_NZVC16(in, in, out);
	return out;
}

static uint16_t op_rol16(STRUCT_CPU *cpu, uint16_t in) {
	unsigned out = (in << 1) | (REG_CC & 1);
	CLR_NZVC;
	SET_NZVC16(in, in, out);
	return out;
}

static uint16_t op_dec16(STRUCT_CPU *cpu, uint16_t in) {
	unsigned out = in - 1;
	CLR_NZV;
	SET_NZ16(out);
	if (out == 0x7fff) REG_CC |= CC_V;
	return out;
}

static uint16_t op_inc16(STRUCT_CPU *cpu, uint16_t in) {
	unsigned out = in + 1;
	CLR_NZV;
	SET_NZ16(out);
	if (out == 0x8000) REG_CC |= CC_V;
	return out;
}

static uint16_t op_tst16(STRUCT_CPU *cpu, uint16_t in) {
	CLR_NZV;
	SET_NZ16(in);
	return in;
}

static uint16_t op_clr16(STRUCT_CPU *cpu, uint16_t in) {
	(void)in;
	CLR_NVC;
	REG_CC |= CC_Z;
	return 0;
}

// 16-bit arithmetic operations

static uint16_t op_sub16(STRUCT_CPU *cpu, uint16_t a, uint16_t b) {
	unsigned out = a - b;
	CLR_NZVC;
	SET_NZVC16(a, b, out);
	return out;
}

static uint16_t op_sbc16(STRUCT_CPU *cpu, uint16_t a, uint16_t b) {
	unsigned out = a - b - (REG_CC & CC_C);
	CLR_NZVC;
	SET_NZVC16(a, b, out);
	return out;
}

static uint16_t op_and16(STRUCT_CPU *cpu, uint16_t a, uint16_t b) {
	unsigned out = a & b;
	CLR_NZV;
	SET_NZ16(out);
	return out;
}

static uint16_t op_ld16(STRUCT_CPU *cpu, uint16_t a, uint16_t b) {
	(void)a;
	CLR_NZV;
	SET_NZ16(b);
	return b;
}

static uint16_t op_eor16(STRUCT_CPU *cpu, uint16_t a, uint16_t b) {
	unsigned out = a ^ b;
	CLR_NZV;
	SET_NZ16(out);
	return out;
}

static uint16_t op_adc16(STRUCT_CPU *cpu, uint16_t a, uint16_t b) {
	unsigned out = a + b + (REG_CC & CC_C);
	CLR_NZVC;
	SET_NZVC16(a, b, out);
	return out;
}

static uint16_t op_or16(STRUCT_CPU *cpu, uint16_t a, uint16_t b) {
	unsigned out = a | b;
	CLR_NZV;
	SET_NZ16(out);
	return out;
}

static uint16_t op_add16(STRUCT_CPU *cpu, uint16_t a, uint16_t b) {
	unsigned out = a + b;
	CLR_NZVC;
	SET_NZVC16(a, b, out);
	return out;
}

// Determine branch condition from op-code

static _Bool branch_condition(STRUCT_CPU const *cpu, unsigned op) {
	_Bool cond;
	_Bool invert = op & 1;
	switch ((op >> 1) & 7) {
	default:
	case 0x0: cond = 1; break; // BRA, !BRN
	case 0x1: cond = !(REG_CC & (CC_Z|CC_C)); break; // BHI, !BLS
	case 0x2: cond = !(REG_CC & CC_C); break; // BCC, BHS, !BCS, !BLO
	case 0x3: cond = !(REG_CC & CC_Z); break; // BNE, !BEQ
	case 0x4: cond = !(REG_CC & CC_V); break; // BVC, !BVS
	case 0x5: cond = !(REG_CC & CC_N); break; // BPL, !BMI
	case 0x6: cond = !((REG_CC ^ (REG_CC << 2)) & CC_N); break; // BGE, !BLT
	case 0x7: cond = !(((REG_CC&(CC_N|CC_Z)) ^ ((REG_CC&CC_V) << 2))); break; // BGT, !BLE
	}
	return cond != invert;
}

#pragma GCC diagnostic pop
