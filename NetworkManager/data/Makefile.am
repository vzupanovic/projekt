
if HAVE_SYSTEMD
systemdsystemunit_DATA = \
	NetworkManager.service \
	NetworkManager-wait-online.service \
	NetworkManager-dispatcher.service

NetworkManager.service: NetworkManager.service.in
	$(edit) $< >$@

if HAVE_SYSTEMD_200
NetworkManager-wait-online.service: NetworkManager-wait-online.service.in
	$(edit) $< >$@
else
NetworkManager-wait-online.service: NetworkManager-wait-online-systemd-pre200.service.in
	$(edit) $< >$@
endif

NetworkManager-dispatcher.service: NetworkManager-dispatcher.service.in
	$(edit) $< >$@

servicedir = $(datadir)/dbus-1/system-services
service_in_files = org.freedesktop.NetworkManager.service.in
service_DATA = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	$(edit) $< >$@
endif

examplesdir = $(docdir)/examples
examples_DATA = server.conf

server.conf: server.conf.in
	$(edit) $< >$@

edit = sed \
	-e 's|@bindir[@]|$(bindir)|g' \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g'

EXTRA_DIST = \
	NetworkManager.service.in \
	NetworkManager-wait-online.service.in \
	NetworkManager-wait-online-systemd-pre200.service.in \
	NetworkManager-dispatcher.service.in \
	org.freedesktop.NetworkManager.service.in \
	server.conf.in

CLEANFILES = \
	NetworkManager.service \
	NetworkManager-wait-online.service \
	NetworkManager-dispatcher.service \
	org.freedesktop.NetworkManager.service \
	server.conf

