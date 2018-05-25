#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "fat.h"


int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("Incorrect argument number\nUsage: parsefat <file>\n");
		exit(EXIT_FAILURE);
	}
	
	print_console(argv[1]);
	

	exit(EXIT_SUCCESS);
}