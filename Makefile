CC       ?= cc
CFLAGS   ?= -std=c11 -Wall -Wextra -Wpedantic -g -O0
CPPFLAGS := -Isrc
TARGET   := kestrel

SRCDIR   := src
BUILDDIR := build

SRCS     := $(wildcard $(SRCDIR)/*.c)
OBJS     := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

.PHONY: all clean test sanitize help

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

test: $(TARGET)
	./tests/run_tests.sh

sanitize: clean
	@$(MAKE) CFLAGS="$(CFLAGS) -fsanitize=address,undefined" test

clean:
	rm -rf $(TARGET) $(TARGET).dSYM $(BUILDDIR) *.o src/*.o

help:
	@echo "Available targets:"
	@echo "  all       Build $(TARGET) binary (default)"
	@echo "  test      Build and run automated test suite"
	@echo "  sanitize  Build and test with AddressSanitizer and UBSan"
	@echo "  clean     Remove build artifacts and binaries"
	@echo "  help      Show this help message"