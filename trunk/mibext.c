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
 * $Id: $
 *
 */

#include <assert.h>
#include <syslog.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "snmp_ucd.h"

/*
 * mibext structures and functions
 */

struct mibext {
	TAILQ_ENTRY(mibext)	link;
	int32_t			index;
	u_char			*names;
	u_char			*command;
	int32_t			result;
	u_char			output[UCDMAXLEN];
	int32_t			errFix;
	u_char			*errFixCmd;
};

TAILQ_HEAD(mibext_list, mibext);

static struct mibext_list mibext_list = TAILQ_HEAD_INITIALIZER(mibext_list);

static struct mibext *
find_ext (int32_t idx) {
	struct mibext *extp;
	TAILQ_FOREACH(extp, &mibext_list, link)
		if (extp->index == idx)
			return (extp);
	return (NULL);
}

static struct mibext *
first_mibext(void)
{
	return (TAILQ_FIRST(&mibext_list));
}

#if 0 /* not used now */
static struct mibext *
next_mibext(const struct mibext *extp)
{
	return (TAILQ_NEXT(extp, link));
}
#endif

static void
run_extCommand(struct mibext *extp)
{
	FILE *fp;
	char buf[UCDMAXLEN];
	
	if (!extp->command)
		return;

	if ((fp = popen((char*) extp->command, "r")) == NULL) {
		syslog(LOG_WARNING, "%s: %m", __func__);
		extp->result = errno;
		return;
	}

	/* read first line to output buffer*/
	fgets((char*) extp->output, UCDMAXLEN-1, fp);

	/* just skip other lines */
	while (fgets(buf, UCDMAXLEN-1, fp) != NULL);
	
	extp->result = pclose(fp);
}

static void
run_extCommands(void)
{
	struct mibext *extp;
	TAILQ_FOREACH(extp, &mibext_list, link)
		run_extCommand(extp);
}

int
op_extTable(struct snmp_context * context __unused, struct snmp_value * value, 
	u_int sub, u_int iidx __unused, enum snmp_op op)
{
	int ret;
	struct mibext *extp = NULL;
	asn_subid_t which = value->var.subs[sub - 1];

	switch (op) {

		case SNMP_OP_GETNEXT:
			if ((extp = NEXT_OBJECT_INT(&mibext_list, &value->var, sub)) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			value->var.len = sub + 1;
			value->var.subs[sub] = extp->index;
			break;

		case SNMP_OP_GET:
			if (value->var.len - sub != 1)
				return (SNMP_ERR_NOSUCHNAME);
			if ((extp = find_ext(value->var.subs[sub])) == NULL)
				return (SNMP_ERR_NOSUCHNAME);
			break;

		case SNMP_OP_SET:
			switch (which) {
				case LEAF_extNames:
					if ((extp = find_ext(value->var.subs[sub])) == NULL) {
						if ((extp = malloc(sizeof(*extp))) == NULL) {
							syslog(LOG_WARNING, "%s: %m", __func__);
							return (SNMP_ERR_NOT_WRITEABLE);
						}
						memset(extp, 0, sizeof(*extp));
						extp->index = value->var.subs[sub];
						INSERT_OBJECT_INT(extp, &mibext_list);
					}
					return  string_save(value, context, -1, &extp->names);

				case LEAF_extCommand:
					if ((extp = find_ext(value->var.subs[sub])) == NULL)
						return (SNMP_ERR_NOT_WRITEABLE);
					return  string_save(value, context, -1, &extp->command);

				default:
					break;
			}
			return (SNMP_ERR_NOT_WRITEABLE);
    
		case SNMP_OP_ROLLBACK:
		case SNMP_OP_COMMIT:
			return (SNMP_ERR_NOERROR);
    
		default:
			assert(0);
			break;
	}

	run_extCommands();
  	
	ret = SNMP_ERR_NOERROR;

	switch (which) {
		
		case LEAF_extIndex:
			value->v.integer = extp->index;
			break;
    		
		case LEAF_extNames:
			ret = string_get(value, extp->names, -1);
			break;

		case LEAF_extCommand:
			ret = string_get(value, extp->command, -1);
			break;

		case LEAF_extResult:
			value->v.integer = extp->result;
			break;

		case LEAF_extOutput:
			ret = string_get(value, extp->output, -1);
			break;

		case LEAF_extErrFix:
			value->v.integer = extp->errFix;
			break;

		case LEAF_extErrFixCmd:
			ret = string_get(value, extp->errFixCmd, -1);
			break;

		default:
			assert(0);
			break;
	}

	return (ret);
};

void
mibext_free (void)
{
	struct mibext *extp = NULL;
	while ((extp = first_mibext()) != NULL) {
		TAILQ_REMOVE (&mibext_list, extp, link);
		free(extp->names);
		free(extp->command);
		free(extp->errFixCmd);
		free (extp);
	}
}

