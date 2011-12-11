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

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "snmp_ucd.h"

/*
 * mibla structures and functions
 */

struct mibla {
	int32_t		index;
	const u_char	*name;
	u_char		load[UCDMAXLEN];
	u_char		*config;
	int32_t		loadInt;
	int32_t		errorFlag;
	u_char		*errMessage;
};

static struct mibla mibla[3];

static uint64_t last_la_update;	/* ticks of the last la data update */

static const u_char *la_names[] = {(const u_char *) "Load-1", (const u_char *) "Load-5", (const u_char *) "Load-15"};

void
mibla_init()
{
	int i;
	double sys_la[3];

	if (getloadavg(sys_la, 3) != 3)
		syslog(LOG_ERR, "getloadavg failed: %s: %m", __func__);

	for (i=0; i < 3; i++) {
		mibla[i].index = i + 1;
		mibla[i].name = la_names[i];
		snprintf ((char *) mibla[i].load, sizeof(mibla[i].load), "%.2f", sys_la[i]);
		mibla[i].config = (u_char *) strdup(LACONFIG);
		mibla[i].loadInt = (int) (100 * sys_la[i]);
		mibla[i].errorFlag = 0;
		mibla[i].errMessage = NULL;
	}
	last_la_update = get_ticks();
}

static void
update_la_data(void)
{
	double sys_la[3];

	/* update data only once in UPDATE_INTERVAL */
	if ((get_ticks() - last_la_update) > UPDATE_INTERVAL) {
		int i;

		if (getloadavg(sys_la, 3) != 3)
			syslog(LOG_ERR, "getloadavg failed: %s: %m", __func__);

		for (i = 0; i < 3; i++) {
			float crit;
			snprintf ((char *) mibla[i].load, sizeof(mibla[i].load),
			    "%.2f", sys_la[i]);
			mibla[i].loadInt = (int) (100 * sys_la[i]);
			crit = strtof((char *) mibla[i].config, NULL);
			mibla[i].errorFlag = ((crit > 0) && (sys_la[i] >= crit));
		}
		last_la_update = get_ticks();
	}
}

int
op_laTable(struct snmp_context * context __unused, struct snmp_value * value,
	u_int sub, u_int iidx __unused, enum snmp_op op)
{
	int ret, i = 0;
	asn_subid_t which = value->var.subs[sub - 1];

	switch (op) {
		case SNMP_OP_GETNEXT:
			i = value->var.subs[sub]++;
			if (i > 2)
				return (SNMP_ERR_NOSUCHNAME);
			value->var.len = sub + 1;
			break;

		case SNMP_OP_GET:
			if (value->var.len - sub != 1)
				return (SNMP_ERR_NOSUCHNAME);
			if ((i = value->var.subs[sub] - 1) > 2)
				return (SNMP_ERR_NOSUCHNAME);
			break;

		case SNMP_OP_SET:
			if (value->var.len - sub != 1)
				return (SNMP_ERR_NOSUCHNAME);
			if ((i = value->var.subs[sub] - 1) > 2)
				return (SNMP_ERR_NOSUCHNAME);
			switch(which) {
				case LEAF_laConfig:
					return (string_save(value, context, -1, &mibla[i].config));
				case LEAF_laErrMessage:
					return (string_save(value, context, -1, &mibla[i].errMessage));
				default:
					break;
			}
			return (SNMP_ERR_NOT_WRITEABLE);

		case SNMP_OP_ROLLBACK:
		case SNMP_OP_COMMIT:
			return (SNMP_ERR_NOERROR);

		default:
			return (SNMP_ERR_RES_UNAVAIL);
	}

	update_la_data();

	ret = SNMP_ERR_NOERROR;

	switch (which) {
		case LEAF_laIndex:
			value->v.integer = mibla[i].index;
			break;

		case LEAF_laNames:
			ret = string_get(value, mibla[i].name, -1);
			break;

		case LEAF_laLoad:
			ret = string_get(value, mibla[i].load, -1);
			break;

		case LEAF_laConfig:
			ret = string_get(value, mibla[i].config, -1);
			break;

		case LEAF_laLoadInt:
			value->v.integer = mibla[i].loadInt;
			break;

		case LEAF_laErrorFlag:
			value->v.integer = mibla[i].errorFlag;
			break;

		case LEAF_laErrMessage:
			ret = string_get(value, mibla[i].errMessage, -1);
			break;

		default:
			ret = SNMP_ERR_RES_UNAVAIL;
			break;
	}

	return (ret);
};
