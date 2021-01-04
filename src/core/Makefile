CUR_DIR = $(shell pwd)
LIBCO_DIR = $(CUR_DIR)/libco

.PHONY : build

build:
	@echo '---- build: $(LIBCO_DIR) ----'
	cd $(LIBCO_DIR) && make -f Makefile
	@echo '---- build: $(notdir $(CUR_DIR)) ----'
	cd $(CUR_DIR) && make -f Makefile.core

clean:
	@echo '---- clean: $(LIBCO_DIR) ----'
	cd $(LIBCO_DIR) && make clean -f Makefile
	@echo '---- clean: $(notdir $(CUR_DIR)) ----'
	cd $(CUR_DIR) && make clean -f Makefile.core
