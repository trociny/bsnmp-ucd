/*
 * Copyright (c) 2007 Mikolaj Golub
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: snmp_ucd.c,v 1.9 2008/01/30 21:16:34 mikolaj Exp $
 *
 */

#include "snmp_ucd.h"

/* our module handle */
static struct lmodule *module;

/* OIDs */
static const struct asn_oid oid_ucdavis = OIDX_ucdavis;

/* the Object Resource registration index */
static u_int ucdavis_index = 0;

/* timer id */
static void	*timer_ss, *timer_ext, *timer_fix;

/* the initialisation function */
static int
ucd_init (struct lmodule *mod, int argc __unused, char *argv[] __unused)
{
	module = mod;

	if (init_mibla_list() != 0)
		return (-1);

	if (init_mibmemory() != 0)
		return (-1);

	if (init_mibss() != 0)
		return (-1);

	get_ss_data(NULL);
	timer_ss = timer_start_repeat(UPDATE_INTERVAL, UPDATE_INTERVAL,
				get_ss_data, NULL, mod);

	timer_ext = timer_start_repeat(EXT_CHECK_INTERVAL, EXT_CHECK_INTERVAL,
				run_extCommands, NULL, mod);

	timer_fix = timer_start_repeat(EXT_CHECK_INTERVAL, EXT_CHECK_INTERVAL,
				run_extFixCmds, NULL, mod);

	if (init_mibversion() != 0)
		return (-1);

	return (0);
}

/* Module is started */
static void
ucd_start (void)
{
	ucdavis_index = or_register (&oid_ucdavis, "The MIB module for UCD-SNMP-MIB.", module);
}

/* Called, when the module is to be unloaded after it was successfully loaded */
static int
ucd_fini (void)
{
	timer_stop(timer_ss);
	timer_stop(timer_ext);
	timer_stop(timer_fix);
	mibext_fini();
	mibdio_fini();
	or_unregister(ucdavis_index);
	return (0);
}

const struct snmp_module config = {
    .comment =      "This module implements main parts of UCD-SNMP-MIB.",
    .init      = ucd_init,
    .start     = ucd_start,
    .fini      = ucd_fini,
    .tree      = ucd_ctree,
    .tree_size = ucd_CTREE_SIZE,
};
