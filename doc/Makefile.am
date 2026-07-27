EXTRA_DIST =

AM_MAKEINFOHTMLFLAGS = --no-headers --no-split

man_MANS = xroar.1

info_TEXINFOS = xroar.texi

EXTRA_DIST += \
	building-for-windows.md \
	xroar-example.conf \
	xroar.pdf \
	images/file-run.png \
	images/drive-control.png \
	images/gamepad.png \
	images/hardware-cartridge.png \
	images/hardware-machine.png \
	images/menubar.png \
	images/printer-control.png \
	images/tape-input.png \
	images/tool-kbd-lang.png \
	images/trouble-cross-colour.png \
	images/trouble-no-basic.png \
	images/tv-controls.png \
	images/view-composite-rendering.png \
	images/view-tv-input.png \
	manpage-header \
	xroar-man.html

HTML2MAN = $(top_builddir)/tools/html2man.pl

xroar.1.in: manpage-header xroar-man.html
	(cat manpage-header; hxnormalize -xe -l 99999 xroar-man.html | hxpipe | $(HTML2MAN)) > $@

xroar-man.pdf: xroar.1
	groff -Tpdf -P-pa4 -t -mpdfmark -mandoc $< > $@
