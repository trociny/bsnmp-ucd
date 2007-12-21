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
 * $Id: mibversion.c,v 1.1.1.1 2007/12/15 20:22:44 mikolaj Exp $
 *
 */

#include <assert.h>
#include <syslog.h>
#include <sys/time.h>
#include <time.h>

#include "snmp_ucd.h"

/*
 * mibversion structures and functions
 */

struct mibversion {
	uint32_t	index;		/* always 0 */
	char const	*tag;
	char const	*date;
	char		*cDate;
	char const	*ident;
	char const	*configureOptions;
};

static struct mibversion mibver;

/* fill our version info */

int
init_mibversion() {

	mibver.index = 0;
	mibver.tag = "$Name:  $";
	mibver.date = "$Date: 2007/12/15 20:22:44 $";
	mibver.ident = "$Id: mibversion.c,v 1.1.1.1 2007/12/15 20:22:44 mikolaj Exp $";
	mibver.configureOptions = "";

	return (0);
}

static void
set_cDate(void){
	struct timeval	tv;
	gettimeofday(&tv, NULL);
	mibver.cDate = ctime((time_t *) &tv.tv_sec);
}

int
op_version(struct snmp_context * context __unused, struct snmp_value * value, 
	u_int sub, u_int iidx __unused, enum snmp_op op)
{
	int ret;
	asn_subid_t which = value->var.subs[sub - 1];

	switch (op) {

		case SNMP_OP_GET:
			break;

		case SNMP_OP_SET:
			return SNMP_ERR_NOT_WRITEABLE;
    
		case SNMP_OP_GETNEXT:
		case SNMP_OP_ROLLBACK:
		case SNMP_OP_COMMIT:
			return SNMP_ERR_NOERROR;
    
		default:
			assert(0);
			break;
	}
  	
	ret = SNMP_ERR_NOERROR;

	switch (which) {
		
		case LEAF_memIndex:
			value->v.integer = mibver.index;
			break;
		case LEAF_versionTag:
			ret = string_get(value, mibver.tag, -1);
			break;
		case LEAF_versionDate:
			ret = string_get(value, mibver.date, -1);
			break;
		case LEAF_versionCDate:
			set_cDate();
			ret = string_get(value, mibver.cDate, -1);
			break;
		case LEAF_versionIdent:
			ret = string_get(value, mibver.ident, -1);
			break;
		case LEAF_versionConfigureOptions:
			ret = string_get(value, mibver.configureOptions, -1);
			break;
		default:
			assert(0);
			break;
	}

	return (ret);
};

