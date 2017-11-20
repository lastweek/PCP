CFLAGS := -O2

SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)

all: $(OBJS)

clean:
	rm -f *.o

%.o: %.c
	gcc -g -o $@ $(CFLAGS) $<
