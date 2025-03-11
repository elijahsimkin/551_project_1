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

bool instr_not_null(char* instr) {
	return *instr != '\0';
}

int main () {
	
	char* instr = (char*)malloc(100);

	print_startup_message();

	do {
		if (*instr != '\0') 
			printf("You entered: %s\n", instr);
		printf("§ ");
		scanf("%s", instr);		
	} while (strcmp(instr, "exit") != 0);
	
	return 0;
}