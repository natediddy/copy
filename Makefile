CC = gcc
CFLAGS = -Wall
LIBS = -lm
TARGET = copy

prefix = /usr/local

ifeq ($(debug),true)
	CFLAGS += -g -O0 -DDEBUGGING
else
	CFLAGS += -O2 -march=native -mtune=native
endif

SOURCES = \
	copy.c \
	copy-md5.c \
	copy-util.c

OBJECTS = \
	copy.o \
	copy-md5.o \
	copy-util.o

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET) $(LIBS)

copy.o: copy.c
	$(CC) $(CFLAGS) -c copy.c

copy-md5.o: copy-md5.c
	$(CC) $(CFLAGS) -c copy-md5.c

copy-util.o: copy-util.c
	$(CC) $(CFLAGS) -c copy-util.c

clean:
	rm -f $(OBJECTS)

clobber:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install $(TARGET) $(prefix)/bin/$(TARGET)

uninstall:
	rm -f $(prefix)/bin/$(TARGET)

.PHONY: clean clobber install uninstall
