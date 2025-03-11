CC = gcc
CFLAGS = -Wall -Wextra -g
SRC = $(wildcard src/*.c)  # Find all .c files in src/
OBJ = $(patsubst src/%.c, src/%.o, $(SRC))  # Convert src/*.c to src/*.o

myshell: $(OBJ)
	$(CC) $(CFLAGS) -o myshell $(OBJ)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f myshell $(OBJ)