bin_PROGRAMS = copy

copy_SOURCES = \
	copy.c \
	copy-checksum.c \
	copy-progress.c \
	copy-utils.c

EXTRA_DIST = README.md

if ENABLE_SOUND
soundfile = complete.oga
sounddir = $(pkgdatadir)/sounds
copy_CFLAGS = \
	-DSOUNDSDIR="\"$(sounddir)\"" \
	-DSOUNDFILE="\"$(soundfile)\""
sound_DATA = $(soundfile)
EXTRA_DIST += $(sound_DATA)
endif

dist_noinst_SCRIPTS = autogen.sh
