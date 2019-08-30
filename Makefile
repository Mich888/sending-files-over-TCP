TARGET: netstore-client netstore-server

CC	= cc
CFLAGS	= -Wall -O2
LFLAGS	= -Wall

netstore-server: serwer.o err.o
	$(CC) $(LFLAGS) $^ -o $@

netstore-client: klient.o err.o
	$(CC) $(LFLAGS) $^ -o $@

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

.PHONY: clean TARGET
clean:
	rm -f netstore-server netstore-client klient.o serwer.o err.o
