CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2

TARGET = pscc
SRC = pscc.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)

