#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int bool;
#define true 1
#define false 0

void print_startup_message() {
	printf("SSSShell v0.1\n");
	printf("Type 'exit' to exit\n");
}

bool is_instr_null(char* instr) {
	// we presume that if the first character
	// in the instruction is a null character
	// that the instruction is null in entirety
	return *instr == '\0';
}

int main () {
	
	char* instr = (char*)malloc(100);

	print_startup_message();

	do {
		if (!is_instr_null(instr)) 
			printf("You entered: %s\n", instr);
		printf("§ ");

		scanf("%s", instr); // scan the instruction for the next go around
	} while (strcmp(instr, "exit") != 0);
	
	return 0;
}