CC = gcc
CFLAGS = -std=c99 -Wall -Wvla -fsanitize=address,undefined -g

TARGETS = mysh

myshell: mysh.o
	$(CC) $(CFLAGS) mysh.o -o mysh

myshell.o: mysh.c
	$(CC) $(CFLAGS) -c mysh.c 

run: mysh
	./mysh

clean:
	rm -f *.o $(TARGETS)
	rm -f *.out $(TARGETS)


