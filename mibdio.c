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
 * $Id$
 *
 */

#include <syslog.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <paths.h>
#include <sys/resource.h>
#include <devstat.h>

#include "snmp_ucd.h"

/*
 * mibdio (DiskIO) structures and functions
 */

struct mibdio {
	TAILQ_ENTRY(mibdio)	link;
	int32_t			index;
	u_char			device[UCDMAXLEN];
	int32_t			nRead;
	int32_t			nWritten;
	int32_t			reads;
	int32_t			writes;
	uint64_t		nReadX;
	uint64_t		nWrittenX;
};

TAILQ_HEAD(mibdio_list, mibdio);

static struct mibdio_list mibdio_list = TAILQ_HEAD_INITIALIZER(mibdio_list);

static int	version_ok;	/* result of checking the userland devstat version against the kernel */

static int	ondevs;		/* here we store old number of devices */

static uint64_t last_dio_update;	/* ticks of the last dio data update */

static struct mibdio *
find_dio (int32_t idx)
{
	struct mibdio *diop;
	TAILQ_FOREACH(diop, &mibdio_list, link)
		if (diop->index == idx)
			return (diop);
	return (NULL);
}

static struct mibdio *
first_mibdio(void)
{
	return (TAILQ_FIRST(&mibdio_list));
}

/* free mibdio list */
static void
mibdio_free (void)
{
	struct mibdio *diop = NULL;
	while ((diop = first_mibdio()) != NULL) {
		TAILQ_REMOVE (&mibdio_list, diop, link);
		free (diop);
	}
}

static int
update_dio_data(void)
{
	int	i, res, ndevs;
	struct statinfo	stats;
	struct devinfo	dinfo;

	if (!version_ok)
		return -1;

	
	if ((get_ticks() - last_dio_update) < UPDATE_INTERVAL)
		return 1;

	last_dio_update = get_ticks();

	memset(&stats, 0, sizeof(stats));
	memset(&dinfo, 0, sizeof(dinfo));
	stats.dinfo = &dinfo;

	res = devstat_getdevs(NULL, &stats);

	if (res == -1) {
		syslog(LOG_ERR, "devstat_getdevs failed: %s: %m", __func__);
		return -1;
	} 

	ndevs = (stats.dinfo)->numdevs;

	if (ndevs != ondevs) {
	
		/* number of devices has changed. realloc mibdio */
	
		mibdio_free();

		for(i = 0; i < ndevs; i++) {
			struct mibdio	*diop = NULL;

			if ((diop = malloc(sizeof(*diop))) == NULL) {
				syslog(LOG_ERR, "failed to malloc: %s: %m", __func__);
				return -1;
			}

			memset(diop, 0, sizeof(*diop));
			diop->index = i + 1;
			INSERT_OBJECT_INT(diop, &mibdio_list);

		}

		ondevs = ndevs;
	}

	/* fill mibdio list with devstat data */

	for(i = 0; i < ndevs; i++) {
		struct mibdio	*diop = NULL;
		struct devstat	dev = (stats.dinfo)->devices[i];

		diop = find_dio(i+1);
		snprintf((char *) diop->device, sizeof(diop->device), "%s%d",
			 dev.device_name, dev.unit_number);
			
		diop->nRead     = (int32_t) dev.bytes[DEVSTAT_READ];
		diop->nWritten  = (int32_t) dev.bytes[DEVSTAT_WRITE];
		diop->reads     = (int32_t) dev.operations[DEVSTAT_READ];
		diop->writes    = (int32_t) dev.operations[DEVSTAT_WRITE];
		diop->nReadX    = dev.bytes[DEVSTAT_READ];
		diop->nWrittenX = dev.bytes[DEVSTAT_WRITE];
	}

	/* free memory allocated by devstat_getdevs() */
	
	free((stats.dinfo)->mem_ptr);
	(stats.dinfo)->mem_ptr = NULL;
	
	return 0;
}

int
op_diskIOTable(struct snmp_context *context __unused, struct snmp_value *value, 
	u_int sub, u_int iidx __unused, enum snmp_op op)
{
	int ret;
	struct mibdio *diop = NULL;
	asn_subid_t which = value->var.subs[sub - 1];

	update_dio_data();

	switch (op) {

		case SNMP_OP_GETNEXT:
			if ((diop = NEXT_OBJECT_INT(&mibdio_list, &value->var, sub)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			value->var.len = sub + 1;
			value->var.subs[sub] = diop->index;
			break;

		case SNMP_OP_GET:
			if (value->var.len - sub != 1)
				return (SNMP_ERR_NOSUCHNAME);
			if ((diop = find_dio(value->var.subs[sub])) == NULL)
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
mibdio_fini (void)
{
	mibdio_free();
}

void
mibdio_init(void) {
	if ( devstat_checkversion(NULL) == -1) {
		syslog(LOG_ERR, "userland and kernel devstat version mismatch: %s", __func__);
		version_ok = 0;
	} else {
		version_ok = 1;
	}
}
