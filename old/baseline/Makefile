CXX = g++
CXXFLAGS = -std=c++17 -O3 -g -Wall -no-pie -Wl,-E -rdynamic
INCLUDES = -I/usr/include -I/usr/local/include -I.

# DPDK flags
DPDK_FLAGS := $(shell pkg-config --cflags libdpdk)
DPDK_LIBS := $(shell pkg-config --libs libdpdk)

# Combined flags
CXXFLAGS += $(DPDK_FLAGS)
LDFLAGS = -L/usr/local/lib -lmemcached -lthrift $(DPDK_LIBS)

OUTPUT_SUFFIX = .hw
GEM5_SUFFIX = .m5

ifeq ($(ENABLE_GEM5), 1)
  CXXFLAGS += -I$(M5OPS_HEADER_PATH) -I$(M5OPS_HEADER_PATH)/../util/m5/src -DENABLE_GEM5=1
  LDFLAGS += -lm5 -L$(M5_BUILD_PATH)/out/
  OUTPUT_SUFFIX := $(OUTPUT_SUFFIX)$(GEM5_SUFFIX)
endif

all: memcached_service$(OUTPUT_SUFFIX)

%$(OUTPUT_SUFFIX): %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LDFLAGS)

clean:
	rm -f memcached_service memcached_service.hw memcached_service*$(GEM5_SUFFIX)
	rm -f *.o *~

.PHONY: all clean
