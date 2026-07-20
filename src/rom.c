/** \file
 *
 *  \brief ROM metadata.
 *
 *  \copyright Copyright 2026 Ciaran Anscomb
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

#include <stdlib.h>
#include <stdint.h>

#include "array.h"
#include "slist.h"
#include "xalloc.h"

#include "rom.h"

static struct slist *rom_meta_list = NULL;

struct rom_meta_internal {
	uint32_t crc32;
	uint32_t size;
	const char *description;
	const char *symtab;   // symbol table name
	const char *part;     // part this ROM goes in
	const char *machine;  // machine required (where != part)
	const char *cart;     // cart required (where != part)
	uint8_t bank, slot;
	bool no_autorun;
};

// Shortcuts

// Every entry needs a description
#define DESC(c,s,d) .crc32 = (c), .size = (s), .description = (d)

// Some associate with symbol tables
#define S_D64_1 .symtab = "d64_1"
#define S_D32 .symtab = "d32"
#define S_BAS10 .symtab = "bas10"
#define S_BAS11 .symtab = "bas11"
#define S_BAS12 .symtab = "bas12"
#define S_BAS13 .symtab = "bas13"
#define S_COCO3 .symtab = "coco3"
#define S_MC10 .symtab = "mc10"

// Type of part they're associated with
#define P_DRAGON64 .part = "dragon64"
#define P_DRAGON32 .part = "dragon32"
#define P_DRAGONPRO .part = "dragonpro"
#define P_COCO .part = "coco"
#define P_COCO3 .part = "coco3"
#define P_DELUXECOCO .part = "deluxecoco"
#define P_MC10 .part = "mc10"
#define P_DRAGONDOS .part = "dragondos", NO_AUTORUN
#define P_DELTA .part = "delta", NO_AUTORUN
#define P_RSDOS .part = "rsdos", NO_AUTORUN
#define P_IDE .part = "ide", NO_AUTORUN
#define P_ROM .part = "rom"
#define P_GMC .part = "gmc"
#define P_ORCH90 .part = "orch90"
#define NO_AUTORUN .no_autorun = 1

// Specific machine arch needed, if not already implied by part
#define M_DRAGON64 .machine = "dragon64"
#define M_DRAGON32 .machine = "dragon32"
#define M_DRAGON .machine = "dragon32,dragon64"
#define M_COCO .machine = "coco,coco3"
#define M_COCO12 .machine = "coco"
#define M_COCO3 .machine = "coco3"
#define M_MC10 .machine = "mc10"

static const struct rom_meta_internal rom_internal[] = {
	// Dragon system ROMs
	{ DESC(0x84f68bf9, 0x4000, "Dragon 64 32K BASIC (1983) (Dragon Data Ltd)"), P_DRAGON64, S_D64_1 },
	{ DESC(0x17893a42, 0x4000, "Dragon 64 64K BASIC (1983) (Dragon Data Ltd)"), P_DRAGON64, .bank = 1 },
	{ DESC(0xe3879310, 0x4000, "Dragon 32 BASIC (1982) (Dragon Data Ltd)"), P_DRAGON32, S_D32 },
	{ DESC(0x95af0a0a, 0x4000, "Dragon 200-E 32K BASIC (Eurohard S.A.)"), P_DRAGON64, S_D64_1 },
	{ DESC(0x48b985df, 0x4000, "Dragon 200-E 64K BASIC (Eurohard S.A.)"), P_DRAGON64, S_D64_1, .bank = 1 },
	{ DESC(0x565724bc, 0x1000, "Dragon 200-E Character Set (Eurohard S.A.)"), P_DRAGON64, S_D64_1, .bank = 2 },
	{ DESC(0xd6172b56, 0x2000, "Dragon Professional Boot 0.4 (Dragon Data Ltd)"), P_DRAGONPRO, .bank = 1 },
	{ DESC(0xc3dab585, 0x2000, "Dragon Professional Boot 1.0 (Dragon Data Ltd)"), P_DRAGONPRO, .bank = 1 },

	// Dragon system ROMs (alternates, bad dumps)
	{ DESC(0x60a4634c, 0x4000, "Dragon 64 32K BASIC (Dragon Data Ltd) [bad]"), P_DRAGON64, S_D64_1 },
	{ DESC(0xee33ae92, 0x4000, "Dragon 64 32K BASIC (Dragon Data Ltd, p:woolham)"), P_DRAGON64, S_D64_1 },
	{ DESC(0x1660ae35, 0x4000, "Dragon 64 64K BASIC (Dragon Data Ltd, p:woolham)"), P_DRAGON64, .bank = 1 },
	{ DESC(0xff7bf41e, 0x4000, "Dragon 32 BASIC (Dragon Data Ltd, p:woolham)"), P_DRAGON32, S_D32 },
	{ DESC(0x9c7eed69, 0x4000, "Dragon 32 BASIC (Dragon Data Ltd, p:woolham)"), P_DRAGON32, S_D32 },

	// Tandy system ROMs
	{ DESC(0x00b50aaa, 0x2000, "Colour BASIC 1.0 (Tandy)"), P_COCO, S_BAS10 },
	{ DESC(0x6270955a, 0x2000, "Colour BASIC 1.1 (Tandy)"), P_COCO, S_BAS11 },
	{ DESC(0x54368805, 0x2000, "Colour BASIC 1.2 (Tandy)"), P_COCO, S_BAS12 },
	{ DESC(0xd8f4d15e, 0x2000, "Colour BASIC 1.3 (Tandy)"), P_COCO, S_BAS13 },
	{ DESC(0xe031d076, 0x2000, "Extended Colour BASIC 1.0 (Tandy)"), P_COCO, .slot = 1 },
	{ DESC(0xa82a6254, 0x2000, "Extended Colour BASIC 1.1 (Tandy)"), P_COCO, .slot = 1 },
	{ DESC(0xd918156e, 0x2000, "Colour BASIC (Dynacom, MX-1600)"), P_COCO, S_BAS10 },
	{ DESC(0x322a3d58, 0x2000, "Extended Colour BASIC (Dynacom, MX-1600)"), P_COCO, .slot = 1 },

	// Tandy system ROMs (alternates, bad dumps)
	{ DESC(0x6111a086, 0x2000, "Extended Colour BASIC 1.0 (Tandy) [bad]"), P_COCO, .slot = 1 },
	{ DESC(0xd11b1c96, 0x2000, "Colour BASIC (Dynacom, MX-1600, p:zephyr)"), P_COCO, S_BAS10 },

	// CoCo 3 system ROMs
	{ DESC(0xb4c88d6c, 0x8000, "Super Extended Colour BASIC (Tandy)"), P_COCO3, S_COCO3 },
	{ DESC(0xff050d80, 0x8000, "Super Extended Colour BASIC (Tandy) (PAL)"), P_COCO3, S_COCO3 },

	// Deluxe CoCo system ROMs
	{ DESC(0x1cce231e, 0x8000, "Advanced Colour BASIC 00.00.07 (Tandy)"), P_DELUXECOCO },

	// MC-10 system ROMs
	{ DESC(0x11fda97e, 0x2000, "Microcolour BASIC (Tandy)"), P_MC10, S_MC10 },
	{ DESC(0xf876abe9, 0x2000, "Microcolour BASIC (Tandy) (Alice)"), P_MC10, S_MC10 },

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// DragonDOS
	{ DESC(0xb44536f6, 0x2000, "DragonDOS 1.0 (Dragon Data Ltd)"), P_DRAGONDOS },
	{ DESC(0x67bd6e27, 0x2000, "DragonDOS 1.3A (Dragon Data Ltd)"), P_DRAGONDOS },
	{ DESC(0x0d1b492c, 0x2000, "DragonDOS 1.5 (Dragon Data Ltd)"), P_DRAGONDOS },
	{ DESC(0x14f4c54a, 0x2000, "DragonDOS 4.0 (Eurohard S.A.)"), P_DRAGONDOS },
	{ DESC(0x16d25658, 0x2000, "DragonDOS 4.1 (Eurohard S.A.)"), P_DRAGONDOS },
	{ DESC(0x6bb0b4bb, 0x2000, "DragonDOS 4.2 (Eurohard S.A.)"), P_DRAGONDOS },
	{ DESC(0xd4d954a0, 0x2000, "DOSplus 4.8 (S3)"), P_DRAGONDOS },
	{ DESC(0x7c6dfca8, 0x2000, "DOSplus 4.9B (S3)"), P_DRAGONDOS },
	{ DESC(0x8023c1c8, 0x2000, "SuperDOS E4 (PNP)"), P_DRAGONDOS },
	{ DESC(0x460b703a, 0x2000, "SuperDOS E5 (PNP)"), P_DRAGONDOS },
	{ DESC(0x8c1d6c45, 0x2000, "SuperDOS E6 (PNP)"), P_DRAGONDOS },
	{ DESC(0x5d7779b7, 0x2000, "SuperDOS E7T (PNP)"), P_RSDOS },

	// Delta System
	{ DESC(0x149eb4dd, 0x2000, "Delta System 1A (Premier Microsystems)"), P_DELTA },
	{ DESC(0x307fb37c, 0x2000, "Delta System 2.0 (Premier Microsystems)"), P_DELTA },

	// RS-DOS
	{ DESC(0xb4f9968e, 0x2000, "Disk Extended Colour BASIC 1.0 (Tandy)"), P_RSDOS },
	{ DESC(0x0b9c5415, 0x2000, "Disk Extended Colour BASIC 1.1 (Tandy)"), P_RSDOS },
	{ DESC(0xe9ad60a0, 0x2000, "DOS-400 (Prológica)"), P_RSDOS },

	// MOOH
	// SDBBOOT 64K image is 8K image repeated 8 times:
	{ DESC(0xb703acc8, 0x2000, "MOOH SDBBOOT ROM V1 (Tormod Volden)"), .part = "mooh" },
	{ DESC(0x8ad667ac, 0x10000, "MOOH SDBBOOT ROM V1 (Tormod Volden)"), .part = "mooh" },

	// IDE
	{ DESC(0xe6f24735, 0x2000, "HDB-DOS 1.5 Becker CoCo 3"), P_IDE },
	{ DESC(0xd7e7df0c, 0x2000, "HDB-DOS 1.4 DW3 CoCo 2"), P_IDE },
	{ DESC(0xabf3a8dd, 0x2000, "HDB-DOS 1.4 LBA"), P_IDE },
	{ DESC(0xdffc86c4, 0x2000, "YA-DOS 0.5B Picard"), P_IDE },
	{ DESC(0x53e22a43, 0x2000, "YA-DOS 0.5D Picard"), P_IDE },
	{ DESC(0x0baadcc4, 0x2000, "YA-DOS 0.5E Picard"), P_IDE },

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Dragon carts
	{ DESC(0xaf91e6ff, 0x2000, "Alldream (1983) (Dragon Data Ltd)"), P_ROM, M_DRAGON },
	{ DESC(0x61143386, 0x2000, "Astroblast (1982) (Dragon Data Ltd) (A0107)"), P_ROM, M_DRAGON },
	{ DESC(0x7901a633, 0x2000, "Berserk (1982) (Dragon Data Ltd) (A0100)"), P_ROM, M_DRAGON },
	{ DESC(0xbb31add1, 0x2000, "Bridge Master (1983) (Dragon Data Ltd)"), P_ROM, M_DRAGON },
	{ DESC(0x8bcaed11, 0x1000, "Cave Hunter (1982) (Dragon Data Ltd)"), P_ROM, M_DRAGON },
	{ DESC(0xd4c36a96, 0x2000, "Chess (1982) (Dragon Data Ltd) (A0108)"), P_ROM, M_DRAGON },
	{ DESC(0x2ad2f4e0, 0x2000, "Cosmic Invaders (1982) (Dragon Data Ltd) (A0102)"), P_ROM, M_DRAGON },
	{ DESC(0x7e1cc4b3, 0x2000, "Demonstration Cartridge (1982) (Dragon Data Ltd) (A0109)"), P_ROM, M_DRAGON },
	{ DESC(0x863cfcfc, 0x2000, "Diagnostic ROM (1982) (Dragon Data Ltd)"), P_ROM, M_DRAGON },
	{ DESC(0x410a0332, 0x2000, "Doodle Bug (1982) (Dragon Data Ltd) (A0110)"), P_ROM, M_DRAGON },
	{ DESC(0x41c61438, 0x1000, "Dragon 32 Soak Test (198x) (Dragon Data Ltd)"), P_ROM, M_DRAGON32 },
	{ DESC(0xa77bdb5b, 0x4000, "Dragon Computer Controller (19xx)"), P_ROM, M_DRAGON },
	{ DESC(0xc6820601, 0x2000, "Dragon Viewdata Terminal (198x) (Dragon Data Ltd)"), P_ROM, M_DRAGON },
	{ DESC(0xc05955e9, 0x2000, "Flagon Bird v1.0 (2014) (Bosco)"), P_ROM, M_DRAGON },
	{ DESC(0x07bdebb2, 0x4000, "Flagon Bird v1.1 (2014) (Bosco)"), P_ROM, M_DRAGON },
	{ DESC(0x45c089f6, 0x2000, "Ghost Attack (1982) (Dragon Data Ltd) (A0103)"), P_ROM, M_DRAGON },
	{ DESC(0x6628d50b, 0x2000, "Logo (198x) (Dragon Data Ltd) (F20311)"), P_ROM, M_DRAGON },
	{ DESC(0x0f789b79, 0x2000, "MACE (1983) (Windrush Micro Systems)"), P_ROM, M_DRAGON },
	{ DESC(0x64aba2b0, 0x1000, "Meteoroids (1982) (Dragon Data Ltd) (A0101)"), P_ROM, M_DRAGON },
	{ DESC(0x53e4be03, 0x2000, "Rail Runner (1982) (Dragon Data Ltd) (A0111)"), P_ROM, M_DRAGON },
	{ DESC(0x06c9b4e7, 0x2000, "Starship Chameleon (1982) (Dragon Data Ltd) (A0106)"), P_ROM, M_DRAGON },

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// CoCo carts
	{ DESC(0x7d1cac0e, 0x2000, "Androne (1983) (Tandy) (26-3096)"), P_ROM, M_COCO },
	{ DESC(0x2fdf5b58, 0x1000, "Art Gallery (1981) (Robert G. Kilgus) (26-3061)"), P_ROM, M_COCO },
	{ DESC(0xf76f6fbe, 0x4000, "Atom (1983) (Tandy) (26-3149)"), P_ROM, M_COCO },
	{ DESC(0x16d2d946, 0x800 , "Audio Spectrum Analyzer (1981) (Tandy) (26-3156)"), P_ROM, M_COCO },
	{ DESC(0x0d964862, 0x800 , "Audio Spectrum Analyzer v2 (1983) (Tandy) (26-3156)"), P_ROM, M_COCO },
	{ DESC(0xa3b8ba85, 0x1000, "Backgammon (1980) (Tandy) (26-3059)"), P_ROM, M_COCO },
	{ DESC(0x3be0cf60, 0x1000, "Bingo Math (1980) (Tandy) (26-3150)"), P_ROM, M_COCO },
	{ DESC(0xd6b940f1, 0x2000, "Bridge Tutor I (1982) (Philidor Software) (26-3158)"), P_ROM, M_COCO },
	{ DESC(0xc7ed9d30, 0x1000, "Bustout (1981) (Tandy) (26-3056)"), P_ROM, M_COCO },
	{ DESC(0x41dd5a1a, 0x2000, "Canyon Climber (1982) (Tandy) (26-3089)"), P_ROM, M_COCO },
	{ DESC(0x8869eddc, 0x1000, "Castle Guard (1981) (The Image Producers) (26-3079)"), P_ROM, M_COCO },
	{ DESC(0x05dc5ef3, 0x1000, "Checker King (1980) (Personal Software) (26-3055)"), P_ROM, M_COCO },
	{ DESC(0xfe4c93e4, 0x2000, "Clowns & Balloons (1982) (Tandy) (26-3087)"), P_ROM, M_COCO },
	{ DESC(0xad1937c1, 0x2000, "Color Baseball (1980) (Dale A. Lear) (26-3095)"), P_ROM, M_COCO },
	{ DESC(0xf4be90bc, 0x1000, "Color Cubes (1981) (Robert G. Kilgus) (26-3075)"), P_ROM, M_COCO },
	{ DESC(0xd78be10a, 0x1000, "Color File (1981) (Tandy) (26-3103)"), P_ROM, M_COCO },
	{ DESC(0x8acd7ea2, 0x2800, "Color Forth (1981) (Microworks)"), P_ROM, M_COCO },
	{ DESC(0x9fb1e7d9, 0x2000, "Color Logo (1983) (Larry Kheriaty & George Gerhold) (26-2722)"), P_ROM, M_COCO },
	{ DESC(0x3aca199b, 0x2000, "Color Scripsit (1981) (Tandy) (26-3105)"), P_ROM, M_COCO },
	{ DESC(0x7bffd03a, 0x4000, "Color Scripsit II (1986) (Tandy) (26-3109)"), P_ROM, M_COCO },
	{ DESC(0x06075f2a, 0x1000, "Crosswords (1981) (26-3082)"), P_ROM, M_COCO },
	{ DESC(0xbfa3585d, 0x4000, "Cyrus World Class Chess (1983) (Tandy) (26-3064)"), P_ROM, M_COCO },
	{ DESC(0xd990e1f9, 0x1000, "Deluxe RS-232 Program Pak (1983) (26-2226) (Tandy)"), P_ROM, M_COCO },  // XXX needs rs232 pak
	{ DESC(0x1199d27f, 0x2000, "Demolition Derby (1984) (Tandy) (26-3044)"), P_ROM, M_COCO },
	{ DESC(0xb7a1aa3e, 0x3f00, "Demon Attack (1984) (Tandy) (26-3099)"), P_ROM, M_COCO },
	{ DESC(0xd5257b50, 0x800 , "Diagnostics (1980) (Tandy) (26-3019)"), P_ROM, M_COCO },
	{ DESC(0x8cd56308, 0x800 , "Diagnostics v2.0 (1982) (Tandy) (26-3019)"), P_ROM, M_COCO },
	{ DESC(0xba1d9e81, 0x2000, "Dino Wars (1981) (Tandy) (26-3057)"), P_ROM, M_COCO },
	{ DESC(0x667bc55d, 0x2000, "Direct Connect Modem Pak (1985) (26-2228) (Tandy)"), P_ROM, M_COCO },
	{ DESC(0x819fc9fb, 0x2000, "Don Pan (1985) (Tandy) (26-3097)"), P_ROM, M_COCO },
	{ DESC(0xb86830a9, 0x3f00, "Doodle Bug 1 (1982) (26-xxxx) (Computerware)"), P_ROM, M_COCO },
	{ DESC(0xc3d41232, 0x2000, "Doodle Bug 2 (1982) (26-xxxx) (Computerware)"), P_ROM, M_COCO },
	{ DESC(0xea820c39, 0x2000, "Doodle Bug 3 (1982) (26-xxxx) (Computerware)"), P_ROM, M_COCO },
	{ DESC(0x4cc44337, 0x1000, "Doubleback (1982) (Tandy) (26-3091)"), P_ROM, M_COCO },
	{ DESC(0xa923f5a2, 0x2000, "Downland v1.0 (1983) (Tandy) (26-3046)"), P_ROM, M_COCO },
	{ DESC(0x0229c319, 0x2000, "Downland v1.1 (1983) (Tandy) (26-3046)"), P_ROM, M_COCO },
	{ DESC(0x6f1e913a, 0x4000, "Dragonfire (1984) (Tandy) (26-3098)"), P_ROM, M_COCO },
	{ DESC(0xf4374a55, 0x4000, "EDTASM+ (1982) (26-3250) (Tandy)"), P_ROM, M_COCO },
	{ DESC(0x7a1c2f2d, 0x2000, "Facemaker (1984) (Tandy) (26-3166)"), P_ROM, M_COCO },
	{ DESC(0xf17b5b38, 0x2000, "Football (1980) (26-3053) (Tandy)"), P_ROM, M_COCO },
	{ DESC(0x93153b15, 0x2000, "Fraction Fever (1984) (Spinnaker) (26-3169)"), P_ROM, M_COCO },
	{ DESC(0x984ee0d9, 0x1000, "Galactic Attack (1982) (Tandy) (26-3066)"), P_ROM, M_COCO },
	{ DESC(0xfd84fc5c, 0x2000, "Gomoku-Renju (1983) (Tandy) (26-3069)"), P_ROM, M_COCO },
	{ DESC(0xb2e463b6, 0x2000, "Kids on Keys (1984) (Spinnaker) (26-3167)"), P_ROM, M_COCO },
	{ DESC(0x36eecac4, 0x2000, "Kindercomp (1984) (Tandy) (26-3168)"), P_ROM, M_COCO },
	{ DESC(0x4cee1c54, 0x2000, "Mega-Bug (1982) (Tandy) (26-3076)"), P_ROM, M_COCO },
	{ DESC(0x66130daa, 0x2000, "Micro Chess V2.0 (1980) (Personal Software) (26-3050)"), P_ROM, M_COCO },
	{ DESC(0xd4320300, 0x1000, "Micro Painter (1982) (Datasoft) (26-3077)"), P_ROM, M_COCO },
	{ DESC(0x24772d4f, 0x1000, "Microbes (1981) (Tandy) (26-3085)"), P_ROM, M_COCO },
	{ DESC(0xf7653118, 0x1000, "Monster Maze (1983) (Tandy) (26-3081)"), P_ROM, M_COCO },
	{ DESC(0x90e48836, 0x1000, "Music (1980) (Tandy) (26-3151)"), P_ROM, M_COCO },
	{ DESC(0x54094dea, 0x2000, "Panic Button (1983) (Tandy) (26-3147)"), P_ROM, M_COCO },
	{ DESC(0xd577436a, 0x2000, "Personal Finance (1980) (Tandy) (26-3101)"), P_ROM, M_COCO },
	{ DESC(0x73c41fde, 0x2000, "Personal Finance II (1983) (Tandy) (26-3106)"), P_ROM, M_COCO },
	{ DESC(0xd5baf3ad, 0x1000, "Pinball (1980) (Tandy) (26-3052)"), P_ROM, M_COCO },
	{ DESC(0xf7dcc3bb, 0x1000, "Polaris (1981) (Tandy) (26-3065)"), P_ROM, M_COCO },
	{ DESC(0x770e5f3a, 0x2000, "Poltergeist (1982) (Tandy) (26-3073)"), P_ROM, M_COCO },
	{ DESC(0x7f507089, 0x800 , "Popcorn (1981) (Tandy) (26-3090)"), P_ROM, M_COCO },
	{ DESC(0x4123fb50, 0x2000, "Project Nebula (1981) (Tandy) (26-3063)"), P_ROM, M_COCO },
	{ DESC(0x3218e0f5, 0x1000, "Quasar Commander (1980) (Tandy) (26-3051)"), P_ROM, M_COCO },
	{ DESC(0xfad4c7e3, 0x1000, "Reactoid (1983) (Tandy) (26-3092)"), P_ROM, M_COCO },
	{ DESC(0x3bf566bb, 0x2000, "Roman Checkers (1981) (Tandy) (26-3071)"), P_ROM, M_COCO },
	{ DESC(0x1f1ba95d, 0x2000, "Shooting Gallery (1982) (Tandy) (26-3088)"), P_ROM, M_COCO },
	{ DESC(0x1a05a395, 0x2000, "Skiing (1981) (Tandy) (26-3058)"), P_ROM, M_COCO },
	{ DESC(0xb98d5eaf, 0x2000, "Slay the Nereis (1983) (Tandy) (26-3086)"), P_ROM, M_COCO },
	{ DESC(0x44390e55, 0x4000, "Soko-Ban (1988) (Tandy) (26-3161)"), P_ROM, M_COCO },
	{ DESC(0xf4700120, 0x1000, "Space Assault (1981) (Tandy) (26-3060)"), P_ROM, M_COCO },
	{ DESC(0x80d7ac8b, 0x3e00, "Spectaculator (1983) (Tandy) (26-3104)"), P_ROM, M_COCO },
	{ DESC(0xec9d0199, 0x1000, "Spidercide (1983) (Tandy) (26-3049)"), P_ROM, M_COCO },
	{ DESC(0x36f31eb7, 0x2000, "Starblaze (1983) (Greg Zumwalt) (26-3094)"), P_ROM, M_COCO },
	{ DESC(0xaeb38e39, 0x2000, "Stellar Lifeline (1983) (Tandy) (26-3047)"), P_ROM, M_COCO },
	{ DESC(0xf4f2b0a0, 0x2000, "Temple of Rom (1984) (Tandy) (26-3045)"), P_ROM, M_COCO },
	{ DESC(0x99de1a03, 0x1000, "Tennis (1981) (Tandy) (26-3080)"), P_ROM, M_COCO },
	{ DESC(0x54f38ab8, 0x3ff0, "TypeMate (1988) (26-3155) (ZCT Systems)"), P_ROM, M_COCO },
	{ DESC(0xcebd3196, 0x1000, "Typing Tutor (1980) (Leah R. O'Connor) (26-3152)"), P_ROM, M_COCO },
	{ DESC(0x8987b1e3, 0x800 , "Videotex v1.1 (1981) (Tandy) (26-2222)"), P_ROM, M_COCO },
	{ DESC(0x1cd04106, 0x800 , "Videotex v1.2 (1981) (Tandy) (26-2222)"), P_ROM, M_COCO },
	{ DESC(0xab96914a, 0x1000, "Wildcatting (1982) (Tandy) (26-3067)"), P_ROM, M_COCO },

	// CoCo carts (alternates, bad dumps)
	{ DESC(0x498fde21, 0x4000, "Arkanoid (1987) (Taito) [coco12]"), P_ROM, M_COCO },
	{ DESC(0xbc930185, 0x2000, "Color Scripsit (1981) (Tandy) (26-3105) [alt]"), P_ROM, M_COCO },
	{ DESC(0x7abb161b, 0x2000, "Micro Chess V2.0 (1980) (Personal Software) (26-3050) [alt]"), P_ROM, M_COCO },
	{ DESC(0xbed7dcde, 0x4000, "Silpheed (1988) (Sierra) [coco12]"), P_ROM, M_COCO3 },

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// CoCo 3 carts
	{ DESC(0xd4bbe731, 0x4000, "A Mazing World of Malcom Mortar (1987) (Tandy) (26-3160)"), P_ROM, M_COCO3 },
	{ DESC(0x2fab4955, 0x8000, "Arkanoid (1987) (Taito)"), P_ROM, M_COCO3 },
	{ DESC(0x82929650, 0x4000, "Castle of Tharoggad (1988) (Tandy) (26-3159)"), P_ROM, M_COCO3 },
	{ DESC(0xd45e59e3, 0x2000, "Dungeons of Daggorath (1982) (Tandy) (26-3093)"), P_ROM, M_COCO3 },
	{ DESC(0x899978e7, 0x8000, "GFL Championship Football II (1988) (ZCT Systems)"), P_ROM, M_COCO3 },
	{ DESC(0x83bd6056, 0x8000, "Mind Roll (1988) (Tandy) (26-3100)"), P_GMC, .machine = "coco3,coco" },
	{ DESC(0xa9680ede, 0x10000, "Predator (1989) (Tandy) (26-3165)"), P_GMC, M_COCO3 },
	{ DESC(0xc8b64049, 0x8000, "RAD Warrior (1987) (Tandy)"), P_ROM, M_COCO3 },
	{ DESC(0x09c2e97d, 0x8000, "Rampage! (1989) (Activision)"), P_ROM, M_COCO3 },
	{ DESC(0xdd94dd06, 0x20000, "RoboCop (1988) (Tandy) (26-3164)"), P_GMC, M_COCO3 },
	{ DESC(0x3dc0ba73, 0x4000, "Shanghai (1987) (Tandy) (26-3084)"), P_ROM, M_COCO3 },
	{ DESC(0xccfd0a0c, 0x8000, "Silpheed (1988) (Sierra)"), P_ROM, M_COCO3 },
	{ DESC(0xf47d3880, 0x4000, "Springster (1987) (Tandy) (26-3078)"), P_ROM, M_COCO3 },
	{ DESC(0xe8e54cbe, 0x8000, "Super Pitfall (1988) (Activision)"), P_ROM, M_COCO3 },
	{ DESC(0x8375f98b, 0x4000, "Tetris (1987) (Tandy) (26-3163)"), P_ROM, .machine = "coco3,coco" },
	{ DESC(0x0ef0df20, 0x4000, "Thexder (1987) (Tandy) (26-3072)"), P_ROM, M_COCO3 },

	// CoCo 3 carts (alternates, bad dumps)
	{ DESC(0xc985282a, 0x2000, "Dungeons of Daggorath (1982) (Tandy) (26-3093) [f shield, Aaron Oliver]"), P_ROM, M_COCO3 },
	{ DESC(0x878906fe, 0x8000, "Mind Roll (1988) (Tandy) (26-3100) [f plane1]"), P_GMC, .machine = "coco3,coco" },

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Dragon & CoCo GMC carts
	{ DESC(0xabe7bb9e, 0x4000, "Blockdown (2021) (Teipen Mwnci)"), P_GMC, .machine = "dragon32,dragon64,coco,coco3" },
	{ DESC(0x58716b7f, 0x10000, "Dunjunz (2020) (Teipen Mwnci)"), P_GMC, .machine = "dragon64,coco3,dragon32,coco" },

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Other
	{ DESC(0x480032e2, 0x2000, "DOS-Dream (1986) (Grosvenor Software)"), P_DRAGONDOS, .slot = 1 },
	{ DESC(0x15fb39af, 0x2000, "Orchestra-90/CC (1984) (Tandy) (26-3143)"), P_ORCH90 },
};

static struct rom_meta *rom_meta_new_from_internal(const struct rom_meta_internal *rmi) {
	struct rom_meta *rm = xmalloc(sizeof(*rm));
	*rm = (struct rom_meta){0};
	rm->description = xstrdup(rmi->description);
	rm->size = rmi->size;
	rm->crc32 = rmi->crc32;
	if (rmi->symtab)
		rm->symtab = xstrdup(rmi->symtab);
	if (rmi->part)
		rm->part = xstrdup(rmi->part);
	if (rmi->machine)
		rm->machine = xstrdup(rmi->machine);
	if (rmi->cart)
		rm->cart = xstrdup(rmi->cart);
	rom_meta_list = slist_prepend(rom_meta_list, rm);
	return rm;
}

struct rom_meta *rom_meta_by_crc32(uint32_t crc32, uint32_t size) {
	// First, scan the dynamic list
	for (struct slist *iter = rom_meta_list; iter; iter = iter->next) {
		struct rom_meta *rm = iter->data;
		if (rm->crc32 == crc32 && rm->size == size) {
			return rm;
		}
	}
	// Next, scan the builtin list and create a dynamic entry if found
	for (size_t i = 0; i < ARRAY_N_ELEMENTS(rom_internal); ++i) {
		const struct rom_meta_internal *rmi = &rom_internal[i];
		if (rmi->size == size && rmi->crc32 == crc32) {
			return rom_meta_new_from_internal(rmi);
		}
	}
	return NULL;
}

static void rom_meta_free(void *sptr) {
	struct rom_meta *rm = sptr;
	free(rm->description);
	free(rm->symtab);
	free(rm->part);
	free(rm->machine);
	free(rm->cart);
	free(rm);
}

void rom_meta_remove_all(void) {
	slist_free_full(rom_meta_list, (slist_free_func)rom_meta_free);
	rom_meta_list = NULL;
}
