/* 
 * Skeleton code for Lab 1 - Shell processing
 * This file contains skeleton code for executing commands parsed in cmdline.c.
  * Acknowledgement: derived from UCLA CS111
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdarg.h>

#include "cmdline.h"
#include "myshell.h"

/* EXERCISE: Make sure you free memory used by the command_t structures
 * when it is no longer needed.
 * EXERCISE:  Make sure you clean up ALL zombie processes, including those
 * that ran in the background!
 * There are several ways to do this, and several places to put the code.
 * Check out the SIGCHLD signal and the waitpid() system call for hints.
 * HINT:
 *   You DO NOT need to clean up zombies as soon as they are created.
 *   It is OK to clean up zombies as little as once per command.
 *   That is, it is OK if a zombie hangs around until the next time the user
 *   types a command.  But it is NOT OK for zombies to hang around forever.
 */


int main(int argc, char *argv[])
{
	int quiet = 0;
	char input[BUFSIZ];
	int r = 0;

	// Check for '-q' option: be quiet -- print no prompts
	if (argc > 1 && strcmp(argv[1], "-q") == 0)
		quiet = 1;

	while (!feof(stdin)) {
		parsestate_t parsestate;
		command_t *cmdlist;

		if (fgets(input, BUFSIZ, stdin) == NULL) {
			if (ferror(stdin))
				// This function prints a description of the
				// error, preceded by 'cs111_fall07: '.
				perror("prog1");
			break;
		}

		parse_init(&parsestate, input);

		cmdlist = command_line_parse(&parsestate, 0);
		if (!cmdlist) {
			printf("Syntax error\n");
			continue;
		}

		if (!quiet) {
			command_print(cmdlist, 8);
			fflush(stdout);
		}

		if (cmdlist)
			command_line_exec(cmdlist);
	}

	return 0;
}
