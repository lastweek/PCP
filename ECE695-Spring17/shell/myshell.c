/* 
 * Skeleton code for Shell processing
 * This file contains skeleton code for executing commands parsed in main-x.c.
 * Acknowledgement: derived from UCLA CS111
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "cmdline.h"
#include "myshell.h"

/** 
 * Reports the creation of a background job in the following format:
 *  [job_id] process_id
 * to stderr.
 */
void report_background_job(int job_id, int process_id);

/* command_exec(cmd, pass_pipefd)
 *
 *   Execute the single command specified in the 'cmd' command structure.
 *
 *   The 'pass_pipefd' argument is used for pipes.
 *   On input, '*pass_pipefd' is the file descriptor used to read the
 *   previous command's output.  That is, it's the read end of the previous
 *   pipe.  It equals STDIN_FILENO if there was no previous pipe.
 *   On output, command_exec should set '*pass_pipefd' to the file descriptor
 *   used for reading from THIS command's pipe.
 *   If this command didn't have a pipe -- that is, if cmd->commandop != PIPE
 *   -- then it should set '*pass_pipefd = STDIN_FILENO'.
 *
 *   Returns the process ID of the forked child, or < 0 if some system call
 *   fails.
 *
 *   However, these special commands still have a status!
 *   For example, "cd DIR" should return status 0 if we successfully change
 *   to the DIR directory, and status 1 otherwise.
 *   Thus, "cd /tmp && echo /tmp exists" should print "/tmp exists" to stdout
 *   iff the /tmp directory exists.
 *   Not only this, but redirections should work too!
 *   For example, "cd /tmp > foo" should create an empty file named 'foo';
 *   and "cd /tmp 2> foo" should print any error messages to 'foo'.
 *
 *   How can you return a status, and do redirections, for a command executed
 *   in the parent shell?
 *   Hint: It is easiest if you fork a child ANYWAY!
 *   You should divide functionality between the parent and the child.
 *   Some functions will be executed in each process.
 */
static pid_t command_exec(command_t *cmd, int *pass_pipefd)
{
	pid_t pid = -1;
	int fd[2];

	/*
	 * Create a pipe
	 * If this command is the left-hand side of a pipe
	 */
	if (cmd->controlop == CMD_PIPE) {
		if (pipe(fd) < 0)
			die("Fail to create pipe");
	}

	// In the child, you should:
	//    1. Set up stdout to point to this command's pipe, if necessary.
	//    2. Set up stdin to point to the PREVIOUS command's pipe (that
	//       is, *pass_pipefd), if appropriate.
	//    3. Close some file descriptors.  Hint: Consider the read end
	//       of this process's pipe.
	//    4. Set up redirections.
	//       Hint: For output redirections (stdout and stderr), the 'mode'
	//       argument of open() should be set to 0666.
	//    5. Execute the command.
	//       There are some special cases:
	//       a. Parentheses.  Execute cmd->subshell.  (How?)
	//       b. A null command (no subshell, no arguments).
	//          Exit with status 0.
	//       c. "exit".
	//       d. "cd".
	//
	// In the parent, you should:
	//    1. Close some file descriptors.  Hint: Consider the write end
	//       of this command's pipe, and one other fd as well.
	//    2. Handle the special "exit" and "cd" commands.
	//    3. Set *pass_pipefd as appropriate.
	//
	// "cd" error note:
	// 	- Upon syntax errors: Display the message
	//	  "cd: Syntax error on bad number of arguments"
	// 	- Upon system call errors: Call perror("cd")
	//
	// "cd" Hints:
	//    For the "cd" command, you should change directories AFTER
	//    the fork(), not before it.  Why?
	//    Design some tests with 'bash' that will tell you the answer.
	//    For example, try "cd /tmp ; cd $HOME > foo".  In which directory
	//    does foo appear, /tmp or $HOME?  If you chdir() BEFORE the fork,
	//    in which directory would foo appear, /tmp or $HOME?
	//

	if ((pid = fork()) < 0) {
		die("Fail to fork");
		return pid;
	} else if (pid == 0) {
		/* Child*/

		/*
		 * Standard Output Source
		 * 1) Pipe to next cmd
		 * 2) Redirection from file
		 * 3) STDOUT
		 */
		if (cmd->controlop == CMD_PIPE) {
			close(fd[0]);
			dup2(fd[1], STDOUT_FILENO);
		} else if (cmd->redirect_filename[STDOUT_FILENO]) {
			int fd;
			char *fn;

			fn = cmd->redirect_filename[STDOUT_FILENO];
			fd = open(fn, O_RDWR | O_CREAT, 0644);
			if (fd < 0)
				die("fail to open %s\n", fn);

			dup2(fd, STDOUT_FILENO);
		}

		/*
		 * Standard Input Source
		 * 1) Pipe from previous cmd
		 * 2) Redirection from file
		 * 3) STDIN
		 */
		if (*pass_pipefd != STDIN_FILENO) {
			/* The right-side of the pipe, CMD | CMD */
			dup2(*pass_pipefd, STDIN_FILENO);
		} else if (cmd->redirect_filename[STDIN_FILENO]) {
			int fd;
			char *fn;
			
			fn = cmd->redirect_filename[STDIN_FILENO];
			fd = open(fn, O_RDWR, 0644);
			if (fd < 0)
				die("fail to open %s\n", fn);

			dup2(fd, STDIN_FILENO);
		}

		execvp(cmd->argv[0], cmd->argv);
	} else {
		/* Parent */

		if (cmd->controlop == CMD_PIPE) {
			close(fd[1]);
			*pass_pipefd = fd[0];
		} else
			*pass_pipefd = STDIN_FILENO;
	}

	return pid;
}

/* command_line_exec(cmdlist)
 *
 *   Execute the command list.
 *
 *   Execute each individual command with 'command_exec'.
 *   String commands together depending on the 'cmdlist->controlop' operators.
 *   Returns the exit status of the entire command list, which equals the
 *   exit status of the last completed command.
 *
 *   The operators have the following behavior:
 *
 *      CMD_END, CMD_SEMICOLON
 *                        Wait for command to exit.  Proceed to next command
 *                        regardless of status.
 *      CMD_AND           Wait for command to exit.  Proceed to next command
 *                        only if this command exited with status 0.  Otherwise
 *                        exit the whole command line.
 *      CMD_OR            Wait for command to exit.  Proceed to next command
 *                        only if this command exited with status != 0.
 *                        Otherwise exit the whole command line.
 *      CMD_BACKGROUND, CMD_PIPE
 *                        Do not wait for this command to exit.  Pretend it
 *                        had status 0, for the purpose of returning a value
 *                        from command_line_exec.
 */
int command_line_exec(command_t *cmdlist)
{
	int cmd_status = 0;
	int pipefd = STDIN_FILENO;

	while (cmdlist) {
		pid_t pid;
		int wp_status;

		if (cmdlist->subshell) {
			cmd_status = command_line_exec(cmdlist->subshell);

			if ((cmdlist->controlop == CMD_AND && !cmd_status) ||
			    (cmdlist->controlop == CMD_OR && cmd_status))
				goto next;
		}

		pid = command_exec(cmdlist, &pipefd);
		if (pid < 0) {
			cmd_status = -EFAULT;
			break;
		}

		switch (cmdlist->controlop) {
		case CMD_END:
		case CMD_SEMICOLON:
			if (waitpid(pid, &wp_status, 0) < 0)
				die("waitpid fails");

			if (WIFEXITED(wp_status))
				cmd_status = WEXITSTATUS(wp_status);
			else
				die("Signal etc. not supported\n");
			goto next;
		case CMD_AND:
		case CMD_OR:
			if (waitpid(pid, &wp_status, 0) < 0)
				die("waitpid fails");

			if (WIFEXITED(wp_status))
				cmd_status = WEXITSTATUS(wp_status);
			else
				die("Signal etc. not supported\n");

			if ((cmdlist->controlop == CMD_AND && !cmd_status) ||
			    (cmdlist->controlop == CMD_OR && cmd_status))
				goto next;
			else
				goto out;
		case CMD_BACKGROUND:
		case CMD_PIPE:
			;
		}

next:
		cmdlist = cmdlist->next;
	}

out:
	return cmd_status;
}

void report_background_job(int job_id, int process_id)
{
    fprintf(stderr, "[%d] %d\n", job_id, process_id);
}
