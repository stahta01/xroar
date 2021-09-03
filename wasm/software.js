// The way initial autodetection of available machines and cartridges currently
// works in XRoar, ROM image files need to exist in advance.  They don't have
// to contain anything though, so we create a set of zero-length "stub" files,
// listed here, that trigger fetching the full ROM later.

const romstubs = [
	// Dragon 32
	'd32.rom',
	// Dragon 64
	'd64_1.rom',
	'd64_2.rom',
	// Dragon 200-E
	'd200e_1.rom',
	'd200e_2.rom',
	'd200e_26.rom',
	// DragonDOS
	'dplus49b.rom',
	'sdose6.rom',
	// CoCo
	'bas13.rom',
	'extbas11.rom',
	// CoCo 3
	'coco3.rom',
	'coco3p.rom',
	// RS-DOS
	'disk11.rom',
];

// List of software to present to the user in drop-down menus.

const software = [

	{
		'name': 'Games',
		'description': 'Games',
		'entries': [

			{
				'description': 'Dunjunz',
				'author': 'Ciaran Anscomb',
				'machine': 'dragon64',
				'autorun': 'dunjunz.cas',
			},

			// other possible fields:
			// 'cart': named cartridge, eg 'dragondos, 'rsdos'
			// 'cart_rom': primary rom image for cart
			// 'cart_rom2': secondary ($E000-) rom image for cart
			// 'disks': [ ... ] array of disk images indexed by drive
			// 'basic': string to type into BASIC
			// 'joy_right' and 'joy_left': plug in joystick, eg
			//     'kjoy0' (cursors+alt) or 'mjoy0' (mouse+button)

			{
				'description': 'NitrOS-9',
				'machine': 'dragon64',
				'cart': 'dragondos',
				'disks': [ 'NOS9_6809_L1_80d.dsk' ],
				'basic': '\003BOOT\r',
			},
			// ... repeat for each program in this menu

		]
	},
	// ... repeat for each drop-down menu

];
