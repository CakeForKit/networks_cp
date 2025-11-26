CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE
TARGET = static_server
SOURCES = ./src/server.c

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

debug: $(SOURCES)
	$(CC) $(CFLAGS) -g -o $(TARGET) $(SOURCES)

clean:
	rm -f $(TARGET) *.o server.log
	rm -rf static
	rm -f index.html style.css test.txt

.PHONY: clean debug