/*  Copyright 2007,2012 Ciaran Anscomb
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include <SDL_image.h>

const char *argv0;
const char *array_name = "font";
const char *array_type = "unsigned int";
enum vdg_type { VDG, VDGT1 };

static void print_byte(uint8_t b);
static void end_line(void);
static int getpixel(SDL_Surface *surface, int x, int y);
static void print_usage(FILE *f);

#ifdef main
#undef main
#endif

int main(int argc, char **argv) {
	int header_only = 0;
	enum vdg_type output_mode = VDG;
	int nchars = 64;
	int pad_top = 3;
	int pad_bottom = 2;
	Uint32 pmask = ~0;
	int i;

	argv0 = argv[0];
	for (i = 1; i < argc; i++) {
		if (0 == strcmp(argv[i], "--")) {
			i++;
			break;
		}
		if (argv[i][0] != '-')
			break;
		if (0 == strcmp(argv[i], "-h") || 0 == strcmp(argv[i], "--help")) {
			print_usage(stdout);
			exit(EXIT_SUCCESS);
		}
		if (0 == strcmp(argv[i], "--vdg")) {
			output_mode = VDG;
			nchars = 64;
			pad_top = 3;
		} else if (0 == strcmp(argv[i], "--vdgt1")) {
			output_mode = VDGT1;
			nchars = 128;
			pad_top = 1;
		} else if (0 == strcmp(argv[i], "--array") && i+1 < argc) {
			array_name = argv[++i];
		} else if (0 == strcmp(argv[i], "--type") && i+1 < argc) {
			array_type = argv[++i];
		} else if (0 == strcmp(argv[i], "--header")) {
			header_only = 1;
		} else {
			fprintf(stderr, "%s: unrecognised option '%s'\nTry '%s --help' for more information.\n", argv0, argv[i], argv0);
			exit(EXIT_FAILURE);
		}
	}

	if (i >= argc) {
		print_usage(stderr);
		exit(EXIT_FAILURE);
	}

	SDL_Surface *in = IMG_Load(argv[i]);
	if (in == NULL) {
		fprintf(stderr, "%s: %s: %s\n", argv0, argv[i], IMG_GetError());
		exit(EXIT_FAILURE);
	}
	int fheight = in->h / 6;
	if (in->w != 128 || fheight < 7) {
		fprintf(stderr, "%s: %s: Wrong resolution for a font image file\n", argv0, argv[i]);
		exit(EXIT_FAILURE);
	}
	if (in->format->BytesPerPixel == 4) {
		pmask = in->format->Rmask | in->format->Gmask | in->format->Bmask;
	}

	pad_bottom = 12 - fheight - pad_top;

	printf("/* Automatically generated\n * by %s from %s */\n\n", argv0, argv[i]);
	if (header_only) {
		printf("extern const %s %s[%d];\n\n", array_type, array_name, nchars * 12);
		return 0;
	}
	printf("const %s %s[%d] = {\n", array_type, array_name, nchars * 12);

	for (i = 0; i < nchars; i++) {
		int c;
		uint8_t invert;
		switch (output_mode) {
		default:
		case VDG:
			c = (i & 0x3f) ^ 0x20;
			invert = 0;
			break;
		case VDGT1:
			if (i < 32) {
				c = i + 64;
				invert = 0;
			} else if (i < 64) {
				c = i - 32;
				invert = 0xff;
			} else if (i < 96) {
				c = i - 32;
				invert = 0;
			} else {
				c = i - 96;
				invert = 0;
			}
			break;
		}
		int xbase = (c & 15) * 8;
		int ybase = (c >> 4) * fheight;
		printf("\t");
		for (int j = 0; j < pad_top; j++) print_byte(0 ^ invert);
		for (int j = 0; j < fheight; j++) {
			int b = 0;
			for (int k = 0; k < 8; k++) {
				b <<= 1;
				b |= ((getpixel(in, xbase + k, ybase + j) & pmask) == 0) ? 0 : 1;
			}
			print_byte(b ^ invert);
		}
		for (int j = 0; j < pad_bottom; j++) print_byte(0 ^ invert);
		if ((i + 1) < nchars)
			printf(",");
		end_line();
	}
	printf("};\n\n");

	return 0;
}

static _Bool first_byte = 1;

static void print_byte(uint8_t b) {
	if (!first_byte)
		printf(", ");
	printf("0x%02x", b);
	first_byte = 0;
}

static void end_line(void) {
	printf("\n");
	first_byte = 1;
}

static int getpixel(SDL_Surface *surface, int x, int y) {
	int bpp = surface->format->BytesPerPixel;
	Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
	Uint32 dp;
	switch(bpp) {
		case 1: dp = *p; break;
		case 2: dp = *(Uint16 *)p; break;
		case 3:
			if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
				dp = p[0] << 16 | p[1] << 8 | p[2];
			else
				dp = p[0] | p[1] << 8 | p[2] << 16;
			break;
		case 4: dp = *(Uint32 *)p; break;
		default: dp = 0; break;
	}
	Uint8 dr, dg, db;
	SDL_GetRGB(dp, surface->format, &dr, &dg, &db);
	float yy = 0.299 * (float)dr + 0.587 * (float)dg + 0.114 * (float)db;
	return (yy >= 128.0);
}

static void print_usage(FILE *f) {
	fprintf(f, "Usage: %s [OPTION]... font-image-file\n\n", argv0);
	fprintf(f, "      --array                name of array to use in generated C code [font]\n");
	fprintf(f, "      --type                 data type for generated array [unsigned int]\n");
	fprintf(f, "      --vdg                  pad out font to 12-lines for VDG code\n");
	fprintf(f, "  -h, --help     display this help and exit\n");
}
