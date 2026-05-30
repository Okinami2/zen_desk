THIS_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TOP_DIR  := $(abspath $(THIS_DIR)/..)

PARAM_FILE ?= $(TOP_DIR)/Makefile.param
include $(PARAM_FILE)

TARGET := fusion_service

OUT_DIR := $(TOP_DIR)/out
BIN_DIR := $(OUT_DIR)/bin
LIB_DIR := $(OUT_DIR)/lib

TARGET_PATH := $(BIN_DIR)
COMMON_LIB := $(LIB_DIR)/libcommon.a

SRCS := $(wildcard $(THIS_DIR)/src/*.c)

# 临时把 device_service 的实现一起编进来，解决当前链接依赖
DEVICE_SERVICE_SRC := $(TOP_DIR)/device_service/src/device_service.c
EC11_SRC := $(TOP_DIR)/device_service/src/ec11.c

OBJS := $(SRCS:%.c=%.o)
DEVICE_SERVICE_OBJ := $(DEVICE_SERVICE_SRC:.c=.o)
EC11_OBJ := $(EC11_SRC:.c=.o)

CFLAGS += -I$(THIS_DIR)/include
CFLAGS += -I$(TOP_DIR)/common/include
CFLAGS += -I$(TOP_DIR)/device_service/include

LDLIBS += $(COMMON_LIB) -lpthread -lm -ldl

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(COMMON_LIB) $(OBJS) $(DEVICE_SERVICE_OBJ) $(EC11_OBJ)
	@mkdir -p $(TARGET_PATH)
	$(CC) $(OBJS) $(DEVICE_SERVICE_OBJ) $(EC11_OBJ) -o $(TARGET_PATH)/$@ $(LDLIBS)

clean:
	@rm -f $(TARGET_PATH)/$(TARGET)
	@rm -f $(OBJS)
	@rm -f $(DEVICE_SERVICE_OBJ)
	@rm -f $(EC11_OBJ)