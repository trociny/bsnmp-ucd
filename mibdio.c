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

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/resource.h>

#include <devstat.h>
#include <math.h>
#include <paths.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "snmp_ucd.h"

/*
 * mibdio (DiskIO) structures and functions.
 */

struct mibdio {
	TAILQ_ENTRY(mibdio)	link;
	int32_t			index;
	u_char			device[UCDMAXLEN];
	int32_t			nRead;
	int32_t			nWritten;
	int32_t			reads;
	int32_t			writes;
	double			la1;
	double			la5;
	double			la15;
	uint64_t		nReadX;
	uint64_t		nWrittenX;
	struct bintime		_busy_time;
};

TAILQ_HEAD(mibdio_list, mibdio);

static struct mibdio_list mibdio_list = TAILQ_HEAD_INITIALIZER(mibdio_list);

static int version_ok;			/* Userland and kernel match. */
static int ondevs;			/* Old number of devices. */
static uint64_t last_dio_update;	/* Ticks of the last disk data update. */
static double exp1, exp5, exp15;	/* DiskIOLA exponents. */

static void update_dio_data(void*);

static struct mibdio *
find_dio (int32_t idx)
{
	struct mibdio *diop;

	TAILQ_FOREACH(diop, &mibdio_list, link) {
		if (diop->index == idx)
			break;
	}

	return (diop);
}

/*
 * Free mibdio list.
 */
static void
mibdio_free (void)
{
	struct mibdio *diop;

	while ((diop = TAILQ_FIRST(&mibdio_list)) != NULL) {
		TAILQ_REMOVE (&mibdio_list, diop, link);
		free (diop);
	}
}

void
update_dio_data(void *arg __unused)
{
	struct statinfo stats;
	struct devinfo dinfo;
	struct mibdio *diop;
	struct devstat dev;
	double interval, busy_time, percent;
	uint64_t now;
	int i, res, ndevs;

	if (!version_ok)
		return;

	memset(&stats, 0, sizeof(stats));
	memset(&dinfo, 0, sizeof(dinfo));
	stats.dinfo = &dinfo;

	res = devstat_getdevs(NULL, &stats);

	if (res == -1) {
		syslog(LOG_ERR, "devstat_getdevs failed: %s: %m", __func__);
		return;
	}

	ndevs = stats.dinfo->numdevs;

	if (ndevs != ondevs) {
		/*
		 * Number of devices has changed. Realloc mibdio.
		 */
		mibdio_free();

		for(i = 0; i < ndevs; i++) {
			diop = malloc(sizeof(*diop));
			if (diop == NULL) {
				syslog(LOG_ERR, "failed to malloc: %s: %m",
				    __func__);
				return;
			}
			memset(diop, 0, sizeof(*diop));
			diop->index = i + 1;
			INSERT_OBJECT_INT(diop, &mibdio_list);
		}
		ondevs = ndevs;
	}

	now = get_ticks();
	interval = (double)(now - last_dio_update) / 100;
	last_dio_update = now;

	exp1 = exp(-interval / 60);
	exp5 = exp(-interval / 300);
	exp15 = exp(-interval / 900);

	/*
	 * Fill mibdio list with devstat data.
	 */
	for(i = 0; i < ndevs; i++) {
		dev = stats.dinfo->devices[i];
		diop = find_dio(i+1);
		snprintf((char *) diop->device, sizeof(diop->device), "%s%d",
			 dev.device_name, dev.unit_number);
		diop->nRead = (int32_t)dev.bytes[DEVSTAT_READ];
		diop->nWritten = (int32_t)dev.bytes[DEVSTAT_WRITE];
		diop->reads = (int32_t)dev.operations[DEVSTAT_READ];
		diop->writes = (int32_t)dev.operations[DEVSTAT_WRITE];
		diop->nReadX = dev.bytes[DEVSTAT_READ];
		diop->nWrittenX = dev.bytes[DEVSTAT_WRITE];
		if (diop->_busy_time.sec > 0) {
			busy_time = dev.busy_time.sec - diop->_busy_time.sec +
			    (double)(dev.busy_time.frac - diop->_busy_time.frac)
			    / (double)0xffffffffffffffffull;
			if (busy_time < 0) /* FP loss of precision near zero. */
				busy_time = 0;
			percent = busy_time * 100 / interval;
			diop->la1 = diop->la1 * exp1 + percent * (1. - exp1);
			diop->la5 = diop->la5 * exp5 + percent * (1. - exp5);
			diop->la15 = diop->la15 * exp15 + percent * (1. - exp15);
		}
		diop->_busy_time = dev.busy_time;
	}

	/*
	 * Free memory allocated by devstat_getdevs().
	 */
	free(stats.dinfo->mem_ptr);
	stats.dinfo->mem_ptr = NULL;

	return;
}

int
op_diskIOTable(struct snmp_context *context __unused, struct snmp_value *value,
	u_int sub, u_int iidx __unused, enum snmp_op op)
{
	struct mibdio *diop;
	asn_subid_t which;
	int ret;

	which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GETNEXT:
		diop = NEXT_OBJECT_INT(&mibdio_list, &value->var, sub);
		if (diop == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = diop->index;
		break;

	case SNMP_OP_GET:
		if (value->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		diop = find_dio(value->var.subs[sub]);
		if (diop == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);

	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);

	default:
		return (SNMP_ERR_RES_UNAVAIL);
	}

	ret = SNMP_ERR_NOERROR;

	switch (which) {
	case LEAF_diskIOIndex:
		value->v.integer = diop->index;
		break;

	case LEAF_diskIODevice:
		ret = string_get(value, diop->device, -1);
		break;

	case LEAF_diskIONRead:
		value->v.integer = diop->nRead;
		break;

	case LEAF_diskIONWritten:
		value->v.uint32 = diop->nWritten;
		break;

	case LEAF_diskIOReads:
		value->v.uint32 = diop->reads;
		break;

	case LEAF_diskIOWrites:
		value->v.uint32 = diop->writes;
		break;

	case LEAF_diskIOLA1:
		value->v.uint32 = diop->la1;
		break;

	case LEAF_diskIOLA5:
		value->v.uint32 = diop->la5;
		break;

	case LEAF_diskIOLA15:
		value->v.uint32 = diop->la15;
		break;

	case LEAF_diskIONReadX:
		value->v.counter64 = diop->nReadX;
		break;

	case LEAF_diskIONWrittenX:
		value->v.counter64 = diop->nWrittenX;
		break;

	default:
		ret = SNMP_ERR_RES_UNAVAIL;
		break;
	}

	return (ret);
};

void
mibdio_fini(void)
{

	mibdio_free();
}

void
mibdio_init(void)
{
	if ( devstat_checkversion(NULL) == -1) {
		syslog(LOG_ERR,
		    "userland and kernel devstat version mismatch: %s",
		    __func__);
		version_ok = 0;
	} else {
		version_ok = 1;
	}

	update_dio_data(NULL);

	register_update_interval_timer(update_dio_data);
}
