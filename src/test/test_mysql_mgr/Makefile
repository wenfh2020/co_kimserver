CC = gcc
CXX = g++
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
        -lcolib -lmysqlclient -lpthread -ldl -lcryptopp -lprotobuf

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

.PHONY: clean

$(PROC_NAME): $(OBJS) $(CORE_OBJS)
	$(CXX) $(CXXFLAG) -o $@ $^ $(INC) $(LDFLAGS)

%.o:%.cpp
	$(CXX) $(CXXFLAG) $(INC) -c -o $@ $<
%.o:%.cc
	$(CXX) $(CXXFLAG) $(INC) -c -o $@ $<
%.o:%.c
	$(CC) $(CFLAGS) $(INC)  -c -o $@ $<
clean:
	rm -f $(PROC_NAME) $(OBJS)
