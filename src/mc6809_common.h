/*

Motorola MC6809-compatible common functions

Copyright 2003-2017 Ciaran Anscomb

This file is part of XRoar.

XRoar is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

See COPYING.GPL for redistribution conditions.

*/

/* Memory interface */

static uint8_t fetch_byte_notrace(struct MC6809 *cpu, uint16_t a);
static uint16_t fetch_word_notrace(struct MC6809 *cpu, uint16_t a);
static void store_byte(struct MC6809 *cpu, uint16_t a, uint8_t d);
#define peek_byte(c,a) ((void)fetch_byte_notrace(c,a))
#define NVMA_CYCLE (peek_byte(cpu, 0xffff))

/* Read & write various addressing modes */

static uint8_t byte_immediate(struct MC6809 *cpu);
static uint8_t byte_direct(struct MC6809 *cpu);
static uint8_t byte_extended(struct MC6809 *cpu);
static uint8_t byte_indexed(struct MC6809 *cpu);
static uint16_t word_immediate(struct MC6809 *cpu);
static uint16_t word_direct(struct MC6809 *cpu);
static uint16_t word_extended(struct MC6809 *cpu);
static uint16_t word_indexed(struct MC6809 *cpu);

static uint16_t short_relative(struct MC6809 *cpu);
#define long_relative word_immediate

/* Stack operations */

static void push_s_byte(struct MC6809 *cpu, uint8_t v);
static void push_s_word(struct MC6809 *cpu, uint16_t v);
static uint8_t pull_s_byte(struct MC6809 *cpu);
static uint16_t pull_s_word(struct MC6809 *cpu);

static void push_u_byte(struct MC6809 *cpu, uint8_t v);
static void push_u_word(struct MC6809 *cpu, uint16_t v);
static uint8_t pull_u_byte(struct MC6809 *cpu);
static uint16_t pull_u_word(struct MC6809 *cpu);

/* 8-bit inherent operations */

static uint8_t op_neg(struct MC6809 *cpu, uint8_t in);
static uint8_t op_com(struct MC6809 *cpu, uint8_t in);
static uint8_t op_lsr(struct MC6809 *cpu, uint8_t in);
static uint8_t op_ror(struct MC6809 *cpu, uint8_t in);
static uint8_t op_asr(struct MC6809 *cpu, uint8_t in);
static uint8_t op_asl(struct MC6809 *cpu, uint8_t in);
static uint8_t op_rol(struct MC6809 *cpu, uint8_t in);
static uint8_t op_dec(struct MC6809 *cpu, uint8_t in);
static uint8_t op_inc(struct MC6809 *cpu, uint8_t in);
static uint8_t op_tst(struct MC6809 *cpu, uint8_t in);
static uint8_t op_clr(struct MC6809 *cpu, uint8_t in);
static uint8_t op_daa(struct MC6809 *cpu, uint8_t in);

/* 8-bit arithmetic operations */

static uint8_t op_sub(struct MC6809 *cpu, uint8_t a, uint8_t b);
static uint8_t op_sbc(struct MC6809 *cpu, uint8_t a, uint8_t b);
static uint8_t op_and(struct MC6809 *cpu, uint8_t a, uint8_t b);
static uint8_t op_ld(struct MC6809 *cpu, uint8_t a, uint8_t b);
static uint8_t op_eor(struct MC6809 *cpu, uint8_t a, uint8_t b);
static uint8_t op_adc(struct MC6809 *cpu, uint8_t a, uint8_t b);
static uint8_t op_or(struct MC6809 *cpu, uint8_t a, uint8_t b);
static uint8_t op_add(struct MC6809 *cpu, uint8_t a, uint8_t b);

/* 16-bit arithmetic operations */

static uint16_t op_sub16(struct MC6809 *cpu, uint16_t a, uint16_t b);
static uint16_t op_ld16(struct MC6809 *cpu, uint16_t a, uint16_t b);
static uint16_t op_add16(struct MC6809 *cpu, uint16_t a, uint16_t b);

/* Various utility functions */

static uint16_t sex5(unsigned v);
static uint16_t sex8(unsigned v);
static _Bool branch_condition(struct MC6809 const *cpu, unsigned op);

