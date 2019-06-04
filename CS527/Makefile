SRCDIR   = src
BINDIR   = bin
INCLUDES = include

CC=gcc
CFLAGS=-g -fno-stack-protector -z execstack -lpthread -pthread -std=gnu11 -I $(INCLUDES)/ 

CLIENT=$(SRCDIR)/client.c
SRVSRC=$(SRCDIR)/server.c $(SRCDIR)/server_conf.c
DEPS=$(wildcard $(INCLUDES)/%.h)

all: $(BINDIR)/client $(BINDIR)/server $(DEPS)

$(BINDIR)/client: $(CLIENT)
	$(CC) $(CFLAGS) $(CLIENT) -o $@

$(BINDIR)/server: $(SRVSRC)
	$(CC) $(CFLAGS) $(SRVSRC) -o $@

.PHONY: clean
clean:
	rm -f $(BINDIR)/client $(BINDIR)/server
