AXIS_USABLE_LIBS = UCLIBC GLIBC
include $(AXIS_TOP_DIR)/tools/build/rules/common.mak

PROGS     = axisSocketServer

CFLAGS += -Wall -g -O2
ifeq ($(AXIS_BUILDTYPE),host)
LDFLAGS += -lcapturehost -ljpeg
else
LDFLAGS += -lcapture
endif # AXIS_BUILDTYPE == host

PKGS = glib-2.0 gio-2.0 axisSocketServer

CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LIBDIR) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_LIBDIR) pkg-config --libs $(PKGS))

INSTDIR   = $(prefix)/bin/
INSTMODE  = 0755
INSTOWNER = root
INSTGROUP = root

OBJS = axisSocketServer.o

all:	$(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) $(LDLIBS) -o $@

clean:
	rm -f $(PROGS) *.o core
	rm -f *.tar

