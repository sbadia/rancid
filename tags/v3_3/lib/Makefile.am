## Process this file with automake to produce Makefile.in
## A Makefile.in is supplied, in case you do not have automake.

## $Id: Makefile.am 2481 2012-05-24 17:39:33Z heas $
##

AUTOMAKE_OPTIONS=foreign no-dependencies

my_pkglibdir = $(pkglibdir)
my_pkglib_DATA = rancid.pm arbor.pm ciscowlc.pm dell.pm eos.pm foundry.pm \
		 ios.pm iosxr.pm junos.pm mrv.pm nxos.pm panos.pm rbt.pm \
		 wavesvros.pm
pkglib_in = $(my_pkglib_DATA:%=%.in)

RANLIB= :

EXTRA_DIST= $(pkglib_in)
##dist_bin_SCRIPTS= $(bin_SCRIPTS:%=%.in)

CLEANFILES= rancid
CLEANFILES+= $(my_pkglib_DATA)

#clean:
#	rm -f Makefile env $(BIN_DATAS) $(BIN_PROGS)

# auto_edit does the autoconf variable substitution.  This allows the
# substitution to have the full expansion of the variables, e.g.: $sysconfdir
# will be /prefix/etc instead of ${prefix}/etc.
#
# This is a bit of a PITA, but is the method recommended by the autoconf
# documentation.
auto_edit = sed \
	-e 's,@prefix\@,$(prefix),g' \
	-e 's,@localstatedir\@,$(localstatedir),g' \
	-e 's,@sysconfdir\@,$(sysconfdir),g' \
	-e 's,@EXPECT_PATH\@,$(EXPECT_PATH),g' \
	-e 's,@PERLV\@,$(PERLV),g' \
	-e 's,@PERLV_PATH\@,$(PERLV_PATH),g' \
	-e 's,@LG_PING_CMD\@,$(LG_PING_CMD),g' \
	-e 's,@ADMINMAILPLUS\@,$(ADMINMAILPLUS),g' \
	-e 's,@MAILPLUS\@,$(MAILPLUS),g' \
	-e 's,@PACKAGE\@,$(PACKAGE),g' \
	-e 's,@SVN_FSTYPE\@,$(SVN_FSTYPE),g' \
	-e 's,@VERSION\@,$(VERSION),g' \
	-e 's,@COPYYEARS\@,$(COPYYEARS),g' \
	-e '/@copyright@/r ../COPYING' \
	-e '/@copyright@/d'

$(pkglib_in:%.in=%): Makefile $(top_builddir)/config.status
	rm -f $@ $@.tmp; \
	$(auto_edit) $(srcdir)/$@.in >$@.tmp; \
	chmod +r $@.tmp; \
	mv $@.tmp $@
# XXX there must be a better way to produce these dependencies, perhaps in
#     configure.
arbor.pm: arbor.pm.in
dell.pm: dell.pm.in
ciscowlc.pm: ciscowlc.pm.in
eos.pm: eos.pm.in
foundry.pm: foundry.pm.in
ios.pm: ios.pm.in
iosxr.pm: iosxr.pm.in
junos.pm: junos.pm.in
rancid.pm: rancid.pm.in
mrv.pm: mrv.pm.in
nxos.pm: nxos.pm.in
panos.pm: panos.pm.in
rbt.pm: rbt.pm.in
wavesvros.pm: wavesvros.pm.in
