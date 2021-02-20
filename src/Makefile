ROOT = .
DIRS = ./core ./modules ./test

.PHONY : subdirs clean

subdirs:
	@for dir in $(DIRS); do (cd $$dir && make -f Makefile) ; done

clean:
	@for dir in $(DIRS); do (cd $$dir && make clean -f Makefile) ; done