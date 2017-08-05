#***************************************************************************
# File makefile
# Date 23.07.17 - #1
# Copyright (c) 2017-2017 s710 (s710 (at) posteo (dot) de). All rights reserved.
# --------------------------------------------------------------------------
# Ark ClusterChat / makefile
#***************************************************************************

#--------------------------------------------------------------------------
# variables
#--------------------------------------------------------------------------

OBJDIR = obj
APPL = $(OBJDIR)/main.o $(OBJDIR)/channel.o $(OBJDIR)/thread.o $(OBJDIR)/rconthread.o $(OBJDIR)/clusterchat.o $(OBJDIR)/ini.o
DISTBIN = arkclusterchat

OPTS=-c -Wreturn-type -Wformat -pedantic -Wunused-variable -Wunused-label -Wunused-value -Wno-long-long -Wno-c++11-compat-deprecated-writable-strings -Wno-deprecated
CXXFLAGS ?= $(OPTS)

#--------------------------------------------------------------------------
# compiler
#--------------------------------------------------------------------------

CC       = g++
doMake       = make
doCompile    = $(CC) $(CXXFLAGS)
doLink       = $(CC)
doClean      = rm *.o

#--------------------------------------------------------------------------
# Do
#--------------------------------------------------------------------------

all: 
	@(echo -; echo Making $(DISTBIN) ...; $(doMake) $(DISTBIN))

$(DISTBIN):  $(APPL)
	@echo Linking "$*" ...
	$(doLink) $(APPL) -o $@ -lpthread

clean:
	@(echo Cleanup of app/$(DISTBIN) ... )
	(rm $(DISTBIN))
	(cd $(OBJDIR) && $(doClean))

#--------------------------------------------------------------------------
# Compiler Call
#--------------------------------------------------------------------------

$(OBJDIR)/%.o: %.c*
	@echo Compile "$(*F)" ...
	@mkdir -p $(@D)
	$(doCompile) $(*F).c* -o $@

#--------------------------------------------------------------------------
# module dependencies
#--------------------------------------------------------------------------

$(OBJDIR)/main.o            :      main.cc
$(OBJDIR)/channel.o         :      channel.cc channel.hpp
$(OBJDIR)/thread.o          :      thread.cc thread.hpp def.h
$(OBJDIR)/rconthread.o      :      rconthread.cc rconthread.hpp thread.hpp def.h
$(OBJDIR)/clusterchat.o     :      clusterchat.cc clusterchat.hpp rconthread.hpp def.h
$(OBJDIR)/ini.o             :      ini.c ini.h
