plugin_LTLIBRARIES = libgstvdpau.la

libgstvdpau_la_SOURCES = \
	gstvdpmpegdec.c \
	mpegutil.c \
	gstvdpau.c \
	gstvdpvideopostprocess.c \
	gstvdpsink.c

libgstvdpau_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(X11_CFLAGS) $(VDPAU_CFLAGS)
libgstvdpau_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(X11_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR) $(VDPAU_LIBS) libgstvdp-@GST_MAJORMINOR@.la
libgstvdpau_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvdpau_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstvdpmpegdec.h \
	mpegutil.h \
	gstvdputils.h \
	gstvdpvideopostprocess.h \
	gstvdpsink.h
	
lib_LTLIBRARIES = libgstvdp-@GST_MAJORMINOR@.la
 
libgstvdp_@GST_MAJORMINOR@_la_SOURCES = \
	gstvdpdevice.c \
	gstvdputils.c \
	gstvdpvideobuffer.c \
	gstvdpoutputbuffer.c \
	gstvdpvideosrcpad.c \
	gstvdpoutputsrcpad.c \
	gstvdp.c 
	
libgstvdp_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/vdpau
libgstvdp_@GST_MAJORMINOR@include_HEADERS = \
	gstvdpdevice.h \
	gstvdpvideobuffer.h \
	gstvdpoutputbuffer.h \
	gstvdpvideosrcpad.h \
	gstvdpoutputsrcpad.h \
	gstvdp.h
 
libgstvdp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
  $(GST_PLUGINS_BASE_CFLAGS) $(X11_CFLAGS) $(VDPAU_CFLAGS)
  
libgstvdp_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(X11_LIBS) $(VDPAU_LIBS) \
  -lgstvideo-$(GST_MAJORMINOR)
  
libgstvdp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_LT_LDFLAGS) $(GST_ALL_LDFLAGS)
libgstvdp_@GST_MAJORMINOR@_la_LIBTOOLFLAGS = --tag=disable-static
	

