THIS_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TOP_DIR  := $(abspath $(THIS_DIR)/..)

PARAM_FILE ?= $(TOP_DIR)/Makefile.param
include $(PARAM_FILE)

include $(TOP_DIR)/smp_linux.mak

TARGET := vision_service

OUT_DIR := $(TOP_DIR)/out
BIN_DIR := $(OUT_DIR)/bin
LIB_DIR := $(OUT_DIR)/lib

TARGET_PATH := $(BIN_DIR)
COMMON_LIB := $(LIB_DIR)/libcommon.a

SMP_SRCS := $(wildcard $(THIS_DIR)/src/*.c)	\
			$(wildcard $(THIS_DIR)/common/*.c)

# target source
OBJS := $(SMP_SRCS:%.c=%.o)

CFLAGS += -I$(REL_INC)/svp_npu
CFLAGS += -fstack-protector-strong
CFLAGS += -I$(THIS_DIR)/include
CFLAGS += -I$(THIS_DIR)/common
CFLAGS += -I$(TOP_DIR)/common/include
CFLAGS += $(COMM_INC)

MPI_LIBS += $(REL_LIB)/svp_npu/libsvp_acl.a
MPI_LIBS += -L$(REL_LIB)/svp_npu -lprotobuf-c
MPI_LIBS += $(REL_LIB)/libss_hdmi.a
MPI_LIBS += $(LIBS_LD_CFLAGS)

.PHONY: all clean cleanstream

all: $(TARGET)

$(TARGET): $(COMMON_LIB) $(OBJS)
	@mkdir -p $(TARGET_PATH)
	$(CC) $(CFLAGS) -o $(TARGET_PATH)/$@ $(OBJS) $(COMMON_LIB) \
		-lpthread -lm \
		-Wl,--start-group $(MPI_LIBS) $(SDK_LIB) $(SENSOR_LIBS) $(INIPARSER_LIB) $(REL_LIB)/libsecurec.a -Wl,--end-group

clean:
	@rm -f $(TARGET_PATH)/$(TARGET)
	@rm -f $(OBJS)

cleanstream:
	@rm -f *.h264
	@rm -f *.h265
	@rm -f *.jpg
	@rm -f *.mjp
	@rm -f *.mp4