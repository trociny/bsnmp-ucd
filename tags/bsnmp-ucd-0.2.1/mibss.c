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
 * $Id: mibss.c,v 1.5.2.2 2008/05/11 12:10:28 mikolaj Exp $
 *
 */

#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <paths.h>

#include "snmp_ucd.h"

/*
 * mibmemory structures and functions
 */

struct mibss {
	int32_t		index;		/* is always 1 */
	const u_char 	*errorName;	/* is always "systemStats" */
	int32_t		swapIn;
	int32_t		swapOut;
	int32_t		sysInterrupts;
	int32_t		sysContext;
	int32_t		cpuUser;
	int32_t		cpuSystem;
	int32_t		cpuIdle;
	uint32_t	cpuRawUser;
	uint32_t	cpuRawNice;
	uint32_t	cpuRawSystem;
	uint32_t	cpuRawIdle;
	uint32_t	cpuRawWait;
	uint32_t	cpuRawKernel;
	uint32_t	cpuRawInterrupt;
	uint32_t	rawInterrupts;
	uint32_t	rawContexts;
	uint32_t	cpuRawSoftIRQ;
	uint32_t	rawSwapIn;
	uint32_t	rawSwapOut;
};

static struct mibss mibss;

static int pagesize;	/* initialized in init_mibss() */

#define pagetok(size) ((size) * (pagesize >> 10))

/*
 *  (This has been stolen from BSD top utility)
 *
 *  percentages(cnt, out, new, old, diffs) - calculate percentage change
 *	between array "old" and "new", putting the percentages i "out".
 *	"cnt" is size of each array and "diffs" is used for scratch space.
 *	The array "old" is updated on each call.
 *	The routine assumes modulo arithmetic.  This function is especially
 *	useful on BSD mchines for calculating cpu state percentages.
 */

static long 
percentages(int cnt, int *out, register long *new, register long *old, long *diffs)
{
    register int i;
    register long change;
    register long total_change;
    register long *dp;
    long half_total;

    /* initialization */
    total_change = 0;
    dp = diffs;

    /* calculate changes for each state and the overall change */
    for (i = 0; i < cnt; i++)
    {
	if ((change = *new - *old) < 0)
	{
	    /* this only happens when the counter wraps */
	    change = (int)
		((unsigned long)*new-(unsigned long)*old);
	}
	total_change += (*dp++ = change);
	*old++ = *new++;
    }

    /* avoid divide by zero potential */
    if (total_change == 0)
    {
	total_change = 1;
    }

    /* calculate percentages based on overall change, rounding up */
    half_total = total_change / 2l;

    /* Do not divide by 0. Causes Floating point exception */
    if(total_change) {
        for (i = 0; i < cnt; i++)
        {
          *out++ = (int)((*diffs++ * 1000 + half_total) / total_change);
        }
    }

    /* return the total in case the caller wants to use it */
    return(total_change);
}

void
get_ss_data(void* arg  __unused)
{
	static int32_t oswappgsin = -1;
	static int32_t oswappgsout = -1;
	static int32_t ointr = -1;
	static int32_t oswtch = -1;
	static uint64_t last_update = 0;
	uint64_t current;

	/* for cpu stats */
	static int	cpu_states[CPUSTATES];
	static long	cp_time[CPUSTATES];
	static long	cp_old[CPUSTATES];
	static long	cp_diff[CPUSTATES];
	size_t		cp_time_size = sizeof(cp_time);

	u_long		val;

	sysctlval("vm.stats.vm.v_swappgsin", &val);
	mibss.rawSwapIn = (uint32_t) val;
	sysctlval("vm.stats.vm.v_swappgsout", &val);
	mibss.rawSwapOut = (uint32_t) val;
	sysctlval("vm.stats.sys.v_intr", &val);
	mibss.rawInterrupts = (uint32_t) val;
	sysctlval("vm.stats.sys.v_swtch", &val);
	mibss.rawContexts = (uint32_t) val;
	sysctlval("vm.stats.sys.v_soft", &val);
	mibss.cpuRawSoftIRQ = (uint32_t) val;

	current = get_ticks();
	if (current > last_update) {
		if (oswappgsin < 0) { /* first interval */
			mibss.swapIn  = 0;
			mibss.swapOut = 0;
			mibss.sysInterrupts = 0;
			mibss.sysContext = 0;
		} else {
			mibss.swapIn = pagetok(((mibss.rawSwapIn - oswappgsin))) / (current-last_update);
			mibss.swapOut = pagetok(((mibss.rawSwapOut - oswappgsout))) / (current-last_update);
			mibss.sysInterrupts = (mibss.rawInterrupts - ointr) / (current-last_update);
			mibss.sysContext = (mibss.rawContexts - oswtch) / (current-last_update);
		}
		oswappgsin  = mibss.rawSwapIn;
		oswappgsout = mibss.rawSwapOut;
		ointr = mibss.rawInterrupts;
		oswtch = mibss.rawContexts;

		last_update = current;
	}

	if (sysctlbyname("kern.cp_time", &cp_time, &cp_time_size, NULL, 0) < 0)
		syslog(LOG_ERR, "sysctl failed: %s: %m", __func__);

	/* convert cp_time counts to percentages * 10 */
	percentages(CPUSTATES, cpu_states, cp_time, cp_old, cp_diff);

	mibss.cpuUser = cpu_states[CP_USER] / 10;
	mibss.cpuSystem = (cpu_states[CP_SYS] + cpu_states[CP_INTR]) / 10;
	mibss.cpuIdle = cpu_states[CP_IDLE] / 10;
	mibss.cpuRawUser = cp_time[CP_USER];
	mibss.cpuRawNice = cp_time[CP_NICE];
	mibss.cpuRawSystem = cp_time[CP_SYS] + cp_time[CP_INTR];
	mibss.cpuRawIdle = cp_time[CP_IDLE];
	mibss.cpuRawKernel = cp_time[CP_SYS];
	mibss.cpuRawInterrupt = cp_time[CP_INTR];
}

/* init all our ss objects */

void
mibss_init() {

	pagesize = getpagesize();

	mibss.index = 1;
	mibss.errorName = (const u_char *) "systemStats";
}

int
op_systemStats(struct snmp_context *context __unused, struct snmp_value *value, 
	u_int sub, u_int iidx __unused, enum snmp_op op)
{
	int ret;
	asn_subid_t which = value->var.subs[sub - 1];

	switch (op) {

		case SNMP_OP_GET:
			break;

		case SNMP_OP_SET:
			return (SNMP_ERR_NOT_WRITEABLE);
    
		case SNMP_OP_GETNEXT:
		case SNMP_OP_ROLLBACK:
		case SNMP_OP_COMMIT:
			return (SNMP_ERR_NOERROR);
    
		default:
			return (SNMP_ERR_RES_UNAVAIL);
	}
  	
	ret = SNMP_ERR_NOERROR;

	switch (which) {
		
		case LEAF_memIndex:
			value->v.integer = mibss.index;
			break;
		case LEAF_ssErrorName:
			ret = string_get(value, mibss.errorName, -1);
			break;
		case LEAF_ssSwapIn:
			value->v.integer = mibss.swapIn;
			break;
		case LEAF_ssSwapOut:
			value->v.integer = mibss.swapOut;
			break;
		case LEAF_ssSysInterrupts:
			value->v.integer = mibss.sysInterrupts;
			break;
		case LEAF_ssSysContext:
			value->v.integer = mibss.sysContext;
			break;
		case LEAF_ssCpuUser:
			value->v.integer = mibss.cpuUser;
			break;
		case LEAF_ssCpuSystem:
			value->v.integer = mibss.cpuSystem;
			break;
		case LEAF_ssCpuIdle:
			value->v.integer = mibss.cpuIdle;
			break;
		case LEAF_ssCpuRawUser:
			value->v.uint32 = mibss.cpuRawUser;
			break;
		case LEAF_ssCpuRawNice:
			value->v.uint32 = mibss.cpuRawNice;
			break;
		case LEAF_ssCpuRawSystem:
			value->v.uint32 = mibss.cpuRawSystem;
			break;
		case LEAF_ssCpuRawIdle:
			value->v.uint32 = mibss.cpuRawIdle;
			break;
		case LEAF_ssCpuRawWait:
			value->v.uint32 = mibss.cpuRawWait;
			break;
		case LEAF_ssCpuRawKernel:
			value->v.uint32 = mibss.cpuRawKernel;
			break;
		case LEAF_ssCpuRawInterrupt:
			value->v.uint32 = mibss.cpuRawInterrupt;
			break;
		case LEAF_ssRawInterrupts:
			value->v.uint32 = mibss.rawInterrupts;
			break;
		case LEAF_ssRawContexts:
			value->v.uint32 = mibss.rawContexts;
			break;
		case LEAF_ssCpuRawSoftIRQ:
			value->v.uint32 = mibss.cpuRawSoftIRQ;
			break;
		case LEAF_ssRawSwapIn:
			value->v.uint32 = mibss.rawSwapIn;
			break;
		case LEAF_ssRawSwapOut:
			value->v.uint32 = mibss.rawSwapOut;
			break;
		default:
			ret = SNMP_ERR_RES_UNAVAIL;
			break;
	}

	return (ret);
};

