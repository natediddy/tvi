CC = gcc
CFLAGS = -Wall
LIBS = -lcurl

prefix = /usr/local

ifneq ($(debug),)
	CFLAGS += -g -DDEBUG
else
	CFLAGS += -O2 -march=native -mtune=native
endif

TARGET = ep

SOURCES = ep.c
OBJECTS = ep.o

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(TARGET) $(LIBS)

ep.o:
	$(CC) $(CFLAGS) -c ep.c

clean:
	@rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install $(TARGET) $(prefix)/bin/$(TARGET)

uninstall:
	rm -f $(prefix)/bin/$(TARGET)
