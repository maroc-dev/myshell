CC = gcc
CFLAGS = -std=c99 -Wall -Wvla -fsanitize=address,undefined -g

TARGETS = myshell

myshell: myshell.o
	$(CC) $(CFLAGS) myshell.o -o myshell

myshell.o: myshell.c
	$(CC) $(CFLAGS) -c myshell.c 

run: myshell
	./myshell

clean:
	rm -f *.o $(TARGETS)
	rm -f *.out $(TARGETS)


