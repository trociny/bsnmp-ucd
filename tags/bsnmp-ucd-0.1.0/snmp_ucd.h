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
 * $Id: snmp_ucd.h,v 1.1.1.1 2007/12/15 20:22:44 mikolaj Exp $
 *
 */

#ifndef SNMP_UCD_H
#define SNMP_UCD_H

#include <bsnmp/snmpmod.h>
#include "ucd_tree.h"
#include "ucd_oid.h"

#define UPDATE_INTERVAL	500	/* update interval in ticks */
#define UCDMAXLEN	256	/* used as length of buffers */

/*
 * Default swap warning limit (kb) 
 */
#define DEFAULTMINIMUMSWAP 16000


/* utils.c */
void sysctlval(const char *, u_long*);

/* mibla.c */
extern int init_mibla_list (void);

/* mibmem.c */
extern int init_mibmemory (void);

/* minss.c */
extern int init_mibss (void);
extern void get_ss_data (void*);

/* mibversion.c */
extern int init_mibversion (void);

#endif /* SNMP_UCD_H */
