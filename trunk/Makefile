# Copyright (c) 2007-2013 Mikolaj Golub
# All rights reserved.
#
# $Id$

PREFIX?=	/usr/local

LIBDIR?=	${PREFIX}/lib
MANDIR?= 	${PREFIX}/man
EXAMPLEDIR?=	${PREFIX}/share/examples

MKDIR?=			mkdir -p
BSD_INSTALL_DATA?=	install -m 444

LIBTOOL?=	libtool
GENSNMPTREE?=	gensnmptree

SHLIB_MAJOR=	1
SHLIB_MINOR=	0

MOD=	ucd
SRCS=	${MOD}_tree.c snmp_${MOD}.c utils.c \
	mibconfig.c mibdio.c mibdisk.c mibext.c mibla.c mibmem.c mibpr.c \
	mibss.c mibversion.c
INCS=	snmp_${MOD}.h
DEFS=	${MOD}_tree.def
MAN8=	bsnmp-${MOD}.8

XSYM=	ucdavis

LDADD=	-lkvm -ldevstat -lm

WARNS=	-Wsystem-headers -Werror -Wall -Wno-format-y2k -W \
	-Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align \
	-Wbad-function-cast -Wchar-subscripts -Winline \
	-Wnested-externs -Wredundant-decls -std=c99

CFLAGS +=	${WARNS}

LIB=	snmp_${MOD}.la

CLEANFILES+=	*.la *.lo *.o .libs
CLEANFILES+=	${MOD}_oid.h ${MOD}_tree.c ${MOD}_tree.h

all:	${LIB}

${LIB}: ${MOD}_oid.h ${MOD}_tree.h ${SRCS:.c=.lo}
	${LIBTOOL} --mode=link ${CC} ${LDADD} ${LDLAGS} -module -o ${.TARGET} \
		${SRCS:.c=.lo} -rpath ${LIBDIR} \
		-version-info ${SHLIB_MAJOR}:${SHLIB_MINOR}

.SUFFIXES: .lo

${SRCS:.c=.lo}: ${SRCS} ${MOD}_oid.h ${MOD}_tree.h ${INCS}
	${LIBTOOL} --mode=compile ${CC} -c ${CFLAGS} -o ${.TARGET} ${.PREFIX}.c

${MOD}_oid.h: ${MOD}_tree.def
	${GENSNMPTREE} <${MOD}_tree.def -e ${XSYM} > ${.TARGET}

${MOD}_tree.h ${MOD}_tree.c : ${MOD}_tree.def
	${GENSNMPTREE} <${MOD}_tree.def -p ${MOD}_

install: ${LIB}
	@${MKDIR} ${DESTDIR}${LIBDIR}
	${LIBTOOL} --mode=install ${BSD_INSTALL_DATA} ${LIB} ${DESTDIR}${LIBDIR}
	@${MKDIR} ${DESTDIR}${MANDIR}/man8
.for f in ${MAN8}
	${BSD_INSTALL_DATA} ${f} ${DESTDIR}${MANDIR}/man8/${f}
.endfor
	@${MKDIR} ${DESTDIR}${EXAMPLEDIR}/bsnmp-${MOD}
	${BSD_INSTALL_DATA} snmpd.config.sample \
		${DESTDIR}${EXAMPLEDIR}/bsnmp-${MOD}/snmpd.config.sample

clean:
	rm -Rf ${CLEANFILES}
