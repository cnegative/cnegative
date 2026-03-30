CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -g -Iinclude
LDFLAGS :=

BUILD_DIR := build
TARGET := $(BUILD_DIR)/cnegc
HOST_OS := $(shell uname -s 2>/dev/null || echo Unknown)
HOST_ARCH := $(shell uname -m 2>/dev/null || echo Unknown)

C_SOURCES := $(shell find src -name '*.c' | sort)
ASM_SOURCES :=
ifeq ($(HOST_ARCH),x86_64)
ifeq ($(HOST_OS),Linux)
ASM_SOURCES := $(shell find src -name '*.S' | sort)
endif
ifeq ($(HOST_OS),Darwin)
ASM_SOURCES := $(shell find src -name '*.S' | sort)
endif
endif
ifneq ($(ASM_SOURCES),)
CFLAGS += -DCN_USE_X86_64_ASM_SCAN=1
endif
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES)) $(patsubst src/%.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))

.PHONY: all clean check test net-test udp-test check-lines

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@

check: $(TARGET)

check-lines:
	bash scripts/check-file-lines.sh

test: check-lines $(TARGET)
	bash tests/smoke.sh

net-test: $(TARGET)
	$(TARGET) build examples/valid_stdlib_net_tcp.cneg build/net-main-python
	$(TARGET) build examples/valid_stdlib_net_tcp_server.cneg build/net-server-python
	$(TARGET) build examples/valid_stdlib_net_tcp_client.cneg build/net-client-python
	python3 tests/net_integration.py

udp-test: $(TARGET)
	$(TARGET) build examples/valid_stdlib_net_udp.cneg build/udp-main-python
	$(TARGET) build examples/valid_stdlib_net_udp_server.cneg build/udp-server-python
	$(TARGET) build examples/valid_stdlib_net_udp_client.cneg build/udp-client-python
	python3 tests/net_integration.py --main-binary build/udp-main-python --server-binary build/udp-server-python --client-binary build/udp-client-python

clean:
	rm -rf $(BUILD_DIR)
