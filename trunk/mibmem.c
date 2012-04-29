/*
 * Copyright (c) 2007-2012 Mikolaj Golub
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
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#include <vm/vm_param.h>

#include <fcntl.h>
#include <kvm.h>
#include <paths.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "snmp_ucd.h"

/*
 * mibmemory structures and functions.
 */

struct mibmemory {
	int32_t	index;			/* is always 0 */
	u_char const	*errorName;	/* is always "swap" */
	int32_t		totalSwap;
	int32_t		availSwap;
	int32_t		totalReal;
	int32_t		availReal;
	int32_t		totalFree;
	int32_t		minimumSwap;
	int32_t		shared;
	int32_t		buffer;
	int32_t		cached;
	int32_t		swapError;
	u_char		*swapErrorMsg;
};

static struct mibmemory mibmem;

static kvm_t *kd;	/* Initialized in init_memory(). */

static int pagesize;	/* Initialized in init_memory(). */

#define pagetok(size) ((size) * (pagesize >> 10))

static void
swapmode(int *rettotal, int *retavail)
{
	struct kvm_swap swapary[1];

	*rettotal = 0;
	*retavail = 0;

	if (kvm_getswapinfo(kd, swapary, 1, 0) < 0) {
		syslog(LOG_WARNING, "kvm_getswapinfo failed: %s: %m", __func__);
		return;
	}

#define CONVERT(v)	((quad_t)(v) * pagesize / 1024)
	*rettotal = CONVERT(swapary[0].ksw_total);
	*retavail = CONVERT(swapary[0].ksw_total - swapary[0].ksw_used);
#undef  CONVERT
}

/* get memory data and fill mibmem */

static void
get_mem_data(void)
{
	static struct	vmtotal total;
	size_t		total_size;
	u_long		val;

	total_size = sizeof(total);

	if (sysctlbyname("vm.vmtotal", &total, &total_size, NULL, 0) < 0)
		syslog(LOG_ERR, "sysctl filed: %s: %m", __func__);

	swapmode(&mibmem.totalSwap, &mibmem.availSwap);

	mibmem.swapError = mibmem.availSwap <= mibmem.minimumSwap;

	sysctlval("hw.physmem", &val);
	mibmem.totalReal = (int32_t) (val >> 10);
	sysctlval("vm.stats.vm.v_free_count", &val);
	mibmem.availReal = pagetok(val);
	mibmem.totalFree = pagetok(total.t_free);
	sysctlval("vm.stats.vm.v_cache_count", &val);
	mibmem.cached = (int32_t) pagetok(val);
	sysctlval("vfs.bufspace", &val);
	mibmem.buffer = (int32_t) (val / 1024);
	mibmem.shared = pagetok(total.t_vmshr + total.t_avmshr +
				total.t_rmshr + total.t_armshr);
}

static uint64_t last_mem_update;	/* Ticks of the last mem data update. */

/*
 * Init all our memory objects.
 */
void
mibmemory_init()
{

	pagesize = getpagesize();

	kd = kvm_open(NULL, _PATH_DEVNULL, NULL, O_RDONLY, "kvm_open");
	if (kd == NULL)
		syslog(LOG_ERR, "kvm_open failed: %s: %m", __func__);

	mibmem.index = 0;
	mibmem.errorName = (const u_char *)"swap";
	mibmem.minimumSwap = DEFAULTMINIMUMSWAP;
	mibmem.swapErrorMsg = NULL;

	get_mem_data();

	last_mem_update = get_ticks();
}

static void
update_memory_data(void)
{

	/* Update data only once in UPDATE_INTERVAL. */
	if (get_ticks() - last_mem_update > UPDATE_INTERVAL) {
		get_mem_data();
		last_mem_update = get_ticks();
	}
}

int
op_memory(struct snmp_context * context __unused, struct snmp_value * value,
	u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which;
	int ret;

	which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GET:
		break;

	case SNMP_OP_SET:
		switch(which) {
		case LEAF_memMinimumSwap:
			mibmem.minimumSwap = value->v.integer;
			return (SNMP_ERR_NOERROR);
		case LEAF_memSwapErrorMsg:
			ret = string_save(value, context, -1,
			    &mibmem.swapErrorMsg);
			return (ret);
		default:
			break;
		}
		return (SNMP_ERR_NOT_WRITEABLE);

	case SNMP_OP_GETNEXT:
	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);

	default:
		return (SNMP_ERR_RES_UNAVAIL);
		break;
	}

	update_memory_data();

	ret = SNMP_ERR_NOERROR;

	switch (which) {
	case LEAF_memIndex:
		value->v.integer = mibmem.index;
		break;
	case LEAF_memErrorName:
		ret = string_get(value, mibmem.errorName, -1);
		break;
	case LEAF_memTotalSwap:
		value->v.integer = mibmem.totalSwap;
		break;
	case LEAF_memAvailSwap:
		value->v.integer = mibmem.availSwap;
		break;
	case LEAF_memTotalReal:
		value->v.integer = mibmem.totalReal;
		break;
	case LEAF_memAvailReal:
		value->v.integer = mibmem.availReal;
		break;
	case LEAF_memTotalFree:
		value->v.integer = mibmem.totalFree;
		break;
	case LEAF_memMinimumSwap:
		value->v.integer = mibmem.minimumSwap;
		break;
	case LEAF_memShared:
		value->v.integer = mibmem.shared;
		break;
	case LEAF_memBuffer:
		value->v.integer = mibmem.buffer;
		break;
	case LEAF_memCached:
		value->v.integer = mibmem.cached;
		break;
	case LEAF_memSwapError:
		value->v.integer = mibmem.swapError;
		break;
	case LEAF_memSwapErrorMsg:
		ret = string_get(value, mibmem.swapErrorMsg, -1);
		break;
	default:
		ret = SNMP_ERR_RES_UNAVAIL;
		break;
	}

	return (ret);
};
