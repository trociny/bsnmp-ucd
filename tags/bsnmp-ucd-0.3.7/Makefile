# Copyright (c) 2007-2013 Mikolaj Golub
# All rights reserved.
#
# $Id$

PREFIX?=	/usr/local
LIBDIR=		${PREFIX}/lib
MANDIR=		${PREFIX}/man/man

SHLIB_MAJOR=	1
SHLIB_MINOR=	0

MOD=	ucd
SRCS=	mibconfig.c mibdio.c mibdisk.c mibext.c mibla.c mibmem.c mibpr.c \
	mibss.c mibversion.c utils.c
MAN=	bsnmp-${MOD}.8

XSYM=	ucdavis
.if defined(INSTALL_DEFS)
DEFS=	${MOD}_tree.def
.endif
.if defined(INSTALL_BMIBS)
BMIBS=	UCD-SNMP-MIB.txt
.endif

WARNS=	6

DPADD=	${LIBKVM} ${LIBDEVSTAT} ${LIBM}
LDADD=	-lkvm -ldevstat -lm

.include <bsd.snmpmod.mk>
