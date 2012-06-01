/*
 * Copyright (c) 2007-2012 Mikolaj Golub
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

#ifndef SNMP_UCD_H
#define SNMP_UCD_H

#include <bsnmp/snmpmod.h>
#include "ucd_tree.h"
#include "ucd_oid.h"

#define UPDATE_INTERVAL		500	/* update interval in ticks */
#define EXT_CHECK_INTERVAL	100	/* if command exited check interval in ticks */
#define EXT_UPDATE_INTERVAL	3000	/* ext commands rerun interval in ticks */
#define EXT_TIMEOUT		60	/* ext commands execution timeout interval in secs */

#define UCDMAXLEN		256	/* used as length of buffers */

/* Default swap warning limit (kb) */
#define DEFAULTMINIMUMSWAP	16000

/* Default laConfig value */
#define LACONFIG		"12.00"

/* utils.c */
extern int ucd_debug;
extern void sysctlval(const char *, u_long*);

/* mibla.c */
extern void mibla_init (void);

/* mibmem.c */
extern void mibmemory_init (void);

/* mibss.c */
extern void mibss_init (void);
extern void update_ss_data (void*);

/* mibext.c */
extern void mibext_fini (void);
extern void run_extCommands (void*);
extern void run_extFixCmds (void*);

/* mibdisk,c */
extern void mibdisk_fini (void);
extern void mibdisk_init (void);

/* mibdio.c */
extern void mibdio_fini (void);
extern void mibdio_init (void);
extern void update_dio_data (void*);

/* mibpr.c */
extern void mibpr_fini (void);
extern void run_prCommands (void*);
extern void run_prFixCmds (void*);

/* mibversion.c */
extern void mibversion_init (void);

#endif /* SNMP_UCD_H */
