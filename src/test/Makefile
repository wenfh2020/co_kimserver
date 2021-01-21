ROOT = .
TEST_DIRS = $(shell find $(ROOT) -name 'test*' -type d)

.PHONY : subdirs clean

subdirs:
	@for dir in $(TEST_DIRS); do (cd $$dir && make -f Makefile) ; done

clean:
	@for dir in $(TEST_DIRS); do (cd $$dir && make clean -f Makefile) ; done