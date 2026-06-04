/** \file
 *
 *  \brief GDB target description files.
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

#include "array.h"

#include "gdb_annex.h"

static const char m6801_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
"<target>"
  "<architecture>m6801</architecture>"
  "<xi:include href=\"m6801-core.xml\"/>"
"</target>";

static const char m6803_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
"<target>"
  "<architecture>m6803</architecture>"
  "<xi:include href=\"m6801-core.xml\"/>"
"</target>";

static const char m6801_core_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">"
"<feature name=\"org.gnu.gdb.m6801.core\">"
  "<flags id=\"cc_flags\" size=\"1\">"
    "<field name=\"C\" start=\"0\" end=\"0\"/>"
    "<field name=\"V\" start=\"1\" end=\"1\"/>"
    "<field name=\"Z\" start=\"2\" end=\"2\"/>"
    "<field name=\"N\" start=\"3\" end=\"3\"/>"
    "<field name=\"I\" start=\"4\" end=\"4\"/>"
    "<field name=\"H\" start=\"5\" end=\"5\"/>"
  "</flags>"
  "<reg name=\"cc\" bitsize=\"8\" type=\"cc_flags\" regnum=\"0\"/>"
  "<reg name=\"a\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"b\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"x\" bitsize=\"16\" type=\"uint16\"/>"
  "<reg name=\"sp\" bitsize=\"16\" type=\"uint16\"/>"
  "<reg name=\"pc\" bitsize=\"16\" type=\"code_ptr\"/>"
"</feature>";

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static const char m6809_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
"<target>"
  "<architecture>m6809</architecture>"
  "<xi:include href=\"m6809-core.xml\"/>"
"</target>";

static const char h6309_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
"<target>"
  "<architecture>h6309</architecture>"
  "<xi:include href=\"m6809-core.xml\"/>"
  "<xi:include href=\"m6809-h6309.xml\"/>"
"</target>";

static const char m6809_core_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">"
"<feature name=\"org.gnu.gdb.m6809.core\">"
  "<flags id=\"cc_flags\" size=\"1\">"
    "<field name=\"C\" start=\"0\" end=\"0\"/>"
    "<field name=\"V\" start=\"1\" end=\"1\"/>"
    "<field name=\"Z\" start=\"2\" end=\"2\"/>"
    "<field name=\"N\" start=\"3\" end=\"3\"/>"
    "<field name=\"I\" start=\"4\" end=\"4\"/>"
    "<field name=\"H\" start=\"5\" end=\"5\"/>"
    "<field name=\"F\" start=\"6\" end=\"6\"/>"
    "<field name=\"E\" start=\"7\" end=\"7\"/>"
  "</flags>"
  "<reg name=\"cc\" bitsize=\"8\" type=\"cc_flags\" regnum=\"0\"/>"
  "<reg name=\"a\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"b\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"dp\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"x\" bitsize=\"16\" type=\"uint16\"/>"
  "<reg name=\"y\" bitsize=\"16\" type=\"uint16\"/>"
  "<reg name=\"u\" bitsize=\"16\" type=\"uint16\"/>"
  "<reg name=\"s\" bitsize=\"16\" type=\"uint16\"/>"
  "<reg name=\"pc\" bitsize=\"16\" type=\"code_ptr\"/>"
"</feature>";

static const char m6809_h6309_xml[] =
"<?xml version=\"1.0\"?>"
"<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">"
"<feature name=\"org.gnu.gdb.m6809.h6309\">"
  "<flags id=\"md_flags\" size=\"1\">"
    "<field name=\"NM\" start=\"0\" end=\"0\"/>"
    "<field name=\"FM\" start=\"1\" end=\"1\"/>"
    "<field name=\"IL\" start=\"6\" end=\"6\"/>"
    "<field name=\"D0\" start=\"7\" end=\"7\"/>"
  "</flags>"
  "<reg name=\"md\" bitsize=\"8\" type=\"md_flags\" regnum=\"9\"/>"
  "<reg name=\"e\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"f\" bitsize=\"8\" type=\"uint8\"/>"
  "<reg name=\"v\" bitsize=\"16\" type=\"uint16\"/>"
"</feature>";

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct gdb_annex gdb_annex_list[] = {
	{ "m6801.xml", m6801_xml, sizeof(m6801_xml) - 1 },
	{ "m6803.xml", m6803_xml, sizeof(m6803_xml) - 1 },
	{ "m6801-core.xml", m6801_core_xml, sizeof(m6801_core_xml) - 1 },

	{ "m6809.xml", m6809_xml, sizeof(m6809_xml) - 1 },
	{ "h6309.xml", h6309_xml, sizeof(h6309_xml) - 1 },
	{ "m6809-core.xml", m6809_core_xml, sizeof(m6809_core_xml) - 1 },
	{ "m6809-h6309.xml", m6809_h6309_xml, sizeof(m6809_h6309_xml) - 1 },
};

size_t num_gdb_annex = ARRAY_N_ELEMENTS(gdb_annex_list);
