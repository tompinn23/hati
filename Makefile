MAKEFLAGS += -rR
.SUFFIXES:

CC := gcc
CFLAGS := -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE=1

lib_cflags := $(shell pkg-config --cflags libcurl)
libs := $(shell pkg-config --libs libcurl)

objects := src/main.o src/dl.o src/log.o src/list.o

.PHONY: all, clean
all: hati

hati: $(addprefix obj/, $(objects))
	$(CC) -o $@ $^ $(LDFLAGS) $(libs)

.PHONY: clean
clean:
	rm -fr obj/*
	rm hati

obj/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) -MMD $(CFLAGS) $(lib_cflags) -c $< -o $@

dependencies := $(patsubst %.o, %.d, $(addprefix obj/, $(objects)))

-include $(dependencies)