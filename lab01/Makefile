CC = gcc
CFLAGS = -g -Wall

default: my_cat

my_cat: my_cat.o
	$(CC) $(CFLAGS) -o my_cat my_cat.o

my_cat.o: my_cat.c
	$(CC) $(CFLAGS) -c my_cat.c

clean:
	$(RM) my_cat my_cat.exe *.o *~
