# Copyright (C) 1994, Index Data I/S 
# All rights reserved.
# Sebastian Hammer, Adam Dickmeiss
# $Id: Makefile,v 1.25 1995-02-06 10:08:31 adam Exp $

SHELL=/bin/sh
MAKE=make
SUBDIR=util str bfile dfa dict isam rset it base
CFLAGS=-Wall -g -pedantic -ansi 
CC=gcc

all:
	for i in $(SUBDIR); do cd $$i; if $(MAKE) CFLAGS="$(CFLAGS)" CC="$(CC)"; then cd ..; else exit 1; fi; done

dep depend:
	for i in $(SUBDIR); do cd $$i; if $(MAKE) depend; then cd ..; else exit 1; fi; done

clean:
	for i in $(SUBDIR); do (cd $$i; $(MAKE) clean); done

cleanup:
	rm -f `find $(SUBDIR) -name "*.[oa]" -print`
	rm -f `find $(SUBDIR) -name "core" -print`
	rm -f `find $(SUBDIR) -name "errlist" -print`
	rm -f `find $(SUBDIR) -name "a.out" -print`

distclean: cleanup clean
	for i in $(SUBDIR); do (cd $$i; \
		mv Makefile Makefile.old; \
		sed '/^#Depend/q' <Makefile.old >Makefile; \
		rm Makefile.old); done

usedepend1:
	for i in $(SUBDIR); do (cd $$i; \
		mv Makefile Makefile.tmp; \
		sed 's/^if/#if/' <Makefile.tmp|sed 's/^include/#include/'| \
		sed 's/^endif/#endif/' | \
		sed 's/^depend: depend2/depend: depend1/g' >Makefile; \
		rm Makefile.tmp); done

usedepend2:
	for i in $(SUBDIR); do (cd $$i; \
		mv Makefile Makefile.tmp; \
		sed '/^#Depend/q' <Makefile.tmp| \
		sed 's/^#if/if/' |sed 's/^#include/include/'| \
		sed 's/^#endif/endif/' | \
		sed 's/^depend: depend1/depend: depend2/g' >Makefile; \
		rm Makefile.tmp); done

wc:
	wc `find . -name '*.[ch]'`
	
