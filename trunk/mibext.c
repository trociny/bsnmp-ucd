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
 * $Id: mibext.c,v 1.3 2008/01/06 09:06:28 mikolaj Exp $
 *
 */

#include <assert.h>
#include <syslog.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>

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
	int 			_fd[2];
	pid_t			_pid;
	uint64_t		_ticks;
};

TAILQ_HEAD(mibext_list, mibext);

static struct mibext_list mibext_list = TAILQ_HEAD_INITIALIZER(mibext_list);

static struct mibext *
find_ext (int32_t idx)
{
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

void
run_extCommands(void* arg __unused)
{
	struct mibext	*extp;

	uint64_t	current;
	
	current = get_ticks();

	/* run commads which are ready for running */
	
	TAILQ_FOREACH(extp, &mibext_list, link) {
		pid_t	pid;
		
		if (!extp->command)
			continue; /* no command specified */

		if (extp->_pid)
			continue; /* command has already been running */

		if ((current - extp->_ticks) < EXT_UPDATE_INTERVAL)
			continue; /* run command only if EXT_UPDATE_INTERVAL passes after last run */
		
		/* create a pipe */
		if (pipe(extp->_fd) != 0) {
			syslog(LOG_WARNING, "%s: %m", __func__);
			continue;
		}

		/* make the pipe non-blocking */
		fcntl(extp->_fd[0],F_SETFL,O_NONBLOCK);
		fcntl(extp->_fd[1],F_SETFL,O_NONBLOCK);

		if ((pid = fork()) < 0) {
			syslog(LOG_WARNING, "%s: %m", __func__);
			close(extp->_fd[0]);
			close(extp->_fd[1]);
			continue;
		}

		/* execute the command in the child process */
		if (pid==0) {
			
			char	buf[UCDMAXLEN], null[UCDMAXLEN];
			int	fd, len, status;
			FILE	*fp;

			/* close all descriptors except stdio and pipe for reading */
			for (fd = 3; fd < extp->_fd[1]; fd++)
				close(fd);

			/* become process group leader */
			setpgid(0,0);

			/*syslog(LOG_WARNING, "run command `%s'", extp->command);*/
			
			/* run the command */
			if ((fp = popen((char*) extp->command, "r")) == NULL) {
				syslog(LOG_ERR, "popen failed: %m");
				/* send empty line to parent and exit*/
				write(extp->_fd[1], "", 1);
				_exit(127);
			}

			/* read first line to output buffer*/
			if (fgets(buf, UCDMAXLEN-1, fp) != NULL) { /* we have some output */
				
				/* chop 'end of line' */
				len = strlen(buf) - 2;
				if (len < 0) len = 0;
				extp->output[len] = '\0';

				/* just skip other lines */
				while (fgets(null, UCDMAXLEN-1, fp) != NULL);
				
			} else {
				extp->output[0] = '\0';
				len = 0;
			}
			
			/* send output line to parent*/
			write(extp->_fd[1], buf, len+1);

			close(extp->_fd[1]);
			status = pclose(fp);
			_exit(WEXITSTATUS(status));
			
		} else { /* parent */

			extp->_pid = pid;

			/* close pipe for writing */
			close(extp->_fd[1]);

		}
	}

	TAILQ_FOREACH(extp, &mibext_list, link) {

		int n, status;
			
		if(!extp->_pid)
			continue;

		n = read(extp->_fd[0], (char*) extp->output, UCDMAXLEN-1);
		
		/*syslog(LOG_WARNING, "read %d bytes from command `%s'", n, extp->names);*/

		if (n > 0) { /* we have got the data, so the command has finished */
			
			close(extp->_fd[0]);
			
			/* wait for child to exit */
			waitpid(extp->_pid, &status, 0);
			
			extp->_pid = 0;

			/* get the exit code returned from the program */
			extp->result = WEXITSTATUS(status);
			
			/* save time of program finishing */
			extp->_ticks = get_ticks();
		}
	}
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

/* kill all running commands */
static void
mibext_killall (void)
{
	struct mibext *extp;
	TAILQ_FOREACH(extp, &mibext_list, link) {
		if (extp->_pid) {
			syslog(LOG_WARNING, "killing command `%s'", extp->command);
			kill(extp->_pid, SIGTERM);
		}
	}
}

/* free mibext list */
static void
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

void
mibext_fini (void)
{
	mibext_killall();
	mibext_free();
}

int
init_mibext (void)
{
	int	res;
	if ((res = atexit(mibext_killall)) == -1) {
		syslog(LOG_ERR, "atexit failed: %m");
	}
	return res;
}
