CC = gcc
CXX = $(shell command -v ccache >/dev/null 2>&1 && echo "ccache g++" || echo "g++")
CFLAGS = -g -O0 -Wall -m64 -fPIC -pipe
CXXFLAG = -std=c++11 -g -O0 -Wall -fPIC -pipe -Wno-unused-function -Wno-noexcept-type -m64 -D_GNU_SOURCE=1 -D_REENTRANT
CURRENT_DIR = $(notdir $(shell pwd))

VPATH = .
DIRS := $(foreach dir, $(VPATH), $(shell find $(dir) -maxdepth 5 -type d))
CORE_PATH = ../../../src/core

INC := $(INC) \
        -I . \
        -I $(CORE_PATH)

LDFLAGS := $(LDFLAGS) -D_LINUX_OS_ \
        -L $(CORE_PATH)/libco/lib \
        -L /usr/local/lib \
        -lcolib -lmysqlclient -lpthread -ldl \
        -lcryptopp -lprotobuf -lhiredis

# cur dir objs.
CPP_SRCS = $(foreach dir, $(DIRS), $(wildcard $(dir)/*.cpp))
CC_SRCS = $(foreach dir, $(DIRS), $(wildcard $(dir)/*.cc))
C_SRCS = $(foreach dir, $(DIRS), $(wildcard $(dir)/*.c))
OBJS = $(patsubst %.cpp,%.o,$(CPP_SRCS)) $(patsubst %.c,%.o,$(C_SRCS)) $(patsubst %.cc,%.o,$(CC_SRCS))

# core objs.
CORE_PATH_SRC = $(foreach dir, $(CORE_PATH), $(shell find $(dir) -maxdepth 5 -type d))
CORE_CPP_SRCS = $(foreach dir, $(CORE_PATH_SRC), $(wildcard $(dir)/*.cpp))
CORE_CC_SRCS = $(foreach dir, $(CORE_PATH_SRC), $(wildcard $(dir)/*.cc))
CORE_C_SRCS = $(foreach dir, $(CORE_PATH_SRC), $(wildcard $(dir)/*.c))
_CORE_OBJS = $(patsubst %.cpp,%.o,$(CORE_CPP_SRCS)) $(patsubst %.c,%.o,$(CORE_C_SRCS)) $(patsubst %.cc,%.o,$(CORE_CC_SRCS))
CORE_OBJS = $(filter-out $(CORE_PATH)/server.o, $(_CORE_OBJS)) 

PROC_NAME = $(CURRENT_DIR)

all: PRINT $(PROC_NAME)

PRINT:
	@echo "-------$(PROC_NAME)-------"

$(PROC_NAME): $(OBJS) $(CORE_OBJS)
	$(SERVER_LD) -o $@ $^

%.o:%.c
	$(SERVER_CC) -c -o $@ $<
%.o:%.cc
	$(SERVER_CPP) -c -o $@ $<
%.o:%.cpp
	$(SERVER_CPP) -c -o $@ $<

.PHONY: clean

clean:
	rm -f $(PROC_NAME) $(OBJS)

# build format.
CCCOLOR="\033[34m"
LINKCOLOR="\033[34;1m"
SRCCOLOR="\033[33m"
BINCOLOR="\033[37;1m"
ENDCOLOR="\033[0m"
QUIET_CC = @printf '    %b %b\n' $(CCCOLOR)GCC$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_CPP = @printf '    %b %b\n' $(CCCOLOR)CXX$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_LINK = @printf '    %b %b\n' $(LINKCOLOR)LINK$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR) 1>&2;

SERVER_CC = $(QUIET_CC) $(CC) $(CFLAGS) $(INC)
SERVER_LD = $(QUIET_LINK) $(CXX) $(CXXFLAG) $(INC) $(LDFLAGS)
SERVER_CPP = $(QUIET_CPP) $(CXX) $(CXXFLAG) $(INC)