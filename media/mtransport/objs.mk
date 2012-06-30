DEFINES += -DHAVE_STRDUP -DNR_SOCKET='void *' \
	$(NULL)

LOCAL_INCLUDES += \
 -I. \
 -I$(topsrcdir)/media/webrtc/trunk/third_party/libjingle/source/ \
 -I$(topsrcdir)/media/mtransport/ \
 -I$(topsrcdir)/media/mtransport/third_party/ \
 -I$(topsrcdir)/media/mtransport/third_party/nICEr/src/crypto \
 -I$(topsrcdir)/media/mtransport/third_party/nICEr/src/ice \
 -I$(topsrcdir)/media/mtransport/third_party/nICEr/src/net \
 -I$(topsrcdir)/media/mtransport/third_party/nICEr/src/stun \
 -I$(topsrcdir)/media/mtransport/third_party/nICEr/src/util \
 -I$(topsrcdir)/media/mtransport/third_party/nrappkit/src/share \
 -I$(topsrcdir)/media/mtransport/third_party/nrappkit/src/util/libekr \
 -I$(topsrcdir)/media/mtransport/third_party/nrappkit/src/log \
 -I$(topsrcdir)/media/mtransport/third_party/nrappkit/src/registry \
 -I$(topsrcdir)/media/mtransport/third_party/nrappkit/src/stats \
 -I$(topsrcdir)/media/mtransport/third_party/nrappkit/src/plugin \
 -I$(topsrcdir)/media/mtransport/third_party/nrappkit/src/event \
 $(NULL)

# TODO: need to expand for other platforms
ifeq ($(OS_ARCH), Darwin)
LOCAL_INCLUDES +=  -I$(topsrcdir)/media/mtransport/third_party/nrappkit/src/port/darwin/include
endif

ifeq ($(OS_ARCH), Linux)
LOCAL_INCLUDES +=  -I$(topsrcdir)/media/mtransport/third_party/nrappkit/src/port/linux/include
endif

MTRANSPORT_LCPPSRCS = \
  dtlsidentity.cpp \
  nricectx.cpp \
  nricemediastream.cpp \
  nr_socket_prsock.cpp \
  nr_timer.cpp \
  transportflow.cpp \
  transportlayer.cpp \
  transportlayerice.cpp \
  transportlayerdtls.cpp \
  transportlayerlog.cpp \
  transportlayerloopback.cpp \
  transportlayerprsock.cpp \
  $(NULL)

MTRANSPORT_CPPSRCS = $(addprefix $(topsrcdir)/media/mtransport/, $(MTRANSPORT_LCPPSRCS))