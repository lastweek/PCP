/* 
 * Skeleton code for commandline parsing for Lab 1 - Shell processing
 * This file contains the skeleton code for parsing input from the command
 * line.
 * Acknowledgement: UCLA CS111
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#include "cmdline.h"
#include "myshell.h"

/*
 * parsestate_t
 *
 *   The parsestate_t object represents the current state of the command line
 *   parser.  'parse_init' initializes a parsestate_t object for a command
 *   line, then calls to 'parse_gettoken' step through the command line one
 *   token at a time.  'parse_ungettoken' backs up one token.
 */

void parse_init(parsestate_t *parsestate, char *input_line)
{
	parsestate->position = input_line;
	parsestate->last_position = NULL;
}

/*
 * parse_gettoken(parsestate, token)
 *
 *   Fetches the next token from the input line.
 *   The token's type is stored in 'token->type', and the token itself is
 *   stored in 'token->buffer'.  The parsestate itself is moved past the
 *   current token, so that the next call to 'parse_gettoken' will return the
 *   next token.
 *   Tokens are delimited by space characters. Any leading space is skipped.
 *
 *   EXERCISES:
 *        1. The example function just reads the whole command line into a
 *           single token (it ignores spaces). Make it stop when it reaches the
 *           end of a token, as delimited by "whitespace" (a C term for
 *           blank characters, including tab '\t' and newline '\n'.  Hint:
 *           man isspace)
 *        2. Add support for "double quotes".  If the parsestate contains
 *           the string ==> "a b" c <== (arrows not included), then the next
 *           call to 'parse_gettoken' should stick the string "a b" into
 *           'token', and move the parsestate to "c" or " c".
 *   EXTRA CREDIT EXERCISE:
 *        3. Allow special characters like ';' and '&' to terminate a token.
 *           In real shells, "a;b" is treated the same as "a ; b".
 *           The following characters and two-character sequences should
 *           end the current token, except if they occur in double quotes.
 *           Of course, you can return one of these sequences as a token
 *           when it appears first in the string.
 *                (  )  ;  |  &  &&  ||  >  <
 *           Note that "2>" does not end a token.  The string "x2>y" is parsed
 *           as "x2 > y", not "x 2> y".
 */
void parse_gettoken(parsestate_t *parsestate, token_t *token)
{
	int i = 0;
	char *str = parsestate->position;
	bool quote_state = false;
	bool any_quotes = false;

restart:
	while (isspace(*str))
		str++;

	/* Reachs cmd end? */
	if (*str == '\0') {
		parsestate->last_position = parsestate->position;
		token->buffer[0] = '\0';
		token->type = TOK_END;
		return;
	}

	/* Get next token */
	while (*str != '\0')  {
		if (i >= TOKENSIZE - 1)
			goto error;

		if (quote_state) {
			/* Quote state take everthing except " */
			if (*str != '\"')
				token->buffer[i++] = *str++;
			else {
				quote_state = false;
				str++;

				/*
				 * Make ">" TOK_NORMAL
				 */
				any_quotes = true;
			}
			continue;
		}

		/* Normal space termination */
		if (isspace(*str))
			break;

		if (*str == '\"') {
			quote_state = true;
			str++;
			continue;
		}

		token->buffer[i++] = *str++;
	}

	if (i == 0) {
		/* "" case */
		goto restart;
	}

	token->buffer[i] = '\0';

	/* Save state */
	parsestate->last_position = parsestate->position;
	parsestate->position = str;

	/* "" make all special control characters normal */
	if (any_quotes) {
		token->type = TOK_NORMAL;
		return;
	}

	if (!strcmp(token->buffer, "<"))
		token->type = TOK_LESS_THAN;
	else if (!strcmp(token->buffer, ">"))
		token->type = TOK_GREATER_THAN;
	else if (!strcmp(token->buffer, "2>"))
		token->type = TOK_2_GREATER_THAN;
	else if (!strcmp(token->buffer, ";"))
		token->type = TOK_SEMICOLON;
	else if (!strcmp(token->buffer, "&"))
		token->type = TOK_AMPERSAND;
	else if (!strcmp(token->buffer, "|"))
		token->type = TOK_PIPE;
	else if (!strcmp(token->buffer, "&&"))
		token->type = TOK_DOUBLEAMP;
	else if (!strcmp(token->buffer, "||"))
		token->type = TOK_DOUBLEPIPE;
	else if (!strcmp(token->buffer, "("))
		token->type = TOK_OPEN_PAREN;
	else if (!strcmp(token->buffer, ")"))
		token->type = TOK_CLOSE_PAREN;
	else
		token->type = TOK_NORMAL;

	return;

error:
	token->buffer[0] = '\0';
	token->type = TOK_ERROR;
}

/*
 * parse_ungettoken(parsestate)
 *
 *   Backs up the parsestate by one token.
 *   It's impossible to back up more than one token; if you call
 *   parse_ungettoken() twice in a row, the second call will fail.
 */
void parse_ungettoken(parsestate_t *parsestate)
{
	/* Can't back up more than one token */
	assert(parsestate->last_position != NULL);

	parsestate->position = parsestate->last_position;
	parsestate->last_position = NULL;
}

static command_t *command_alloc(void)
{
	command_t *cmd = malloc(sizeof(*cmd));
	if (cmd)
		memset(cmd, 0, sizeof(*cmd));
	return cmd;
}

/*
 * command_free()
 *
 *   Frees all memory associated with a command.
 *
 *   EXERCISE:
 *        Fill in this function.
 *        Also free other structures pointed to by 'cmd', including
 *        'cmd->subshell' and 'cmd->next'.
 *        If you're not sure what to free, look at the other code in this file
 *        to see when memory for command_t data structures is allocated.
 */
static void command_free(command_t *cmd)
{
	int i;
	
	// It's OK to command_free(NULL).
	if (!cmd)
		return;

	/* Your code here. */
}

/*
 * command_parse(parsestate)
 *
 *   Parses a single command_t structure from the input string.
 *   Returns a pointer to the allocated command, or NULL on error
 *   or if the command is empty. (One example is if the end of the
 *   line is reached, but there are other examples too.)
 */
command_t *command_parse(parsestate_t *parsestate)
{
	int i = 0;
	command_t *cmd;

	cmd = command_alloc();
	if (!cmd)
		return NULL;

	while (1) {
		// Normal tokens go in the cmd->argv[] array.
		// Redirection file names go into cmd->redirect_filename[].
		// Open parenthesis tokens indicate a subshell command.
		// Other tokens complete the current command
		// and are not actually part of it;
		// use parse_ungettoken() to save those tokens for later.

		// There are a couple errors you should check.
		// First, be careful about overflow on normal tokens.
		// Each command_t only has space for MAXTOKENS tokens in
		// 'argv'. If there are too many tokens, reject the whole
		// command.
		// Second, redirection tokens (<, >, 2>) must be followed by
		// TOK_NORMAL tokens containing file names.
		// Third, a parenthesized subcommand can't be part of the
		// same command as other normal tokens.  For example,
		// "echo ( echo foo )" and "( echo foo ) echo" are both errors.
		// (You should figure out exactly how to check for this kind
		// of error. Try interacting with the actual 'bash' shell
		// for some ideas.)
		// 'goto error' when you encounter one of these errors,
		// which frees the current command and returns NULL.

		// Hint: An open parenthesis should recursively call
		// command_line_parse(). The command_t structure has a slot
		// you can use for parens; figure out how to use it!

		token_t token;
		token_t fn_token;
		parse_gettoken(parsestate, &token);

		switch (token.type) {
		case TOK_NORMAL:
			cmd->argv[i++] = strdup(token.buffer);
			break;
		case TOK_LESS_THAN:
			parse_gettoken(parsestate, &fn_token);
			if (fn_token.type != TOK_NORMAL)
				goto error;
			cmd->redirect_filename[0] = strdup(fn_token.buffer);
			break;
		case TOK_GREATER_THAN:
			parse_gettoken(parsestate, &fn_token);
			if (fn_token.type != TOK_NORMAL)
				goto error;
			cmd->redirect_filename[1] = strdup(fn_token.buffer);
			break;
		case TOK_2_GREATER_THAN:
			parse_gettoken(parsestate, &fn_token);
			if (fn_token.type != TOK_NORMAL)
				goto error;

			cmd->redirect_filename[2] = strdup(fn_token.buffer);
			break;
		case TOK_OPEN_PAREN:
			/* CMD ) */
			cmd->subshell = command_line_parse(parsestate, 1);
			if (!cmd->subshell)
				goto error;
			break;
		default:
			parse_ungettoken(parsestate);
			goto done;
		}
	}

done:
	/* Terminate argv array will NULL pointer at last */
	cmd->argv[i] = 0;

	if (i == 0 && !cmd->subshell) {
		command_free(cmd);
		return NULL;
	} else
		return cmd;

error:
	command_free(cmd);
	return NULL;
}

/*
 * command_line_parse(parsestate, in_parens)
 *
 *   Parses a command line from 'input' into a linked list of command_t
 *   structures. The head of the linked list is returned, or NULL is
 *   returned on error.
 *   If 'in_parens != 0', then command_line_parse() is being called recursively
 *   from command_parse().  A right parenthesis should end the "command line".
 *   But at the top-level command line, when 'in_parens == 0', a right
 *   parenthesis is an error.
 */
command_t *command_line_parse(parsestate_t *parsestate, int in_parens)
{
	command_t *prev_cmd = NULL;
	command_t *head = NULL;
	command_t *cmd;
	token_t token, next_token;
	int r;

	// COMMAND                             => OK
	// COMMAND ;                           => OK
	// COMMAND && COMMAND                  => OK
	// COMMAND &&                          => error (can't end with &&)
	// COMMAND )                           => error (but OK if "in_parens")

	while (1) {
		/* Parse current cmd */
		cmd = command_parse(parsestate);
		if (!cmd)
			goto error;

		/* Link current cmd to previous cmds if any */
		if (prev_cmd)
			prev_cmd->next = cmd;
		else
			head = cmd;
		prev_cmd = cmd;

		/* Parse next cmd if any */
		parse_gettoken(parsestate, &token);

		/* MUST == TOK_END, break out */
		if (!token_is_controlop(token.type))
			break;

		cmd->controlop = token.type;

		switch (token.type) {
		case TOK_SEMICOLON:
		case TOK_AMPERSAND:
			parse_gettoken(parsestate, &next_token);
			if (next_token.type == TOK_END)
				/*
				 * CMD ;
				 * CMD &
				 */
				goto done;
			else {
				/*
				 * CMD ; CMD
				 * CMD & CMD
				 */
				parse_ungettoken(parsestate);
				continue;
			}
		case TOK_PIPE:
		case TOK_DOUBLEAMP:
		case TOK_DOUBLEPIPE:
			parse_gettoken(parsestate, &next_token);
			if (next_token.type == TOK_END)
				/*
				 * CMD |
				 * CMD &&
				 * CMD ||
				 */
				goto error;
			else {
				/*
				 * CMD | CMD
				 * CMD && CMD
				 * CMD || CMD
				 */
				parse_ungettoken(parsestate);
				continue;
			}
		default:
			die("BUG");
			goto error;
		}
	}

done:
	if (token.type == TOK_CLOSE_PAREN && !in_parens)
		goto error;

	return head;

error:
	command_free(head);
	return NULL;
}

void command_print(command_t *cmd, int indent)
{
	int argc, i;
	
	if (cmd == NULL) {
		printf("%*s[NULL]\n", indent, "");
		return;
	}

	for (argc = 0; argc < MAXTOKENS && cmd->argv[argc]; argc++)
		/* do nothing */;

	assert(argc <= MAXTOKENS);

	printf("%*s[%d args", indent, "", argc);
	for (i = 0; i < argc; i++)
		printf(" \"%s\"", cmd->argv[i]);

	// Print redirections
	if (cmd->redirect_filename[STDIN_FILENO])
		printf(" <%s", cmd->redirect_filename[STDIN_FILENO]);
	if (cmd->redirect_filename[STDOUT_FILENO])
		printf(" >%s", cmd->redirect_filename[STDOUT_FILENO]);
	if (cmd->redirect_filename[STDERR_FILENO])
		printf(" 2>%s", cmd->redirect_filename[STDERR_FILENO]);

	// Print the subshell command, if any
	if (cmd->subshell) {
		printf("\n");
		command_print(cmd->subshell, indent + 2);
	}
	
	printf("] ");
	switch (cmd->controlop) {
	case TOK_SEMICOLON:
		printf(";");
		break;
	case TOK_AMPERSAND:
		printf("&");
		break;
	case TOK_PIPE:
		printf("|");
		break;
	case TOK_DOUBLEAMP:
		printf("&&");
		break;
	case TOK_DOUBLEPIPE:
		printf("||");
		break;
	case TOK_END:
		// we write "END" as a dot
		printf(".");
		break;
	default:
		assert(0);
	}

	// Done!
	printf("\n");

	// if next is NULL, then controlop should be CMD_END, CMD_BACKGROUND,
	// or CMD_SEMICOLON
	assert(cmd->next || cmd->controlop == CMD_END
	       || cmd->controlop == CMD_BACKGROUND
	       || cmd->controlop == CMD_SEMICOLON);

	if (cmd->next)
		command_print(cmd->next, indent);
}
