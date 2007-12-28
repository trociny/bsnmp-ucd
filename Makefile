# Copyright (c) 2007 Mikolaj Golub
# All rights reserved.
#
# $Id: Makefile,v 1.2 2007/12/28 20:10:30 mikolaj Exp $

MOD=	ucd
SRCS=	${MOD}_tree.c snmp_${MOD}.c utils.c \
	mibla.c mibmem.c mibss.c mibext.c mibversion.c
INCS=	snmp_${MOD}.h
DEFS=	${MOD}_tree.def
MAN8=	bsnmp-${MOD}.8

XSYM=	ucdavis

WARNS=	-Wsystem-headers -Werror -Wall -Wno-format-y2k -W \
	-Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith -Wreturn-type -Wcast-qual \
	-Wwrite-strings -Wswitch -Wshadow -Wcast-align \
	-Wbad-function-cast -Wchar-subscripts -Winline \
	-Wnested-externs -Wredundant-decls -std=c99

CFLAGS=	${WARNS} -O2 

LIB=	snmp_${MOD}.la
SHLIB_MAJOR=	1
SHLIB_MINOR=	0

LIBTOOL=	libtool
GENSNMPTREE=	gensnmptree

LOCALBASE ?=	/usr/local

LIBDIR =	${LOCALBASE}/lib
MANDIR = 	${LOCALBASE}/man

CLEANFILES +=	*.la *.lo *.o .libs
CLEANFILES +=	${MOD}_oid.h ${MOD}_tree.c ${MOD}_tree.h

INSTALL_DIR=	install  -d -o root -g wheel -m 755
INSTALL_DATA=	install  -o root -g wheel -m 444

all:	$(LIB)

$(LIB): ${MOD}_oid.h ${MOD}_tree.h $(SRCS:.c=.lo)
	$(LIBTOOL) --mode=link $(CC) $(LDLAGS) -module -o ${.TARGET} $(SRCS:.c=.lo) -rpath $(LIBDIR) -version-info $(SHLIB_MAJOR):$(SHLIB_MINOR)

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

clean:
	rm -Rf ${CLEANFILES}
