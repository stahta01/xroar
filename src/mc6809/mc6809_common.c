/** \file
 *
 *  \brief Motorola MC6809-compatible common functions.
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
 *  functions common across 6809 ISA CPUs.
 */

/* Memory interface */

static uint8_t fetch_byte_notrace(struct MC6809 *cpu, uint16_t a) {
	cpu->nmi_latch |= (cpu->nmi_armed && cpu->nmi);
	cpu->firq_latch = cpu->firq;
	cpu->irq_latch = cpu->irq;
	DELEGATE_CALL(cpu->mem_cycle, 1, a);
	return cpu->D;
}

static uint16_t fetch_word_notrace(struct MC6809 *cpu, uint16_t a) {
	unsigned v = fetch_byte_notrace(cpu, a) << 8;
	return v | fetch_byte_notrace(cpu, a+1);
}

static void store_byte(struct MC6809 *cpu, uint16_t a, uint8_t d) {
	cpu->nmi_latch |= (cpu->nmi_armed && cpu->nmi);
	cpu->firq_latch = cpu->firq;
	cpu->irq_latch = cpu->irq;
	cpu->D = d;
	DELEGATE_CALL(cpu->mem_cycle, 0, a);
}

#define peek_byte(c,a) ((void)fetch_byte_notrace(c,a))
#define NVMA_CYCLE (peek_byte(cpu, 0xffff))

/* Stack operations */

static void push_s_byte(struct MC6809 *cpu, uint8_t v) {
	store_byte(cpu, --REG_S, v);
}

static void push_s_word(struct MC6809 *cpu, uint16_t v) {
	store_byte(cpu, --REG_S, v);
	store_byte(cpu, --REG_S, v >> 8);
}

static uint8_t pull_s_byte(struct MC6809 *cpu) {
	return fetch_byte(cpu, REG_S++);
}

static uint16_t pull_s_word(struct MC6809 *cpu) {
	unsigned val = fetch_byte(cpu, REG_S++);
	return (val << 8) | fetch_byte(cpu, REG_S++);
}

static void push_u_byte(struct MC6809 *cpu, uint8_t v) {
	store_byte(cpu, --REG_U, v);
}

static void push_u_word(struct MC6809 *cpu, uint16_t v) {
	store_byte(cpu, --REG_U, v);
	store_byte(cpu, --REG_U, v >> 8);
}

static uint8_t pull_u_byte(struct MC6809 *cpu) {
	return fetch_byte(cpu, REG_U++);
}

static uint16_t pull_u_word(struct MC6809 *cpu) {
	unsigned val = fetch_byte(cpu, REG_U++);
	return (val << 8) | fetch_byte(cpu, REG_U++);
}
