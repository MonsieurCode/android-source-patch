# Copyright 2006 The Android Open Source Project


# XXX using libutils for simulator build only...
#
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    altair-ril.c \
    atchannel.c \
    SMS3GPP2.c \
    altair_at_socket.c \
    misc.c \
    at_tok.c

LOCAL_SHARED_LIBRARIES := \
    libcutils libutils libril libnetutils

# for asprinf
LOCAL_CFLAGS := -D_GNU_SOURCE

# emulate RIL for android 4.2
# for android 4.2 , PLATFORM_SDK_VERSION == 17
# for android 4.3 , PLATFORM_SDK_VERSION == 18
ifeq ($(PLATFORM_SDK_VERSION),17)
	LOCAL_CFLAGS += -DRIL_EMULATE_4_2
else ifeq ($(PLATFORM_SDK_VERSION),18)
	LOCAL_SHARED_LIBRARIES += \
         liblog librilutils 
    LOCAL_CFLAGS += -DRIL_EMULATE_4_3
else ifeq ($(PLATFORM_SDK_VERSION),19)
    LOCAL_SHARED_LIBRARIES += \
         liblog librilutils 
else 
	$(error unsupported PLATFORM_SDK_VERSION )
endif       


LOCAL_C_INCLUDES := $(KERNEL_HEADERS)

ifeq ($(TARGET_DEVICE),sooner)
  LOCAL_CFLAGS += -DOMAP_CSMI_POWER_CONTROL -DUSE_TI_COMMANDS
endif

ifeq ($(TARGET_DEVICE),surf)
  LOCAL_CFLAGS += -DPOLL_CALL_STATE -DUSE_QMI
endif

ifeq ($(TARGET_DEVICE),dream)
  LOCAL_CFLAGS += -DPOLL_CALL_STATE -DUSE_QMI
endif





# features supported
#LOCAL_CFLAGS += -DRIL_FEATURE_ENABLE_VOLTE




ifeq (foo,foo)
  #build shared library
  LOCAL_SHARED_LIBRARIES += \
      libcutils libutils
  LOCAL_LDLIBS += -lpthread
  LOCAL_CFLAGS += -DRIL_SHLIB
  LOCAL_MODULE:= libaltair-ril
  include $(BUILD_SHARED_LIBRARY)
else
  #build executable
  LOCAL_SHARED_LIBRARIES += \
      libril
  LOCAL_MODULE:= altair-ril
  include $(BUILD_EXECUTABLE)
endif


