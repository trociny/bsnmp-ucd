/*
 * Copyright (c) 2009-2012 Mikolaj Golub
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
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/queue.h>

#include <devstat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "snmp_ucd.h"

/*
 * mibdisk structures and functions.
 */

struct mibdisk {
	TAILQ_ENTRY(mibdisk)	link;
	int32_t			index;
	u_char			path[MNAMELEN];
	u_char			device[MNAMELEN];
	int32_t			minimum;
	int32_t			minPercent;
	uint64_t		total;
	uint64_t		avail;
	uint64_t		used;
	int32_t			percent;
	int32_t			percentNode;
	int32_t			errorFlag;
};

TAILQ_HEAD(mibdisk_list, mibdisk);

static struct mibdisk_list mibdisk_list = TAILQ_HEAD_INITIALIZER(mibdisk_list);

static int version_ok;			/* Userland and kernel match. */
static int ondevs;			/* Old number of devices. */
static uint64_t last_disk_update;	/* Ticks of the last disk data update. */

static struct mibdisk *
find_disk(int32_t idx)
{
	struct mibdisk *dp;

	TAILQ_FOREACH(dp, &mibdisk_list, link)
		if (dp->index == idx)
			return (dp);
	return (NULL);
}

/*
 * Free mibdisk list starting from the specified index.
 */
static void
mibdisk_free(int32_t idx)
{
	struct mibdisk *dp, *next;

	dp = find_disk(idx);

	while (dp != NULL) {
		next = TAILQ_NEXT(dp, link);
		TAILQ_REMOVE(&mibdisk_list, dp, link);
		free(dp);
		dp = next;
	}
}

static int
update_disk_data(void)
{
	struct statfs *mntbuf;
	struct statinfo stats;
	struct devinfo dinfo;
	struct mibdisk *dp;
	size_t mntsize;
	int64_t used, availblks;
	int i, res, ndevs;

	if (!version_ok)
		return (-1);

	if ((get_ticks() - last_disk_update) < UPDATE_INTERVAL)
		return (0);

	last_disk_update = get_ticks();

	mntsize = getmntinfo(&mntbuf, MNT_NOWAIT);

	memset(&stats, 0, sizeof(stats));
	memset(&dinfo, 0, sizeof(dinfo));
	stats.dinfo = &dinfo;

	res = devstat_getdevs(NULL, &stats);

	if (res == -1) {
		syslog(LOG_ERR, "devstat_getdevs failed: %s: %m", __func__);
		return (-1);
	}

	ndevs = mntsize;

	/*
	 * Realloc mibdio if number of devices has changed.
	 */
	if (ndevs < ondevs) {
		mibdisk_free(ndevs);
	} else if (ndevs > ondevs) {
		for(i = ondevs; i < ndevs; i++) {
			dp = malloc(sizeof(*dp));
			if (dp == NULL) {
				syslog(LOG_ERR, "failed to malloc: %s: %m",
				    __func__);
				return (-1);
			}

			memset(dp, 0, sizeof(*dp));
			dp->index = i + 1;
			dp->minimum = -1;
			dp->minPercent = -1;
			INSERT_OBJECT_INT(dp, &mibdisk_list);
		}
	}
	ondevs = ndevs;

	/*
	 * Fill mibdisk list with devstat data.
	 */
	for(i = 0; i < ndevs; i++) {

		dp = find_disk(i+1);
		strncpy((char*)dp->path, mntbuf[i].f_mntonname,
		    sizeof(dp->path) - 1);
		strncpy((char*)dp->device, mntbuf[i].f_mntfromname,
		    sizeof(dp->device) - 1);
		dp->total = mntbuf[i].f_blocks * mntbuf[i].f_bsize / 1024;
		dp->avail = mntbuf[i].f_bavail * mntbuf[i].f_bsize / 1024;
		used = mntbuf[i].f_blocks - mntbuf[i].f_bfree;
		dp->used = used * mntbuf[i].f_bsize / 1024;
		availblks = mntbuf[i].f_bavail + used;
		dp->percent = (int)(availblks == 0 ? 100.0 : (double)used /
		    (double)availblks * 100.0 + 0.5);
		dp->percentNode = (int)(mntbuf[i].f_files == 0 ? 100 :
		    (double)(mntbuf[i].f_files - mntbuf[i].f_ffree) /
		    (double)mntbuf[i].f_files * 100 + 0.5);
		dp->errorFlag = (dp->minimum >= 0 ?
		    dp->avail < (uint64_t)dp->minimum :
		    100 - dp->percent <= dp->minPercent) ? 1 : 0;
	}

	/*
	 * Free memory allocated by devstat_getdevs().
	 */
	free(stats.dinfo->mem_ptr);
	stats.dinfo->mem_ptr = NULL;

	return (0);
}

int
op_dskTable(struct snmp_context *context __unused, struct snmp_value *value,
	u_int sub, u_int iidx __unused, enum snmp_op op)
{
	struct mibdisk *dp;
	asn_subid_t which;
	int ret;
	u_char buf[UCDMAXLEN];

	if (update_disk_data() == -1)
		return (SNMP_ERR_RES_UNAVAIL);

	which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GETNEXT:
		if ((dp = NEXT_OBJECT_INT(&mibdisk_list, &value->var,
		    sub)) == NULL) {
			return (SNMP_ERR_NOSUCHNAME);
		}
		value->var.len = sub + 1;
		value->var.subs[sub] = dp->index;
		break;

	case SNMP_OP_GET:
		if (value->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		if ((dp = find_disk(value->var.subs[sub])) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_SET:
		syslog(LOG_INFO, "%s: SNMP_OP_SET", __func__);
		if ((dp = find_disk(value->var.subs[sub])) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		switch (which) {
		case LEAF_dskMinimum:
			dp->minimum = value->v.integer;
			return (SNMP_ERR_NOERROR);
		case LEAF_dskMinPercent:
			dp->minPercent = value->v.integer;
			return (SNMP_ERR_NOERROR);
		}
		return (SNMP_ERR_NOT_WRITEABLE);

	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);

	default:
		return (SNMP_ERR_RES_UNAVAIL);
	}

	ret = SNMP_ERR_NOERROR;

	switch (which) {
	case LEAF_dskIndex:
		value->v.integer = dp->index;
		break;

	case LEAF_dskPath:
		ret = string_get(value, dp->path, -1);
		break;

	case LEAF_dskDevice:
		ret = string_get(value, dp->device, -1);
		break;

	case LEAF_dskMinimum:
		value->v.integer = dp->minimum;
		break;

	case LEAF_dskMinPercent:
		value->v.integer = dp->minPercent;
		break;

	case LEAF_dskTotal:
		if (dp->total > INT32_MAX)
			value->v.integer = INT32_MAX;
		else
			value->v.integer = dp->total;
		break;

	case LEAF_dskTotalLow:
		value->v.uint32 = dp->total & UINT32_MAX;
		break;

	case LEAF_dskTotalHigh:
		value->v.uint32 = dp->total >> 32;
		break;

	case LEAF_dskAvail:
		if (dp->avail > INT32_MAX)
			value->v.integer = INT32_MAX;
		else
			value->v.integer = dp->avail;
		break;

	case LEAF_dskAvailLow:
		value->v.uint32 = dp->avail & INT32_MAX;
		break;

	case LEAF_dskAvailHigh:
		value->v.uint32 = dp->avail >> 32;
		break;

	case LEAF_dskUsed:
		if (dp->used > INT32_MAX)
			value->v.integer = INT32_MAX;
		else
			value->v.integer = dp->used;
		break;

	case LEAF_dskUsedLow:
		value->v.uint32 = dp->used & INT32_MAX;
		break;

	case LEAF_dskUsedHigh:
		value->v.uint32 = dp->used >> 32;
		break;

	case LEAF_dskPercent:
		value->v.integer = dp->percent;
		break;

	case LEAF_dskPercentNode:
		value->v.integer = dp->percentNode;
		break;

	case LEAF_dskErrorFlag:
		value->v.integer = dp->errorFlag;
		break;

	case LEAF_prErrMessage:
		if (dp->errorFlag) {
			if (dp->minimum >= 0) {
				snprintf((char*)buf, sizeof(buf),
				    "%s: less than %d free (= %ju)", dp->path,
				    dp->minimum, (uintmax_t)dp->avail);
			} else {
				snprintf((char*)buf, sizeof(buf),
				    "%s: less than %d%% free (= %d%%)",
				    dp->path, dp->minPercent, dp->percent);
			}
		} else {
			buf[0] = '\0';
		}
		ret = string_get(value, buf, -1);
		break;

	default:
		ret = SNMP_ERR_RES_UNAVAIL;
		break;
	}

	return (ret);
}

void
mibdisk_fini(void)
{

	mibdisk_free(0);
}

void
mibdisk_init(void)
{

	if ( devstat_checkversion(NULL) == -1) {
		syslog(LOG_ERR,
		    "userland and kernel devstat version mismatch: %s",
		    __func__);
		version_ok = 0;
	} else {
		version_ok = 1;
	}
}
