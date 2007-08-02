NULL =

INCLUDES =					\
	-DG_LOG_DOMAIN="\"GConf-Cleaner\""	\
	$(GCLEANER_CFLAGS)			\
	$(NULL)
LIBS =						\
	@LDFLAGS@				\
	$(GCLEANER_LIBS)			\
	$(NULL)

bin_PROGRAMS =					\
	gconf-cleaner				\
	$(NULL)

gconf_cleaner_SOURCES =				\
	gconf-cleaner.c				\
	gconf-cleaner.h				\
	main.c					\
	$(NULL)

desktopdir = $(datadir)/applications
desktop_in_files = gconf-cleaner.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST =					\
	$(desktop_in_files)			\
	$(NULL)
