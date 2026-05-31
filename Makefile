ifeq ($(PARAM_FILE),)
PARAM_FILE := ../Makefile.param
include $(PARAM_FILE)
endif


OUT_DIR := $(CURDIR)/out
BIN_DIR := $(OUT_DIR)/bin
LIB_DIR := $(OUT_DIR)/lib
OBJ_DIR := $(OUT_DIR)/obj

APPS := vision_service radar_service fusion_service device_service vo_init
TARGETS := common $(APPS)

APPS_CLEAN := $(addsuffix _clean,$(APPS))
TARGETS_CLEAN := common_clean $(APPS_CLEAN)

.PHONY: all dirs services full clean \
        common common_clean \
        $(APPS) $(APPS_CLEAN) \
        qt qt_clean

all: dirs services
	@echo "~~~~~~~~~~~~~~ Build Services SUCCESS ~~~~~~~~~~~~~~"

dirs:
	@mkdir -p $(BIN_DIR) $(LIB_DIR) $(OBJ_DIR)

services: common $(APPS)

full: dirs services qt
	@echo "~~~~~~~~~~~~~~ Build Full Project SUCCESS ~~~~~~~~~~~~~~"

clean: common_clean $(APPS_CLEAN)
	@rm -rf $(OUT_DIR)
	@echo "~~~~~~~~~~~~~~ Clean Services SUCCESS ~~~~~~~~~~~~~~"

common: dirs
	@echo "~~~~~~~~~~ Start build $@ ~~~~~~~~~~"
	@$(MAKE) -C common

common_clean:
	@echo "~~~~~~~~~~ Start clean common ~~~~~~~~~~"
	@$(MAKE) -C common clean

$(APPS): common
	@echo "~~~~~~~~~~ Start build $@ ~~~~~~~~~~"
	@$(MAKE) -C $@

$(APPS_CLEAN):
	@echo "~~~~~~~~~~ Start clean $(subst _clean,,$@) ~~~~~~~~~~"
	@$(MAKE) -C $(subst _clean,,$@) clean

qt:
	@echo "~~~~~~~~~~ Start build qt_client ~~~~~~~~~~"
	@$(MAKE) -C qt_client

qt_clean:
	@echo "~~~~~~~~~~ Start clean qt_client ~~~~~~~~~~"
	@$(MAKE) -C qt_client clean