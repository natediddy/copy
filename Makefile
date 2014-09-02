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
	main.c \
	checksum.c \
	progress.c \
	utils.c

OBJECTS = \
	main.o \
	checksum.o \
	progress.o \
	utils.o

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET) $(LIBS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

checksum.o: checksum.c
	$(CC) $(CFLAGS) -c checksum.c

progress.o: progress.c
	$(CC) $(CFLAGS) -c progress.c

utils.o: utils.c
	$(CC) $(CFLAGS) -c utils.c

clean:
	rm -f $(OBJECTS)

clobber:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install $(TARGET) $(prefix)/bin/$(TARGET)

uninstall:
	rm -f $(prefix)/bin/$(TARGET)

.PHONY: clean clobber install uninstall
