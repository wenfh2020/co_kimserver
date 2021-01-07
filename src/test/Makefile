ROOT = .
TEST_DIRS = $(shell find $(ROOT) -name 'test*' -type d)

.PHONY : subdirs

subdirs:
	@for dir in $(TEST_DIRS); do (cd $$dir && make -f Makefile  CFLAG='$(CFLAG)') ; done

clean:
	@for dir in $(TEST_DIRS); do (cd $$dir && make clean -f Makefile CFLAG='$(CFLAG)') ; done