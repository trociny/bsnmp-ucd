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
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "snmp_ucd.h"

/*
 * mibpr structures and functions.
 */

struct mibpr {
	TAILQ_ENTRY(mibpr)	link;
	int32_t			index;
	u_char			*names;
	int32_t			count;
	int32_t			min;
	int32_t			max;
	int32_t			errFix;
	u_char			*errFixCmd;
	uint64_t		_fix_ticks;
};

struct pr_msg {
	int32_t		result;
	u_char		output[UCDMAXLEN];
};

TAILQ_HEAD(mibpr_list, mibpr);

static struct mibpr_list mibpr_list = TAILQ_HEAD_INITIALIZER(mibpr_list);
static uint64_t _ticks;

static struct mibpr *
find_pr(int32_t idx)
{
	struct mibpr *prp;

	TAILQ_FOREACH(prp, &mibpr_list, link) {
		if (prp->index == idx)
			return (prp);
	}
	return (NULL);
}

static struct mibpr *
first_mibpr(void)
{

	return (TAILQ_FIRST(&mibpr_list));
}

/*
 * Handle timeouts when executing commands.
 */
static void
prcmd_sighandler(int sig __unused)
{

	_exit(127);
}

static void
reset_counters(int32_t val)
{
	struct mibpr *prp;

	TAILQ_FOREACH(prp, &mibpr_list, link)
		prp->count = val;
}

static void
count_proc(const char *name)
{
	struct mibpr *prp;

	TAILQ_FOREACH(prp, &mibpr_list, link) {
		if (!prp->names || prp->names[0] == '\0')
			continue;
		if (!strcmp((const char *)prp->names, name))
			prp->count++;
	}
}

static void
get_procs(kvm_t *kd)
{
	struct kinfo_proc *kp;
	int nentries, i;

	nentries = -1;
	kp = kvm_getprocs(kd, KERN_PROC_PROC, 0, &nentries);
	if ((kp == NULL && nentries > 0) || (kp != NULL && nentries < 0)) {
		syslog(LOG_ERR, "failed to kvm_getprocs(): %s: %m", __func__);
		reset_counters(-1);
		return;
	}
	reset_counters(0);
	if (nentries > 0) {
		for (i = nentries; --i >= 0; ++kp)
			count_proc(kp->ki_comm);
	}
}

/*
 * Count processes.
 */
void
run_prCommands(void* arg __unused)
{
	char errbuf[_POSIX2_LINE_MAX];
	kvm_t *kd;
	uint64_t current;

	current = get_ticks();

	 /* Run counting only if EXT_UPDATE_INTERVAL exceeded. */
	if ((current - _ticks) < EXT_UPDATE_INTERVAL)
		return;

	kd = kvm_openfiles(_PATH_DEVNULL, _PATH_DEVNULL, NULL, O_RDONLY, errbuf);
	if (kd == 0) {
		syslog(LOG_ERR, "failed to kvm_openfiles(): %s: %m", __func__);
		return;
	}
	get_procs(kd);
	if (kvm_close(kd) == -1)
		syslog(LOG_ERR, "failed to kvm_close(): %s: %m", __func__);

	_ticks = get_ticks();
}

/*
 * Run fix commands.
 */
void
run_prFixCmds(void* arg __unused)
{
	struct mibpr *prp;
	uint64_t current;
	pid_t pid, res;
	int status, fd;

	current = get_ticks();

	/* Run commads if needed. */

	TAILQ_FOREACH(prp, &mibpr_list, link) {

		if (!prp->errFix)
			continue; /* No fix. */

		if (!prp->errFixCmd)
			continue; /* No command specified. */

		if ((current - prp->_fix_ticks) < EXT_UPDATE_INTERVAL)
			continue; /* EXT_UPDATE_INTERVAL has not exceeded. */

		/* Execute the command in the child process. */
		pid = fork();

		if (pid == 0) {
			/* Close all descriptors except stdio. */
			for (fd = 3; fd <= getdtablesize(); fd++)
				close(fd);

			/* Fork again. */
			if ((pid = fork()) < 0) {
				syslog(LOG_ERR, "Can't fork: %s: %m", __func__);
				_exit(127);
			}

			if (pid == 0) { /* Grandchild. */
				/* Become process group leader. */
				setpgid(0, 0);

				/* Trap commands that timed out. */
				signal(SIGALRM, prcmd_sighandler);
				alarm(EXT_TIMEOUT);

				/* Run the command. */
				status = system((char*)prp->errFixCmd);
				if (status != 0) {
					syslog(LOG_WARNING,
					    "command `%s' has retuned status %d",
					    prp->errFixCmd, WEXITSTATUS(status));
				}
				_exit(WEXITSTATUS(status));
			}

			/* Parent of grandchild. */
			_exit(0);
		}

		/* Parent. */

		if (pid == -1)
			syslog(LOG_ERR, "Can't fork: %s: %m", __func__);

		/* Wait for child. */
		for (;;) {
			res = waitpid(pid, &status, 0);
			if (res == -1 && errno == EINTR)
				continue;
			if (res == -1)
				syslog(LOG_ERR, "failed to waitpid: %s: %m", __func__);
			break;
		}
		prp->_fix_ticks = get_ticks();
	}
}

int
op_prTable(struct snmp_context * context __unused, struct snmp_value * value,
	u_int sub, u_int iidx __unused, enum snmp_op op)
{
	struct mibpr *prp;
	asn_subid_t which;
	u_char buf[UCDMAXLEN];
	int ret;

	which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GETNEXT:
		prp = NEXT_OBJECT_INT(&mibpr_list, &value->var, sub);
		if (prp == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = prp->index;
		break;

	case SNMP_OP_GET:
		if (value->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		prp = find_pr(value->var.subs[sub]);
		if (prp == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_SET:
		switch (which) {
		case LEAF_prNames:
			prp = find_pr(value->var.subs[sub]);
			if (prp == NULL) {
				prp = malloc(sizeof(*prp));
				if (prp == NULL) {
					syslog(LOG_ERR,
					    "failed to malloc: %s: %m",
					    __func__);
					return (SNMP_ERR_NOT_WRITEABLE);
				}
				memset(prp, 0, sizeof(*prp));
				prp->index = value->var.subs[sub];
				INSERT_OBJECT_INT(prp, &mibpr_list);
			}
			ret = string_save(value, context, -1, &prp->names);
			return (ret);

		case LEAF_prMin:
			prp = find_pr(value->var.subs[sub]);
			if (prp == NULL)
				return (SNMP_ERR_NOT_WRITEABLE);
			prp->min = value->v.integer;
			return SNMP_ERR_NOERROR;

		case LEAF_prMax:
			prp = find_pr(value->var.subs[sub]);
			if (prp == NULL)
				return (SNMP_ERR_NOT_WRITEABLE);
			prp->max = value->v.integer;
			return SNMP_ERR_NOERROR;

		case LEAF_prErrFix:
			prp = find_pr(value->var.subs[sub]);
			if (prp == NULL)
				return (SNMP_ERR_NOT_WRITEABLE);
			prp->errFix = value->v.integer;
			return SNMP_ERR_NOERROR;

		case LEAF_prErrFixCmd:
			prp = find_pr(value->var.subs[sub]);
			if (prp == NULL)
				return (SNMP_ERR_NOT_WRITEABLE);
			ret = string_save(value, context, -1, &prp->errFixCmd);
			return (ret);

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

	ret = SNMP_ERR_NOERROR;

	switch (which) {
	case LEAF_prIndex:
		value->v.integer = prp->index;
		break;

	case LEAF_prNames:
		ret = string_get(value, prp->names, -1);
		break;

	case LEAF_prCount:
		value->v.integer = prp->count;
		break;

	case LEAF_prMin:
		value->v.integer = prp->min;
		break;

	case LEAF_prMax:
		value->v.integer = prp->max;
		break;

	case LEAF_prErrorFlag:
		if (prp->count >= 0 &&
		    ((prp->min != 0 && prp->count < prp->min) ||
		     (prp->max != 0 && prp->count > prp->max) ||
		     (prp->min == 0 && prp->max == 0 && prp->count > 0))) {
			value->v.integer = 1;
		} else {
			value->v.integer = 0;
		}
		break;

	case LEAF_prErrMessage:
		if (prp->count < 0) {
			buf[0] = '\0'; /* Failed to count processes. */
		} else if (prp->min && prp->count < prp->min) {
			snprintf((char*)buf, sizeof(buf),
			    "Too few %s running (# = %d)", prp->names,
			    prp->count);
		} else if (prp->max && prp->count > prp->max) {
			snprintf((char*)buf, sizeof(buf),
			    "Too many %s running (# = %d)", prp->names,
			    prp->count);
		} else if (prp->min == 0 && prp->max == 0 && prp->count < 1) {
			snprintf((char*)buf, sizeof(buf),
			    "%s process should not be running.", prp->names);
		} else {
			buf[0] = '\0';
		}
		ret = string_get(value, buf, -1);
		break;

	case LEAF_prErrFix:
		value->v.integer = prp->errFix;
		break;

	case LEAF_prErrFixCmd:
		ret = string_get(value, prp->errFixCmd, -1);
		break;

	default:
		ret = SNMP_ERR_RES_UNAVAIL;
		break;
	}

	return (ret);
};

/*
 * Free mibpr list.
 */
static void
mibpr_free(void)
{
	struct mibpr *prp;

	while ((prp = first_mibpr()) != NULL) {
		TAILQ_REMOVE (&mibpr_list, prp, link);
		free(prp->names);
		free(prp->errFixCmd);
		free (prp);
	}
}

void
mibpr_fini(void)
{

	mibpr_free();
}
