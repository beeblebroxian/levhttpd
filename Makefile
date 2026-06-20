```makefile
CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra

TARGET = levhttpd
SRC = levhttpd.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

strip: $(TARGET)
	strip $(TARGET)

clean:
	rm -f $(TARGET)

install:
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

.PHONY: all strip clean install
```
