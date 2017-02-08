## Process this file with automake to produce Makefile.in

AM_CPPFLAGS = -DDATA_DIR=\""$(datadir)"\" \
    -DPACKAGE_LOCALE_DIR=\""$(datadir)/locale/"\" \
    -DPACKAGE_BIN_DIR=\""$(bindir)"\" \
    -DG_LOG_DOMAIN=\""Audio Recorder"\" \
    -DPIXMAPS_DIR=\""$(datadir)/pixmaps/$(PACKAGE)/"\" \
    -DDATADIR=\""$(datadir)"\" \
    -DPACKAGE_DATA_DIR=\""$(pkgdatadir)"\" \
    -DSYSCONFDIR=\""$(sysconfdir)"\" \
    -DPREFIX=\""$(prefix)"\" \
    -I$(top_srcdir) \
    $(GLIB_CFLAGS) $(GIO_CFLAGS) $(GDK_CFLAGS) $(GTK_CFLAGS) \
    $(GSTREAMER_CFLAGS) $(GSTREAMER_PBUTILS_CFLAGS) $(GTHREAD_CFLAGS) \
    $(DBUS_CFLAGS) $(APP_INDICATOR_CFLAGS)

LIBS = $(GLIB_LIBS) $(GIO_LIBS) $(GDK_LIBS) $(GTK_LIBS) \
       $(GSTREAMER_LIBS) $(GSTREAMER_PBUTILS_LIBS) $(GTHREAD_LIBS) \
       $(DBUS_LIBS) $(APP_INDICATOR_LIBS) $(MATH_LIB)

bin_PROGRAMS = audio-recorder

audio_recorder_SOURCES = rec-window.h config.h \
    systray-icon.c \
    auto-start.c auto-start.h \
    help.c help.h \
    audio-sources.c audio-sources.h \
    dbus-server.c dbus-server.h \
    dbus-mpris2.c dbus-mpris2.h \
    dbus-player.c dbus-player.h \
    dbus-skype.c dbus-skype.h \
    dconf.c dconf.h \
    gst-pipeline.c gst-pipeline.h \
    gst-vad.c gst-vad.h \
    gst-recorder.c gst-recorder.h \
    log.c log.h \
    media-profiles.c media-profiles.h \
    gst-devices.c gst-devices.h \
    rec-manager.c rec-manager.h rec-manager-struct.h \
    support.c support.h \
    timer.c timer.h \
	timer-parser.c \
    utility.c utility.h \
    settings.c settings-pipe.c settings.h \
    about.c about.h \
    levelbar.c levelbar.h \
    main.c



