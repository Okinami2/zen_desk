THIS_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
TOP_DIR  := $(abspath $(THIS_DIR)/..)

PARAM_FILE ?= $(TOP_DIR)/Makefile.param
include $(PARAM_FILE)

OUT_DIR := $(TOP_DIR)/out
LIB_DIR := $(OUT_DIR)/lib
OBJ_DIR := $(OUT_DIR)/obj/common

TARGET := $(LIB_DIR)/libcommon.a

SRC_DIR := $(THIS_DIR)/src
INC_DIR := $(THIS_DIR)/include

COMM_SRC := $(wildcard $(SRC_DIR)/*.c)
COMM_OBJ := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(COMM_SRC))
COMM_DEP := $(COMM_OBJ:.o=.d)

COMM_INC := -I$(INC_DIR)

.PHONY: all clean dirs

all: dirs $(TARGET)
	@echo "~~~~~~~~~~ Build common SUCCESS ~~~~~~~~~~"

dirs:
	@mkdir -p $(LIB_DIR) $(OBJ_DIR)

$(TARGET): $(COMM_OBJ)
	@$(AR) rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "[CC] $<"
	@$(CC) $(COMM_INC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	@rm -rf $(OBJ_DIR) $(TARGET)
	@echo "~~~~~~~~~~ Clean common SUCCESS ~~~~~~~~~~"

-include $(COMM_DEP)