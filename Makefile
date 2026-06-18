BM_ROOT := $(dir $(lastword $(MAKEFILE_LIST)))

CC      ?= gcc
CFLAGS  ?= -Os -Wall -Wextra -std=c99 -I$(BM_ROOT)/include
LDFLAGS ?=

BM_SRCS := \
    $(BM_ROOT)/src/core/bm_event.c \
    $(BM_ROOT)/src/core/bm_mempool.c \
    $(BM_ROOT)/src/core/bm_critical.c

ifeq ($(BM_ENABLE_MODULE),1)
    BM_SRCS += $(BM_ROOT)/src/core/bm_module.c
endif

ifeq ($(BM_ENABLE_WDG),1)
    BM_SRCS += $(BM_ROOT)/src/core/bm_wdg.c
endif

ifeq ($(BM_ENABLE_CHANNEL),1)
    BM_SRCS += $(BM_ROOT)/src/core/bm_channel.c
endif

ifeq ($(BM_ENABLE_SHELL),1)
    BM_SRCS += $(BM_ROOT)/src/core/bm_shell.c
endif

ifdef BM_CONFIG_H
    CFLAGS += -include $(BM_CONFIG_H)
endif

.PHONY: all clean

all:
	@echo "BM_SRCS = $(BM_SRCS)"
	@echo "CFLAGS  = $(CFLAGS)"

clean:
	find . -name '*.o' -delete
	find . -name '*.elf' -delete
