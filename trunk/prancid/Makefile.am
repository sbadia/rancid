## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS=foreign no-dependencies

@SET_MAKE@

#EXTRA_DIST = BUGS CHANGES COPYING FAQ README Todo cloginrc.sample configure \
#	install-sh \
#	mkinstalldirs Makefile.in Todo
#DIST_COMMON = 

# '.' is here (and at the beginnging of the macro) so that distclean-recursive
# will run make distclean in . after the other dirs (preserving Makefile)
#SUBDIRS = . include bin man util
SUBDIRS = . bin

# install-exec-local:
# 	$(mkinstalldirs) $(prefix)/util/lg

#install-data-hook:
#	$(mkinstalldirs) $(prefix) ; \
#	for file in CHANGES FAQ README cloginrc.sample ; do \
#	    $(INSTALL_DATA) $$file $(prefix); \
#	done
