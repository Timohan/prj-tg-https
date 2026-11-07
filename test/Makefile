#/*!
#* \file Makefile
#* \brief Makefile for compiling
#*
#* Copyright of Timo hannukkala, Inc. All rights reserved.
#*
#* \author Timo Hannukkala <timohannukkala@hotmail.com>
#*/
CURRENT_DIR:=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

TARGET:=$(CURRENT_DIR)prj-https-test

# compiler and linker flags of the installed library (run pkg-config only once)
ifneq ($(MAKECMDGOALS),clean)
PKG_CFLAGS:=$(shell pkg-config --cflags prj-tg-https-lib)
PKG_LIBS:=$(shell pkg-config --libs prj-tg-https-lib)
ifeq ($(PKG_LIBS),)
$(error prj-tg-https-lib not found by pkg-config, run "make install" in ../lib first or set PKG_CONFIG_PATH)
endif
endif

CXXFLAGS+=-g -Wall -pedantic -pipe -std=gnu++17 -W -pthread
CXXFLAGS+=-I$(CURRENT_DIR)src
# generate .d files so that changing a header rebuilds the objects using it
CXXFLAGS+=-MMD -MP
CXXFLAGS+=$(PKG_CFLAGS)
CXXFLAGS+=-Wuninitialized -Wconversion -Wshadow -Wpointer-arith \
	 -Wswitch-default -Wswitch-enum -Wcast-align \
	 -Winline -Wundef -Wcast-qual -Wunreachable-code -Wlogical-op -Wfloat-equal \
	 -Wredundant-decls -Werror
LDFLAGS+=-pthread
LDLIBS+=$(PKG_LIBS)

src_SRCDIR:=$(CURRENT_DIR)src
src_SRCS:=$(wildcard $(src_SRCDIR)/*.cpp)
src_OBJS:=$(src_SRCS:.cpp=.o)

all: $(TARGET)

default: all

$(TARGET): $(src_OBJS)
	$(CXX) $(LDFLAGS) $(src_OBJS) $(LDLIBS) -o $(TARGET)

$(src_OBJS): %.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET)
	rm -f $(src_OBJS) $(src_OBJS:.o=.d)

-include $(src_OBJS:.o=.d)

.PHONY: all default clean
