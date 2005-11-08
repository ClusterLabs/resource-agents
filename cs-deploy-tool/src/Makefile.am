MAIN_FILES = cs-deploy-tool  \
ads_base.py  \
ads_utils.py  \
authorize_to_node  \
unauthorize_to_node  \
cluster_installer.py  \
cluster_ops.py  \
constants.py  \
environment.py  \
err.py  \
execute.py  \
fence_base.py  \
fence_builder.py  \
IPAddrEntry.py  \
main_win.py  \
module_parser.py  \
resource_ip.py  \
rpm_installer.py  \
storage_config.py  \
storage.py  \
fence_builder.glade  \
main_win.glade

ADSS_FILES = ADSs/__init__.py  \
ADSs/ads_apache.py  \
ADSs/ads_generic.py  \
ADSs/ads_generic.glade  \
ADSs/ads_example.py  \
ADSs/ads_nfsd.py

FENCES_FILES = fences/__init__.py  \
fences/fences.glade  \
fences/fence_apc.py  \
fences/fence_bladecenter.py  \
fences/fence_drac.py  \
fences/fence_ilo.py  \
fences/fence_ipmilan.py  \
fences/fence_manual.py  \
fences/fence_wti.py


EXTRA_DIST = $(MAIN_FILES) $(ADSS_FILES) $(FENCES_FILES)

execdir = $(bindir)

all-local: 


install-exec-local:
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(pkgdatadir) $(DESTDIR)$(pkgdatadir)/ADSs/ $(DESTDIR)$(pkgdatadir)/fences/
	install -m 0755 $(srcdir)/cs-deploy-tool  $(DESTDIR)$(pkgdatadir)/
	install -m 0755 $(srcdir)/authorize_to_node $(DESTDIR)$(pkgdatadir)/
	install -m 0755 $(srcdir)/unauthorize_to_node $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/ads_utils.py $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/ads_base.py $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/cluster_installer.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/cluster_ops.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/constants.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/environment.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/err.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/execute.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/fence_base.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/fence_builder.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/IPAddrEntry.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/main_win.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/module_parser.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/resource_ip.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/rpm_installer.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/storage_config.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/storage.py  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/fence_builder.glade  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/main_win.glade  $(DESTDIR)$(pkgdatadir)/
	install -m 0644 $(srcdir)/ADSs/__init__.py  $(DESTDIR)$(pkgdatadir)/ADSs/
	install -m 0644 $(srcdir)/ADSs/ads_apache.py  $(DESTDIR)$(pkgdatadir)/ADSs/
	install -m 0644 $(srcdir)/ADSs/ads_generic.py  $(DESTDIR)$(pkgdatadir)/ADSs/
	install -m 0644 $(srcdir)/ADSs/ads_generic.glade  $(DESTDIR)$(pkgdatadir)/ADSs/
	install -m 0644 $(srcdir)/ADSs/ads_nfsd.py  $(DESTDIR)$(pkgdatadir)/ADSs/
	install -m 0644 $(srcdir)/fences/__init__.py  $(DESTDIR)$(pkgdatadir)/fences/
	install -m 0644 $(srcdir)/fences/fences.glade  $(DESTDIR)$(pkgdatadir)/fences/
	install -m 0644 $(srcdir)/fences/fence_apc.py  $(DESTDIR)$(pkgdatadir)/fences/
	install -m 0644 $(srcdir)/fences/fence_bladecenter.py  $(DESTDIR)$(pkgdatadir)/fences/
	install -m 0644 $(srcdir)/fences/fence_drac.py  $(DESTDIR)$(pkgdatadir)/fences/
	install -m 0644 $(srcdir)/fences/fence_ilo.py  $(DESTDIR)$(pkgdatadir)/fences/
	install -m 0644 $(srcdir)/fences/fence_ipmilan.py  $(DESTDIR)$(pkgdatadir)/fences/
	install -m 0644 $(srcdir)/fences/fence_manual.py  $(DESTDIR)$(pkgdatadir)/fences/
	install -m 0644 $(srcdir)/fences/fence_wti.py  $(DESTDIR)$(pkgdatadir)/fences/
	softdir=$(pkgdatadir); \
	p=$(DESTDIR) ; \
	softdir=$${softdir/#$$p} ; \
	p=$(prefix) ; \
	softdir=$${softdir/#$$p} ; \
	softdir=$${softdir/#\/} ; \
	ln  -fs ../$${softdir}/cs-deploy-tool $(DESTDIR)$(bindir)/cs-deploy-tool;

uninstall-local:
	rm -fr $(DESTDIR)$(pkgdatadir)
	rm -f  $(DESTDIR)$(bindir)/cs-deploy-tool

docs:
	cd $(srcdir)
	python @PYDOC@ -w $(PYTHON_FILES)
	mv *.html doc

pycheck:
	pychecker -F $(top_srcdir)/pycheckrc $(PYTHON_FILES)

clean:
	rm -f *.pyo *.pyc
	rm -f ${BUILT_SOURCES}
