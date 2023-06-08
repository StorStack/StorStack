all: hmac

hmac: hmac.o sha2.o
	$(CC) sha2.o hmac.o -o hmac

hmac.o: hmac_sha2.c hmac_sha2.h
	$(CC) -Wall -c hmac_sha2.c -o hmac.o # -DTEST_VECTORS 

sha2.o: sha2.c sha2.h
	$(CC) -c sha2.c -o sha2.o

clean:
	- rm -rf *.o hmac
