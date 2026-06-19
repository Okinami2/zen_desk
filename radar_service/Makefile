THIS_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TOP_DIR  := $(abspath $(THIS_DIR)/..)

PARAM_FILE ?= $(TOP_DIR)/Makefile.param
include $(PARAM_FILE)

TARGET := radar_service
COLLECT_TARGET := radar_collect

OUT_DIR := $(TOP_DIR)/out
BIN_DIR := $(OUT_DIR)/bin
LIB_DIR := $(OUT_DIR)/lib

TARGET_PATH := $(BIN_DIR)
COMMON_LIB := $(LIB_DIR)/libcommon.a

SRCS := $(wildcard $(THIS_DIR)/src/*.c)
OBJS := $(SRCS:%.c=%.o)

COLLECT_SRCS := $(THIS_DIR)/tools/radar_collect.c $(THIS_DIR)/src/radar_protocol.c $(THIS_DIR)/src/hi_uart.c
COLLECT_OBJS := $(COLLECT_SRCS:%.c=%.o)

CFLAGS += -I$(THIS_DIR)/include
CFLAGS += -I$(TOP_DIR)/common/include

LDLIBS += $(COMMON_LIB) -lpthread -lm -ldl

.PHONY: all clean

all: $(TARGET) $(COLLECT_TARGET)

$(TARGET): $(COMMON_LIB) $(OBJS)
	@mkdir -p $(TARGET_PATH)
	$(CC) $(OBJS) -o $(TARGET_PATH)/$@ $(LDLIBS)

$(COLLECT_TARGET): $(COMMON_LIB) $(COLLECT_OBJS)
	@mkdir -p $(TARGET_PATH)
	$(CC) $(COLLECT_OBJS) -o $(TARGET_PATH)/$@ $(LDLIBS)

clean:
	@rm -f $(TARGET_PATH)/$(TARGET)
	@rm -f $(TARGET_PATH)/$(COLLECT_TARGET)
	@rm -f $(OBJS)
	@rm -f $(COLLECT_OBJS)