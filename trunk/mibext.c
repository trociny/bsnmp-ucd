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
 * $Id: mibext.c,v 1.12 2008/01/30 21:16:34 mikolaj Exp $
 *
 */

#include <syslog.h>
#include <sys/queue.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <paths.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/time.h>

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
	int			_is_running;
	uint64_t		_ticks;
	uint64_t		_fix_ticks;
};

struct ext_msg {
	int32_t		result;
	u_char		output[UCDMAXLEN];
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

/* handle timeouts when executing commands */

static void
extcmd_sighandler(int sig __unused)
{
	_exit(127);
}


/* run commands and collect results of programs that have already finished */

void
run_extCommands(void* arg __unused)
{
	struct mibext	*extp;

	uint64_t	current;
	
	current = get_ticks();

	/* run commads which are ready for running */
	
	TAILQ_FOREACH(extp, &mibext_list, link) {
		int	status;
		pid_t	pid;
		
		if (!extp->command)
			continue; /* no command specified */

		if (extp->_is_running)
			continue; /* command has already been running */

		if ((current - extp->_ticks) < EXT_UPDATE_INTERVAL)
			continue; /* run command only if EXT_UPDATE_INTERVAL passes after last run */
		
		/* create a pipe */
		if (pipe(extp->_fd) != 0) {
			syslog(LOG_ERR, "failed to pipe: %s: %m", __func__);
			continue;
		}

		/* make the pipe non-blocking */
		fcntl(extp->_fd[0],F_SETFL,O_NONBLOCK);
		fcntl(extp->_fd[1],F_SETFL,O_NONBLOCK);

		pid = fork();

		/* execute the command in the child process */
		if (pid==0) {
			int fd;
			
			/* close all descriptors except stdio and pipe for reading */
			for (fd = 3; fd < extp->_fd[1]; fd++)
				close(fd);

			/* fork again */
			if ((pid = fork()) < 0) {
				syslog(LOG_ERR, "Can't fork: %s: %m", __func__);
				_exit(127);
			}

			if (pid == 0) { /* grandchild */
				struct ext_msg	msg;
				char	null[UCDMAXLEN];
				FILE	*fp;
				
				msg.output[0] = '\0';
				
				/* become process group leader */
				setpgid(0, 0);

				/* trap commands that timeout */
				signal(SIGALRM, extcmd_sighandler);
 				alarm(EXT_TIMEOUT);
				
				/* run the command */
				if ((fp = popen((char*) extp->command, "r")) == NULL) {
					syslog(LOG_ERR, "popen failed: %s: %m", __func__);
					/* send empty line to parent and exit*/
					msg.result = 127;
					write(extp->_fd[1], (char*) &msg, sizeof(msg));
					_exit(127);
				}

				/* read first line to output buffer*/
				if (fgets((char*) msg.output, UCDMAXLEN-1, fp) != NULL) { /* we have some output */
					int	end;
					
					/* chop 'end of line' */
					end = strlen((char*) msg.output) - 1;
					if ((end >= 0) && (msg.output[end] == '\n'))
						msg.output[end] = '\0';

					/* just skip other lines */
					while (fgets(null, UCDMAXLEN-1, fp) != NULL);
				
				}
				
				status = pclose(fp);
				msg.result = WEXITSTATUS(status);
				/* send result to parent*/
				write(extp->_fd[1], (char*) &msg, sizeof(msg));
				close(extp->_fd[1]);
				_exit(msg.result);
				
			} else { /* parent of grandchild */

				_exit(0);
			}
		}

		/* parent (bsnmpd process) */
		
		if (pid < 0) {
			syslog(LOG_ERR, "Can't fork: %s: %m", __func__);
			close(extp->_fd[0]);
			close(extp->_fd[1]);
			continue;
		}
		
		/* close pipe for writing */
		close(extp->_fd[1]);

		/* wait for child */
		for (;;) {
			pid_t res;
			res = waitpid(pid, &status, 0);
			if (res == -1 && errno == EINTR)
				continue;
			
			if (res <= 0) {
				syslog(LOG_ERR, "waitpid failed: %s: %m", __func__);
				break;
			} else {
				/* get the exit code returned from the program */
				status = WEXITSTATUS(status);
			}
			
			if ((res <= 0) || status) {
				/* something wrong with running program */
				/* consider it as program has finished abnormaly */

				/* save time of program finishing */
				extp->_ticks = get_ticks();
				
				/* close pipe for reading */
				close(extp->_fd[0]);

				/* fill extp data */
				extp->result = 127;
				extp->output[0] = '\0';
				extp->_is_running = 0;
			} else { /*  program is runnng */
				extp->_is_running = 1;
			}
			break;
		}
	}

	/* collect data of finished commands */
	
	TAILQ_FOREACH(extp, &mibext_list, link) {
		struct ext_msg	msg;
		int	n;

		if (!extp->_is_running)
			break; /* programm is not running */

		for (;;) {
			n = read(extp->_fd[0], (char*) &msg, sizeof(msg));
			
			if (n == -1 && errno == EINTR)
				continue;	/* interrupted; try again */
			
			if (n == -1 && errno == EAGAIN)
				break;		/* no data this time */
			
			if (n != sizeof(msg)) {
				/* read returned something wrong */
				/* mark command as abnormally finished */
				extp->result = 127;
				strncpy((char*) extp->output, "Exited abnormally!", UCDMAXLEN-1);
			} else {
				extp->result = msg.result;
				strncpy((char*) extp->output, (char*) msg.output, UCDMAXLEN-1);
			}
			
			extp->_is_running = 0;
			
			close(extp->_fd[0]);
			
			/* save time of program finishing */
			extp->_ticks = get_ticks();

			break;
		}
	}
}

/* get max fd opened */

static int
get_fdmax(void) {
	int fd;
	/* FIXME: may be there is more simple way to find out max fd */
	fd = open(_PATH_DEVNULL, O_RDONLY);
	if (fd < 0) {
		syslog(LOG_ERR, "Can't open /dev/null: %s: %m", __func__);
		return -1;
	}
	close(fd);
	return fd-1;
}

/* run fix commands */

void
run_extFixCmds(void* arg __unused)
{
	struct mibext	*extp;

	uint64_t	current;
	
	current = get_ticks();

	/* run commads if needed */
	
	TAILQ_FOREACH(extp, &mibext_list, link) {
		pid_t	pid;
		int	status;
		
		if (!extp->errFix)
			continue;	/* no fix */

		if (!extp->errFixCmd)
			continue;	/* no command specified */

		if (extp->result == 0)
			continue;	/* checked command exited normaly, no need for fix*/

		if ((current - extp->_fix_ticks) < EXT_UPDATE_INTERVAL)
			continue; /* run command only if EXT_UPDATE_INTERVAL passes after last run */

		pid = fork();
		
		/* execute the command in the child process */
		if (pid==0) {

			int	fd, fdmax;
			
			/* close all descriptors except stdio */
			fdmax = get_fdmax();
			for (fd = 3; fd <= fdmax; fd++)
				close(fd);

			/* fork again */
			
			if ((pid = fork()) < 0) {
				syslog(LOG_ERR, "Can't fork: %s: %m", __func__);
				_exit(127);
			}

			if (pid == 0) { /* grandchild */
				
				/* become process group leader */
				setpgid(0, 0);

				/* trap commands that timeout */
				signal(SIGALRM, extcmd_sighandler);
				alarm(EXT_TIMEOUT);

				/* run the command */
				if ((status = system((char*) extp->errFixCmd)) != 0)
					syslog(LOG_WARNING, "command `%s' has retuned status %d", extp->errFixCmd, WEXITSTATUS(status));
				_exit(WEXITSTATUS(status));
				
			}

			/* parent of grandchild */
			_exit(0);
		}

		/* parent */

		if (pid < 0)
			syslog(LOG_ERR, "Can't fork: %s: %m", __func__);

		/* wait for child */
		for (;;) {
			pid_t res;
			res = waitpid(pid, &status, 0);
			if (res == -1 && errno == EINTR)
				continue;
			if (res <= 0)
				syslog(LOG_ERR, "failed to waitpid: %s: %m", __func__);
			break;
		}		
		extp->_fix_ticks = get_ticks();
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
							syslog(LOG_ERR, "failed to malloc: %s: %m", __func__);
							return (SNMP_ERR_NOT_WRITEABLE);
						}
						memset(extp, 0, sizeof(*extp));
						extp->index = value->var.subs[sub];
						INSERT_OBJECT_INT(extp, &mibext_list);
					} else {
						/* we have already had some command defined  under this index.*/
						/* check if the command is run to close our end of pipe */
						if (extp->_is_running) {
							close(extp->_fd[0]);
							extp->_is_running = 0;
						}
					}
					return  string_save(value, context, -1, &extp->names);

				case LEAF_extCommand:
					if ((extp = find_ext(value->var.subs[sub])) == NULL)
						return (SNMP_ERR_NOT_WRITEABLE);
					return  string_save(value, context, -1, &extp->command);

				case LEAF_extErrFix:
					if ((extp = find_ext(value->var.subs[sub])) == NULL)
						return (SNMP_ERR_NOT_WRITEABLE);
					extp->errFix = value->v.integer;
					return SNMP_ERR_NOERROR;

				case LEAF_extErrFixCmd:
					if ((extp = find_ext(value->var.subs[sub])) == NULL)
						return (SNMP_ERR_NOT_WRITEABLE);
					return  string_save(value, context, -1, &extp->errFixCmd);

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
			ret = SNMP_ERR_RES_UNAVAIL;
			break;
	}

	return (ret);
};

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
	mibext_free();
}
