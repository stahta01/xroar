ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = \
	README \
	README.SDS \
	COPYING.GPL \
	COPYING.LGPL \
	top-config.h \
	wasm/index.shtml \
	wasm/software.js \
	wasm/xroar-wasm.css \
	wasm/xroar-wasm.html

SUBDIRS = portalib src doc
DIST_SUBDIRS = tools portalib src doc

clean-local:
	rm -f a.wasm

clena: clean
	@echo "How much clena do you want it?"
