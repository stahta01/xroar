EXTRA_DIST =

AM_MAKEINFOHTMLFLAGS = --no-headers --no-split

man_MANS = xroar.1

info_TEXINFOS = xroar.texi

EXTRA_DIST += \
	building-for-windows.md \
	xroar-example.conf \
	xroar.pdf \
	file-run.png \
	drive-control.png \
	gamepad.png \
	hardware-cartridge.png \
	hardware-machine.png \
	menubar.png \
	printer-control.png \
	tape-input.png \
	tool-kbd-lang.png \
	trouble-cross-colour.png \
	trouble-no-basic.png \
	tv-controls.png \
	view-composite-rendering.png \
	view-tv-input.png

xroar.1.in: manpage-header xroar-man.html
	(cat manpage-header; hxnormalize -xe -l 99999 xroar-man.html | hxpipe | ./html2man.pl) > $@

xroar-man.pdf: xroar.1
	groff -Tpdf -P-pa4 -t -mpdfmark -mandoc $< > $@
