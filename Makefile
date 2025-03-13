CC = gcc
CFLAGS = -Wall -Wextra -g

sshell: src/shell.c
	$(CC) $(CFLAGS) -o sshell src/shell.c

test: sshell
	python3 src/test.py

clean:
	rm -f sshell