// List of software to present to the user in drop-down menus.

const software = [

	{
		'name': 'Games',
		'description': 'Games',
		'entries': [

			{
				'name': 'Dunjunz',
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
				'name': 'NitrOS-9',
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
