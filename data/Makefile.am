#@INTLTOOL_SERVER_RULE@

@INTLTOOL_DESKTOP_RULE@

# Install from data/ folder
thisdatadir = $(pkgdatadir)
thisdata_DATA = *.html \
                *.png \
                COPYING

# Install audio-recorder.desktop file
desktopdir = $(datadir)/applications
dist_desktop_DATA = \
    audio-recorder.desktop

# Install man page
man_MANS = audio-recorder.1

# Makefile rules for schema handling (see data/org.gnome.audio-recorder.gschema.xml and src/dconf.c)
@INTLTOOL_XML_NOMERGE_RULE@
gsettings_SCHEMAS = org.gnome.audio-recorder.gschema.xml
@GSETTINGS_RULES@

EXTRA_DIST = $(thisdata_DATA) \
    audio-recorder.desktop \
    $(gsettings_SCHEMAS)

CLEANFILES = $(gsettings_SCHEMAS).in

DISTCLEANFILES = $(gsettings_SCHEMAS).in
