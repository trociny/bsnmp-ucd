# Copyright (c) 2007 Mikolaj Golub
# All rights reserved.
#
# $Id: Makefile,v 1.6.2.4 2009/07/23 19:19:54 mikolaj Exp $

MOD=	ucd
SRCS=	${MOD}_tree.c snmp_${MOD}.c utils.c \
	mibdio.c mibext.c mibla.c mibmem.c mibss.c mibversion.c
INCS=	snmp_${MOD}.h
DEFS=	${MOD}_tree.def
MAN8=	bsnmp-${MOD}.8

XSYM=	ucdavis

LDADD=	-lkvm -ldevstat

WARNS=	-Wsystem-headers -Werror -Wall -Wno-format-y2k -W \
	-Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align \
	-Wbad-function-cast -Wchar-subscripts -Winline \
	-Wnested-externs -Wredundant-decls -std=c99

CFLAGS +=	${WARNS} 

LIB=	snmp_${MOD}.la
SHLIB_MAJOR=	1
SHLIB_MINOR=	0

LIBTOOL=	libtool
GENSNMPTREE=	gensnmptree

PREFIX ?=	/usr/local

LIBDIR =	${PREFIX}/lib
MANDIR = 	${PREFIX}/man
EXAMPLEDIR =	${PREFIX}/share/examples

CLEANFILES +=	*.la *.lo *.o .libs
CLEANFILES +=	${MOD}_oid.h ${MOD}_tree.c ${MOD}_tree.h

INSTALL_DIR=	install  -d -o root -g wheel -m 755
INSTALL_DATA=	install  -o root -g wheel -m 444

all:	$(LIB)

$(LIB): ${MOD}_oid.h ${MOD}_tree.h $(SRCS:.c=.lo)
	$(LIBTOOL) --mode=link $(CC) $(LDADD) $(LDLAGS) -module -o ${.TARGET} $(SRCS:.c=.lo) -rpath $(LIBDIR) -version-info $(SHLIB_MAJOR):$(SHLIB_MINOR)

.SUFFIXES: .lo

$(SRCS:.c=.lo): $(SRCS) ${MOD}_oid.h ${MOD}_tree.h $(INCS)
	$(LIBTOOL) --mode=compile $(CC) -c $(CFLAGS) -o ${.TARGET} ${.PREFIX}.c

${MOD}_oid.h: ${MOD}_tree.def
	${GENSNMPTREE} <${MOD}_tree.def -e ${XSYM} > ${.TARGET}

${MOD}_tree.h ${MOD}_tree.c : ${MOD}_tree.def
	${GENSNMPTREE} <${MOD}_tree.def -p ${MOD}_

install: $(LIB)
	@$(INSTALL_DIR) $(LIBDIR)
	$(LIBTOOL) --mode=install $(INSTALL_DATA) $(LIB) $(LIBDIR)
	@$(INSTALL_DIR) $(MANDIR)/man8
	for f in $(MAN8) ; do \
		$(INSTALL_DATA) $${f} $(MANDIR)/man8/$${f} ; \
	done
	@$(INSTALL_DIR) $(EXAMPLEDIR)/bsnmp-${MOD}
	$(INSTALL_DATA) snmpd.config.sample $(EXAMPLEDIR)/bsnmp-${MOD}/snmpd.config.sample

clean:
	rm -Rf ${CLEANFILES}
