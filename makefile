.SUFFIXES: .c

all:
	gcc client.c -o client

clean:
	/bin/rm -f *.o *~ *.dat core client