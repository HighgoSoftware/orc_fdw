#-------------------------------------------------------------------------
#
# Makefile
#    Mailefile for building ORC FDW
#
# 2020, Hamid Quddus Akhtar.
#
#    This file uses pg_config/pgxs to build ORC FDW.
#
#    Running REGRESSION: 
#    - Set "ORC_FDW_DIR" to ORC FDW source folder
#
# Copyright (c) 2020, Highgo Software Inc.
#
# IDENTIFICATION
#    Makefile
#
#-------------------------------------------------------------------------

MODULE_big = orc_fdw
PGFILEDESC = "orc_fdw - foreign data wrapper for Apache ORC"

# Makefile variables
FDW_SRC_DIR := ${CURDIR}

EXTENSION = orc_fdw
OBJS = src/orc_interface.o src/orc_deparse.o src/orc_wrapper.o src/orc_fdw.o
DATA = orc_fdw--1.0.0.sql
REGRESS = create_table import_schema misc select
EXTRA_CLEAN = src/*.gcda src/*.gcno

PG_CPPFLAGS = -Iinclude
SHLIB_LINK = -lm -lstdc++
SHLIB_LINK += -L${FDW_SRC_DIR}/lib -lorc
SHLIB_LINK += -Wl,-rpath '${FDW_SRC_DIR}/lib'

PG_CONFIG ?= pg_config

# orc_impl.cpp requires C++ 11.
override PG_CXXFLAGS += -std=c++11 -O3 -Iinclude

PGXS := $(shell $(PG_CONFIG) --pgxs)

# Pass CCFLAGS (when defined) to both C and C++ compilers.
ifdef CCFLAGS
	override PG_CXXFLAGS += $(CCFLAGS)
	override PG_CFLAGS += $(CCFLAGS)
endif

include $(PGXS)

# XXX: Need to explicitly pass -fPIC (or equivalent) to C++ as servers
# prior to 11 do not automatically pass it
ifeq ($(shell test $(VERSION_NUM) -lt 110000; echo $$?), 0)
	override CXXFLAGS += $(CFLAGS_SL)
endif

# XXX: src/Makefile.global omits passng CXX and CPP FLAGS when building
# bytecode from C++. Let's pass these here
%.bc : %.cpp
	$(COMPILE.cxx.bc) $(CXXFLAGS) $(CPPFLAGS)  -o $@ $<
