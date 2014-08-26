CC = gcc
CFLAGS = -Wall
LIBS = -lacl -lm
TARGET = copy

prefix = /usr/local

ifeq ($(debug),true)
	CFLAGS += -g -O0 -DDEBUGGING
else
	CFLAGS += -O2 -march=native -mtune=native
endif

SOURCES = \
	copy.c

OBJECTS = \
	copy.o

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET) $(LIBS)

copy.o: copy.c
	$(CC) $(CFLAGS) -c copy.c

clean:
	rm -f $(OBJECTS)

clobber:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install $(TARGET) $(prefix)/bin/$(TARGET)

uninstall:
	rm -f $(prefix)/bin/$(TARGET)

.PHONY: clean clobber install uninstall
