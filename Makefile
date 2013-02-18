CFLAGS=-I/usr/include/mysql

all: datacheck.so data rd soa

datacheck.so: datacheck.c Makefile
	gcc -Wall -fPIC $(CFLAGS) -shared -o datacheck.so datacheck.c -lcdb -lpcre
	sudo install -m755 datacheck.so /usr/lib/mysql/plugin/datacheck.so

rd: rd.c
	$(CC) $(CFLAGS) -o rd rd.c $(LDFLAGS) -lcdb -lpcre

data: data.cdb

data.cdb: example.in
	cdb -cm -t data.temp data.cdb < example.in

