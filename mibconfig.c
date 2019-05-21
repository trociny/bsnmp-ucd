/*
 * Copyright (c) 2013 Mikolaj Golub
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

#include <unistd.h>
#include "snmp_ucd.h"

u_int update_interval;
u_int ext_check_interval;
u_int ext_update_interval;
u_int ext_timeout;
int osreldate;

/*
 * Initialize configuration parameters.
 */
void
mibconfig_init(void)
{

	update_interval = 500;
	ext_check_interval = 100;
	ext_update_interval = 3000;
	ext_timeout = 60;
	osreldate = getosreldate();
}

int
op_config(struct snmp_context * context __unused, struct snmp_value * value,
	u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which;

	which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GET:
		switch (which) {
		case LEAF_updateInterval:
			value->v.integer = update_interval;
			break;
		case LEAF_extCheckInterval:
			value->v.integer = ext_check_interval;
			break;
		case LEAF_extUpdateInterval:
			value->v.integer = ext_update_interval;
			break;
		case LEAF_extTimeout:
			value->v.integer = ext_timeout;
			break;
		default:
			return (SNMP_ERR_RES_UNAVAIL);
		}
		return (SNMP_ERR_NOERROR);
	case SNMP_OP_SET:
		switch (which) {
		case LEAF_updateInterval:
			if (value->v.integer < 10 || value->v.integer > 6000)
				return (SNMP_ERR_WRONG_VALUE);
			update_interval = value->v.integer;
			restart_update_interval_timer();
			break;
		case LEAF_extCheckInterval:
			if (value->v.integer < 10)
				return (SNMP_ERR_WRONG_VALUE);
			ext_check_interval = value->v.integer;
			restart_ext_check_interval_timer();
			break;
		case LEAF_extUpdateInterval:
			if (value->v.integer < 10)
				return (SNMP_ERR_WRONG_VALUE);
			ext_update_interval = value->v.integer;
			break;
		case LEAF_extTimeout:
			if (value->v.integer < 0)
				return (SNMP_ERR_WRONG_VALUE);
			ext_timeout = value->v.integer;
			break;
		default:
			return (SNMP_ERR_RES_UNAVAIL);
		}
		return (SNMP_ERR_NOERROR);
	case SNMP_OP_GETNEXT:
	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	default:
		return (SNMP_ERR_RES_UNAVAIL);
	}
};
