CC = gcc
CXX=g++
CXX_FLAGS += -std=c++17 -O3 -g -Wall -no-pie -Wl,-E -rdynamic
LDFLAGS = -no-pie -Wl,-E -lpthread -levent -lm
INCLUDES = -I/usr/include -I/usr/local/include -I. \
    -I/home/ubuntu/NMAcc/software/application/rpc_memcached/baseline/memcached \
    -I/home/ubuntu/NMAcc/software/application/rpc_memcached/baseline/memcached/include

OUTPUT_SUFFIX=.hw
GEM5_SUFFIX=.m5
CEREBELLUM_SUFFIX=.crb

MEMCACHED_BASE = /home/ubuntu/NMAcc/software/application/rpc_memcached/baseline/memcached

MEMCACHED_SRCS = \
    $(MEMCACHED_BASE)/memcached.c \
    $(MEMCACHED_BASE)/items.c \
    $(MEMCACHED_BASE)/slabs.c \
    $(MEMCACHED_BASE)/hash.c \
    $(MEMCACHED_BASE)/jenkins_hash.c \
    $(MEMCACHED_BASE)/murmur3_hash.c \
    $(MEMCACHED_BASE)/storage.c \
    $(MEMCACHED_BASE)/assoc.c \
    $(MEMCACHED_BASE)/thread.c \
    $(MEMCACHED_BASE)/util.c \
    $(MEMCACHED_BASE)/logger.c \
    $(MEMCACHED_BASE)/bipbuffer.c \
    $(MEMCACHED_BASE)/crawler.c \
    $(MEMCACHED_BASE)/itoa_ljust.c \
    $(MEMCACHED_BASE)/slab_automove.c \
    $(MEMCACHED_BASE)/stats_prefix.c \
    $(MEMCACHED_BASE)/base64.c \
    $(MEMCACHED_BASE)/cache.c \
    $(MEMCACHED_BASE)/crc32c.c \
    $(MEMCACHED_BASE)/extstore.c \
    $(MEMCACHED_BASE)/authfile.c \
    $(MEMCACHED_BASE)/restart.c \
    $(MEMCACHED_BASE)/proto_bin.c \
    $(MEMCACHED_BASE)/proto_text.c \
    $(MEMCACHED_BASE)/slab_automove_extstore.c

CFLAGS += -DNO_MEMCACHED_MAIN -g
CXX_FLAGS += -DNO_MEMCACHED_MAIN

MEMCACHED_OBJS = $(MEMCACHED_SRCS:.c=.o)

# Add memcached compile flags
MEMCACHED_FLAGS = -DHAVE_CONFIG_H -DNDEBUG

ifeq ($(ENABLE_CEREBELLUM), 1)
  CXX_FLAGS += -lcerebellum -DENABLE_CEREBELLUM=1 -Wfatal-errors
  LDFLAGS   += -lcerebellum -DENABLE_CEREBELLUM=1
  TMP_OUTPUT_SUFFIX = $(OUTPUT_SUFFIX)
  OUTPUT_SUFFIX := $(TMP_OUTPUT_SUFFIX)$(CEREBELLUM_SUFFIX)
endif

ifeq ($(ENABLE_GEM5), 1)
  CXX_FLAGS += -I$(M5OPS_HEADER_PATH) -I$(M5OPS_HEADER_PATH)/../util/m5/src
  CXX_FLAGS += -DENABLE_GEM5=1
  LDFLAGS   += -lm5 -L$(M5_BUILD_PATH)/out/
  LDFLAGS   += -DENABLE_GEM5=1
  TMP_OUTPUT_SUFFIX = $(OUTPUT_SUFFIX)
  OUTPUT_SUFFIX := $(TMP_OUTPUT_SUFFIX)$(GEM5_SUFFIX)
endif

all: memcached_service$(OUTPUT_SUFFIX)

%$(OUTPUT_SUFFIX): %.cpp $(MEMCACHED_OBJS)
	$(CXX) $(CXX_FLAGS) $(INCLUDES) $< $(MEMCACHED_OBJS) -o $@ $(LDFLAGS)

# Add dependency tracking
DEPS = $(MEMCACHED_OBJS:.o=.d)
-include $(DEPS)

# Build memcached objects
%.o: %.c
	$(CC) $(CFLAGS) $(MEMCACHED_FLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f *.hw *$(GEM5_SUFFIX) *$(CEREBELLUM_SUFFIX)
