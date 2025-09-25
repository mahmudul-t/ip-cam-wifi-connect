# ========================= Makefile =========================
# Toolchain prefix (hard-coded)
TOOLCHAIN := /home/mahmudul/mahmudul_workspace/keo_cam_ingenic/toolchain/mips-gcc540-glibc222-64bit-r3.3.0.smaller/bin
CROSS     := $(TOOLCHAIN)/mips-linux-gnu-

# Project settings
TARGET    := orchestrator
BUILD_DIR := build
SRC_DIR   := src

SRCS      := \
  $(SRC_DIR)/orchestrator_main.c \
  $(SRC_DIR)/gpio_btn.c \
  $(SRC_DIR)/proc.c \
  $(SRC_DIR)/net.c \
  $(SRC_DIR)/tiny_server.c \
  $(SRC_DIR)/flow.c

OBJS      := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

CC        := $(CROSS)gcc
STRIP     := $(CROSS)strip

CFLAGS    := -O2 -Wall -Wextra -I$(SRC_DIR) -D_GNU_SOURCE \
             -ffunction-sections -fdata-sections
LDFLAGS   := -Wl,--gc-sections
LDLIBS    := -lpthread

ifdef STATIC
  LDFLAGS += -static
endif

# Default rule
all: $(BUILD_DIR)/$(TARGET)

# Link target
$(BUILD_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)
	@echo "Built $@ with $(CC)"
	@$(STRIP) $@

# Compile .c -> .o into build dir
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure build directory exists
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
