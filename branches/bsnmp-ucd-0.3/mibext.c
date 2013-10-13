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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "snmp_ucd.h"

/*
 * mibext structures and functions.
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

static void run_extCommands(void*);
static void run_extFixCmds(void*);

static struct mibext *
find_ext(int32_t idx)
{
	struct mibext *extp;

	TAILQ_FOREACH(extp, &mibext_list, link) {
		if (extp->index == idx)
			break;
	}

	return (extp);
}

/*
 * Handle timeouts when executing commands.
 */
static void
extcmd_sighandler(int sig __unused)
{

	_exit(127);
}


/*
 * Run commands and collect results of programs that have already finished.
 */
static void
run_extCommands(void* arg __unused)
{
	struct mibext *extp;
	struct ext_msg msg;
	uint64_t current;
	FILE *fp;
	int status, fd, n;
	pid_t pid, res;
	char null[UCDMAXLEN];


	current = get_ticks();

	/* Run commads which are ready for running. */

	TAILQ_FOREACH(extp, &mibext_list, link) {
		if (!extp->command)
			continue; /* No command specified. */

		if (extp->_is_running)
			continue; /* Command has already been running. */

		if ((current - extp->_ticks) < ext_update_interval)
			continue; /* ext_update_interval has not passed yet. */

		/* Make a pipe */
		if (pipe(extp->_fd) == -1) {
			syslog(LOG_ERR, "failed to pipe: %s: %m", __func__);
			continue;
		}
		fcntl(extp->_fd[0],F_SETFL,O_NONBLOCK);
		fcntl(extp->_fd[1],F_SETFL,O_NONBLOCK);

		/* Execute the command in the child process. */
		pid = fork();

		if (pid == 0) {
			/* Close all descriptors except stdio and pipe for reading. */
			for (fd = 3; fd < extp->_fd[1]; fd++)
				close(fd);

			/* Fork again. */
			if ((pid = fork()) < 0) {
				syslog(LOG_ERR, "Can't fork: %s: %m", __func__);
				_exit(127);
			}

			if (pid == 0) { /* grandchild */
				msg.output[0] = '\0';

				/* Become process group leader. */
				setpgid(0, 0);

				/* Trap commands that timeout. */
				signal(SIGALRM, extcmd_sighandler);
 				alarm(ext_timeout);

				/* Run the command. */
				fp = popen((char*) extp->command, "r");
				if (fp == NULL) {
					syslog(LOG_ERR, "popen failed: %s: %m",
					    __func__);
					/* Send empty line to parent and exit. */
					msg.result = 127;
					write(extp->_fd[1], (char*) &msg,
					    sizeof(msg));
					_exit(127);
				}

				/* Read the first line to output buffer. */
				if (fgets((char*) msg.output, sizeof(msg.output), fp) != NULL) { /* we have some output */
					int	end;

					/* Chop 'end of line'. */
					end = strlen((char*) msg.output) - 1;
					if ((end >= 0) && (msg.output[end] == '\n'))
						msg.output[end] = '\0';

					/* Just skip other lines. */
					while (fgets(null, sizeof(null), fp) != NULL);
				}

				status = pclose(fp);
				msg.result = WEXITSTATUS(status);
				/* Send the result to the parent. */
				write(extp->_fd[1], (char*) &msg, sizeof(msg));
				close(extp->_fd[1]);
				_exit(msg.result);
			} else { /* Parent of grandchild. */
				_exit(0);
			}
		}

		/* Parent (bsnmpd process). */

		if (pid == -1) {
			syslog(LOG_ERR, "Can't fork: %s: %m", __func__);
			close(extp->_fd[0]);
			close(extp->_fd[1]);
			continue;
		}

		/* Close pipe for writing. */
		close(extp->_fd[1]);

		/* Wait for child. */
		for (;;) {
			res = waitpid(pid, &status, 0);
			if (res == -1 && errno == EINTR)
				continue;

			if (res == -1) {
				syslog(LOG_ERR, "waitpid failed: %s: %m",
				    __func__);
				break;
			} else {
				/* Get the exit code returned from the program. */
				status = WEXITSTATUS(status);
			}

			if (status != 0) {
				/*
				 * Something wrong with running program.
				 * Treat this as the program has finished
				 * abnormaly.
				 */

				/* Save the program termination time. */
				extp->_ticks = get_ticks();

				/* Close the pipe for reading. */
				close(extp->_fd[0]);

				/* Fill extp data. */
				extp->result = 127;
				extp->output[0] = '\0';
				extp->_is_running = 0;
			} else { /*  Program is running. */
				extp->_is_running = 1;
			}
			break;
		}
	}

	/* Collect data of finished commands. */

	TAILQ_FOREACH(extp, &mibext_list, link) {
		if (!extp->_is_running)
			break; /* Programm is not running */

		for (;;) {
			n = read(extp->_fd[0], (char*) &msg, sizeof(msg));

			if (n == -1 && errno == EINTR)
				continue;	/* Interrupted. Try again. */

			if (n == -1 && errno == EAGAIN)
				break;		/* No data this time. */

			if (n != sizeof(msg)) {
				/*
				 * Read returned something wrong. Mark the
				 * command as abnormally finished
				 */
				extp->result = 127;
				strncpy((char*) extp->output, "Exited abnormally!",
				    sizeof(extp->output) - 1);
			} else {
				extp->result = msg.result;
				strncpy((char *)extp->output, (char *)msg.output,
				    sizeof(extp->output) - 1);
			}

			extp->_is_running = 0;

			close(extp->_fd[0]);

			/* Save the program termination time. */
			extp->_ticks = get_ticks();

			break;
		}
	}
}

/*
 * Run fix commands.
 */
static void
run_extFixCmds(void* arg __unused)
{
	struct mibext *extp;
	uint64_t current;
	pid_t pid, res;
	int status, fd;

	current = get_ticks();

	/* Run commads if needed. */

	TAILQ_FOREACH(extp, &mibext_list, link) {
		if (!extp->errFix)
			continue;	/* No fix. */

		if (!extp->errFixCmd)
			continue;	/* No command specified. */

		if (extp->result == 0)
			continue;	/* Checked command exited normaly, no need for fix. */

		if ((current - extp->_fix_ticks) < ext_update_interval)
			continue;  /* ext_update_interval has not passed yet. */

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

			if (pid == 0) { /* grandchild */
				/* Become process group leader. */
				setpgid(0, 0);

				/* Trap commands that timeout. */
				signal(SIGALRM, extcmd_sighandler);
				alarm(ext_timeout);

				/* Run the command. */
				status = system((char*) extp->errFixCmd);
				if (status != 0) {
					syslog(LOG_WARNING,
					    "command `%s' has retuned status %d",
					    extp->errFixCmd, WEXITSTATUS(status));
				}
				_exit(WEXITSTATUS(status));
			}

			/* Parent of grandchild. */
			_exit(0);
		}

		/* Parent. */

		if (pid == -1) {
			syslog(LOG_ERR, "Can't fork: %s: %m", __func__);
			continue;
		}

		/* Wait for child. */
		for (;;) {
			res = waitpid(pid, &status, 0);
			if (res == -1 && errno == EINTR)
				continue;
			if (res == -1)
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
	struct mibext *extp = NULL;
	asn_subid_t which;
	int ret;

	which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GETNEXT:
		extp = NEXT_OBJECT_INT(&mibext_list, &value->var, sub);
		if (extp == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = extp->index;
		break;

	case SNMP_OP_GET:
		if (value->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		extp = find_ext(value->var.subs[sub]);
		if (extp == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;

	case SNMP_OP_SET:
		switch (which) {
		case LEAF_extNames:
			extp = find_ext(value->var.subs[sub]);
			if (extp == NULL) {
				extp = malloc(sizeof(*extp));
				if (extp == NULL) {
					syslog(LOG_ERR,
					    "failed to malloc: %s: %m",
					    __func__);
					return (SNMP_ERR_NOT_WRITEABLE);
				}
				memset(extp, 0, sizeof(*extp));
				extp->index = value->var.subs[sub];
				INSERT_OBJECT_INT(extp, &mibext_list);
			} else {
				/*
				 * We have already had some command defined
				 * under this index. Check if the command is
				 * running to close our end of pipe.
				 */
				if (extp->_is_running) {
					close(extp->_fd[0]);
					extp->_is_running = 0;
				}
			}
			ret = string_save(value, context, -1, &extp->names);
			return (ret);

		case LEAF_extCommand:
			extp = find_ext(value->var.subs[sub]);
			if (extp == NULL)
				return (SNMP_ERR_NOT_WRITEABLE);
			ret = string_save(value, context, -1, &extp->command);
			return (ret);

		case LEAF_extErrFix:
			extp = find_ext(value->var.subs[sub]);
			if (extp == NULL)
				return (SNMP_ERR_NOT_WRITEABLE);
			extp->errFix = value->v.integer;
			return SNMP_ERR_NOERROR;

		case LEAF_extErrFixCmd:
			extp = find_ext(value->var.subs[sub]);
			if (extp == NULL)
				return (SNMP_ERR_NOT_WRITEABLE);
			ret = string_save(value, context, -1,
			    &extp->errFixCmd);
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

/*
 * mibext initialization.
 */
void
mibext_init(void)
{

	register_ext_check_interval_timer(run_extCommands);
	register_ext_check_interval_timer(run_extFixCmds);
}

/*
 * Free mibext list.
 */
static void
mibext_free(void)
{
	struct mibext *extp;

	while ((extp = TAILQ_FIRST(&mibext_list)) != NULL) {
		TAILQ_REMOVE (&mibext_list, extp, link);
		free(extp->names);
		free(extp->command);
		free(extp->errFixCmd);
		free (extp);
	}
}

void
mibext_fini(void)
{

	mibext_free();
}
