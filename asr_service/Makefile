ifeq ($(PARAM_FILE),)
PARAM_FILE := ../../Makefile.param
include $(PARAM_FILE)
endif

SRC_DIR := src
INC_DIR := include

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:%.c=%.o)

OUT_DIR ?= ../out
BIN_DIR ?= $(OUT_DIR)/bin

TARGET := $(BIN_DIR)/asr_service

CFLAGS += -I$(INC_DIR) -I../common/include
LDFLAGS := $(filter-out -lc++,$(LDFLAGS))
CFLAGS := $(filter-out -lc++,$(CFLAGS))

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(filter-out -lc++,$(CFLAGS)) -o $@ $^ ../common/src/logger.o ../common/src/config.o $(filter-out -lc++,$(LDFLAGS))

%.o: %.c
	$(CC) $(filter-out -lc++,$(CFLAGS)) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
