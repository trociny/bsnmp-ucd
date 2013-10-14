/*
 * Copyright (c) 2007-2013 Mikolaj Golub
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
 * $Id$
 *
 */

#include <sys/types.h>
#include <sys/queue.h>

#include <stdlib.h>
#include <syslog.h>

#include "snmp_ucd.h"

/* our module handle */
static struct lmodule *module;

/* OIDs */
static const struct asn_oid oid_ucdavis = OIDX_ucdavis;

/* the Object Resource registration index */
static u_int ucdavis_index = 0;

/* timers id */
static void *update_interval_timer, *ext_check_interval_timer;

struct timer_hook {
	void	(*h_func)(void*);
        STAILQ_ENTRY(timer_hook)	h_link;
};

STAILQ_HEAD(timer_hook_list, timer_hook);

static struct timer_hook_list update_interval_timer_hook_list =
    STAILQ_HEAD_INITIALIZER(update_interval_timer_hook_list);
static struct timer_hook_list ext_check_interval_timer_hook_list =
    STAILQ_HEAD_INITIALIZER(ext_check_interval_timer_hook_list);

static void
register_timer(struct timer_hook_list * hooks, void (*hook_f)(void*))
{
	struct timer_hook *hook;

	hook = malloc(sizeof(*hook));
	if (hook == NULL) {
		syslog(LOG_ERR, "failed to malloc: %s: %m", __func__);
		return;
	}
	hook->h_func = hook_f;
	STAILQ_INSERT_TAIL(hooks, hook, h_link);
}

void
register_update_interval_timer(void (*hook_f)(void*))
{

	register_timer(&update_interval_timer_hook_list, hook_f);
}

void
register_ext_check_interval_timer(void (*hook_f)(void*))
{

	register_timer(&update_interval_timer_hook_list, hook_f);
}

static void
run_timer_hooks(void* arg)
{
	struct timer_hook_list *hooks;
	struct timer_hook *hook;

	hooks = (struct timer_hook_list *)arg;

	STAILQ_FOREACH(hook, hooks, h_link)
	    hook->h_func(NULL);
}

void
restart_update_interval_timer(void)
{

	timer_stop(update_interval_timer);
	update_interval_timer = timer_start_repeat(update_interval,
	    update_interval, run_timer_hooks,
	    &update_interval_timer_hook_list, module);
}

void
restart_ext_check_interval_timer(void)
{

	timer_stop(ext_check_interval_timer);
	ext_check_interval_timer = timer_start_repeat(ext_check_interval,
	    ext_check_interval, run_timer_hooks,
	    &ext_check_interval_timer_hook_list, module);
}

/* the initialisation function */
static int
ucd_init(struct lmodule *mod, int argc __unused, char *argv[] __unused)
{
	module = mod;

	mibconfig_init();
	mibla_init();
	mibmemory_init();
	mibss_init();
	mibdisk_init();
	mibdio_init();
	mibext_init();
	mibpr_init();
	mibversion_init();

	update_interval_timer = timer_start_repeat(update_interval,
	    update_interval, run_timer_hooks,
	    &update_interval_timer_hook_list, module);
	ext_check_interval_timer = timer_start_repeat(ext_check_interval,
	    ext_check_interval, run_timer_hooks,
	    &ext_check_interval_timer_hook_list, module);

	return (0);
}

/* Module is started */
static void
ucd_start(void)
{
	ucdavis_index = or_register(&oid_ucdavis,
	    "The MIB module for UCD-SNMP-MIB.", module);
}

/* Called, when the module is to be unloaded after it was successfully loaded */
static int
ucd_fini(void)
{

	timer_stop(update_interval_timer);
	timer_stop(ext_check_interval_timer);
	mibext_fini();
	mibdisk_fini();
	mibdio_fini();
	or_unregister(ucdavis_index);
	return (0);
}

const struct snmp_module config = {
    .comment   = "This module implements main parts of UCD-SNMP-MIB.",
    .init      = ucd_init,
    .start     = ucd_start,
    .fini      = ucd_fini,
    .tree      = ucd_ctree,
    .tree_size = ucd_CTREE_SIZE,
};
