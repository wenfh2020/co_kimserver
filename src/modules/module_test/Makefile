CC = gcc
CXX = $(shell command -v ccache >/dev/null 2>&1 && echo "ccache g++" || echo "g++")
CFLAGS = -g -O0 -Wall -m64 -D__GUNC__ -fPIC
CPP_VERSION=$(shell g++ -dumpversion | awk '{if ($$NF > 5.0) print "c++14"; else print "c++11";}')
CXXFLAG = -std=$(CPP_VERSION) -g -O0 -Wall -Wno-unused-function -Wno-noexcept-type -m64 -D_GNU_SOURCE=1 -D_REENTRANT -D__GUNC__ -fPIC -DNODE_BEAT=10.0 -DTHREADED

CORE_PATH = ../../core
VPATH = . $(CORE_PATH)
DIRS := $(foreach dir, $(VPATH), $(shell find $(dir) -maxdepth 5 -type d))

INC := $(INC) \
        -I . \
        -I $(CORE_PATH)

LDFLAGS := $(LDFLAGS) -D_LINUX_OS_ \
        -L $(CORE_PATH)/libco/lib \
        -L /usr/local/lib \
        -lcolib -lmysqlclient -lpthread -ldl -lcryptopp -lprotobuf

# so objs.
SO_PATH = .
SO_PATH_SRC = $(foreach dir, $(SO_PATH), $(shell find $(dir) -maxdepth 5 -type d))
SO_CPP_SRCS = $(foreach dir, $(SO_PATH_SRC), $(wildcard $(dir)/*.cpp))
SO_CC_SRCS = $(foreach dir, $(SO_PATH_SRC), $(wildcard $(dir)/*.cc))
SO_C_SRCS = $(foreach dir, $(SO_PATH_SRC), $(wildcard $(dir)/*.c))
SO_OBJS = $(patsubst %.cpp,%.o,$(SO_CPP_SRCS)) $(patsubst %.c,%.o,$(SO_C_SRCS)) $(patsubst %.cc,%.o,$(SO_CC_SRCS))
SOS = $(patsubst %.o,%.so,$(SO_OBJS))

# core objs.
CORE_PATH_SRC = $(foreach dir, $(CORE_PATH), $(shell find $(dir) -maxdepth 5 -type d))
CORE_CPP_SRCS = $(foreach dir, $(CORE_PATH_SRC), $(wildcard $(dir)/*.cpp))
CORE_CC_SRCS = $(foreach dir, $(CORE_PATH_SRC), $(wildcard $(dir)/*.cc))
CORE_C_SRCS = $(foreach dir, $(CORE_PATH_SRC), $(wildcard $(dir)/*.c))
CORE_OBJS = $(patsubst %.cpp,%.o,$(CORE_CPP_SRCS)) $(patsubst %.c,%.o,$(CORE_C_SRCS)) $(patsubst %.cc,%.o,$(CORE_CC_SRCS))

all: $(SOS)
.PHONY: clean
.SECONDARY: $(SO_OBJS) $(CORE_OBJS)

module%.so: module%.o $(CORE_OBJS)
	$(CXX) $(CXXFLAG) -fPIC -rdynamic -shared -o $@ $^ $(INC) $(LDFLAGS)
	cp $(SOS) ../../../bin/modules

%.o:%.cpp
	$(CXX) $(CXXFLAG) $(INC) -c -o $@ $< 
%.o:%.cc
	$(CXX) $(CXXFLAG) $(INC) -c -o $@ $<
%.o:%.c
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

clean:
	rm -f $(SOS) $(SO_OBJS)