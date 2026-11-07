#/*!
#* \file Makefile
#* \brief Makefile for compiling prj-tg-https-lib library
#* and generating pkgconfig file
#* and making install & uninstall
#*
#* Copyright of Timo Hannukkala. All rights reserved.
#*
#* \author Timo Hannukkala <timohannukkala@hotmail.com>
#*/
CURRENT_DIR:=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

TARGET:=$(CURRENT_DIR)libprj-tg-https-lib.so
VERSION:=0.1
PREFIX:=/usr
# DESTDIR: optional staging directory for packaging, e.g. make install DESTDIR=/tmp/pkg
DESTDIR:=
LIBDIR:=$(PREFIX)/lib
INCLUDEDIR:=$(PREFIX)/include
PKGCONFIGDIR:=$(LIBDIR)/pkgconfig
PKGCONFIGFILE:=prj-tg-https-lib

CXXFLAGS+=-Wuninitialized -Wconversion -Wshadow -Wpointer-arith \
	-Wswitch-default -Wswitch-enum -Wcast-align \
	-Wundef -Wcast-qual -Wunreachable-code -Wfloat-equal \
	-Wredundant-decls -Wextra -Wall
CXXFLAGS+=-Werror
CXXFLAGS+=-O2
CXXFLAGS+=-g -fPIC -std=c++17 -pthread
# generate .d files so that changing a header rebuilds the objects using it
CXXFLAGS+=-MMD -MP
LDFLAGS+=-shared -pthread
LDLIBS+=-lssl -lcrypto

src_SRCDIR:=$(CURRENT_DIR)src
src_SRCS:=$(wildcard $(src_SRCDIR)/*.cpp)
src_OBJS:=$(src_SRCS:.cpp=.o)

src_private_SRCDIR:=$(CURRENT_DIR)src/private
src_private_SRCS:=$(wildcard $(src_private_SRCDIR)/*.cpp)
src_private_OBJS:=$(src_private_SRCS:.cpp=.o)

default: $(TARGET)

$(TARGET): $(src_OBJS) $(src_private_OBJS)
	$(CXX) $(LDFLAGS) $(src_OBJS) $(src_private_OBJS) $(LDLIBS) -o $(TARGET)

$(src_OBJS) $(src_private_OBJS): %.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# the .pc file is written on every install, so it always matches PREFIX and VERSION
install: $(TARGET)
	mkdir -p $(DESTDIR)$(LIBDIR)/prj-tg-https-lib
	cp $(TARGET) $(DESTDIR)$(LIBDIR)/prj-tg-https-lib
	mkdir -p $(DESTDIR)$(INCLUDEDIR)/prj-tg-https-lib
	cp $(src_SRCDIR)/prj_tg_https_lib.h $(DESTDIR)$(INCLUDEDIR)/prj-tg-https-lib
	cp $(src_SRCDIR)/prj_https_log.h $(DESTDIR)$(INCLUDEDIR)/prj-tg-https-lib
	mkdir -p $(DESTDIR)$(PKGCONFIGDIR)
	@echo "installing $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc"
	@echo "prefix=$(PREFIX)" > $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc
	@echo "exec_prefix=\$${prefix}" >> $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc
	@echo "libdir=\$${prefix}/lib/prj-tg-https-lib" >> $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc
	@echo "includedir=\$${prefix}/include/prj-tg-https-lib" >> $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc
	@echo "" >> $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc
	@echo "Name: prj-tg-https-lib" >> $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc
	@echo "Description: prj-tg-https-lib lib makes https connection" >> $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc
	@echo "Version: $(VERSION)" >> $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc
	@echo "Cflags: -I\$${includedir}" >> $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc
	@echo "Libs: -L\$${libdir} -lprj-tg-https-lib -Wl,--rpath=\$${libdir}" >> $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc

uninstall:
	rm -rf $(DESTDIR)$(LIBDIR)/prj-tg-https-lib
	rm -rf $(DESTDIR)$(INCLUDEDIR)/prj-tg-https-lib
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/$(PKGCONFIGFILE).pc

clean:
	rm -f $(src_OBJS) $(src_private_OBJS)
	rm -f $(src_OBJS:.o=.d) $(src_private_OBJS:.o=.d)
	rm -f $(TARGET)

-include $(src_OBJS:.o=.d) $(src_private_OBJS:.o=.d)

.PHONY: default install uninstall clean
