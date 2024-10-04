CC = gcc
CFLAGS = -Wall -lpthread -g

# List of all programs
PROGRAMS = kernelsim intersim app

# Common source files
COMMON_SRC = types.c util.c

# Header files
HEADERS = cfg.h util.h types.h

# Default target
all: $(PROGRAMS)

# Rule for kernelsim
kernelsim: kernelsim.c $(COMMON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ kernelsim.c $(COMMON_SRC)

# Rule for intersim
intersim: intersim.c $(COMMON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ intersim.c $(COMMON_SRC)

# Rule for app
app: app.c $(COMMON_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ app.c $(COMMON_SRC)

# Clean up build artifacts
clean:
	rm -f $(PROGRAMS)

# Phony targets
.PHONY: all clean
