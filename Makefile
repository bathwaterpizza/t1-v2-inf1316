CC = gcc
CFLAGS = -Wall

# List of all programs
PROGRAMS = kernelsim intersim app

# Common source files
COMMON_SRC = types.c util.c

# Default target
all: $(PROGRAMS)

# Rule for kernelsim
kernelsim: kernelsim.c $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^

# Rule for intersim
intersim: intersim.c $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^

# Rule for app
app: app.c $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^

# Clean up build artifacts
clean:
	rm -f $(PROGRAMS)

# Phony targets
.PHONY: all clean
