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

#ifndef SNMP_UCD_H
#define SNMP_UCD_H

#include <bsnmp/snmpmod.h>
#include "ucd_tree.h"
#include "ucd_oid.h"

#define UCDMAXLEN		256	/* Used as length of buffers. */

/* Default swap warning limit (kb). */
#define DEFAULTMINIMUMSWAP	16000

/* Default laConfig value. */
#define LACONFIG		"12.00"

/* snmp_ucd.c */
void register_update_interval_timer(void (*hook_f)(void*));
void register_ext_check_interval_timer(void (*hook_f)(void*));
void restart_update_interval_timer(void);
void restart_ext_check_interval_timer(void);

/* utils.c */
extern void sysctlval(const char *, u_long*);

/* mibconfig.c */

/* Update interval in ticks. */
extern u_int update_interval;

/* Ext command exited check interval in ticks. */
extern u_int ext_check_interval;

/* Ext command re-run interval in ticks. */
extern u_int ext_update_interval;

/* Ext command execution timeout in sec. */
extern u_int ext_timeout;

extern void mibconfig_init(void);

/* mibla.c */
extern void mibla_init(void);

/* mibmem.c */
extern void mibmemory_init(void);

/* mibss.c */
extern void mibss_init(void);

/* mibext.c */
extern void mibext_init(void);
extern void mibext_fini(void);

/* mibdisk,c */
extern void mibdisk_fini(void);
extern void mibdisk_init(void);

/* mibdio.c */
extern void mibdio_fini(void);
extern void mibdio_init(void);

/* mibpr.c */
extern void mibpr_init(void);
extern void mibpr_fini(void);

/* mibversion.c */
extern void mibversion_init(void);

#endif /* SNMP_UCD_H */
