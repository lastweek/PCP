/*
 * Copyright (c) 2018 Yizhou Shan, Liwei Guo, Yajie Geng
 * All rights reserved.
 */

#include <sploit.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static LIST_HEAD(user_list);
static LIST_HEAD(alias_list);

/*
 * Expand the @original command to @buf based on alias.
 * @buf has @max_len bytes available.
 */
int expandCommandAlias(char *original, char *buf, int max_len)
{
	return 0;
}

int server_listen_port;
char PWD[STRING_MAX_LEN];

void set_PWD(char *new_pwd)
{
	strncpy(PWD, new_pwd, STRING_MAX_LEN);
}

int set_port(int port)
{
	if (port < 0 || port > 65535)
		return -EINVAL;

	server_listen_port = port;
	return 0;
}

struct User *find_user_by_name(char *uname)
{
	struct User *u;

	BUG_ON(!uname);

	list_for_each_entry(u, &user_list, next) {
		if (!strncmp(u->uname, uname, USER_NAME_MAX_LEN))
			return u;
	}
	return NULL;
}

static void init_user(struct User *user)
{
	INIT_LIST_HEAD(&user->next);
	user->isLoggedIn = false;
}

/* Return 0 on success, otherwise on failures */
static int add_user(struct User *user)
{
	struct User *u;

	/* Avoid duplication */
	u = find_user_by_name(user->uname);
	if (u)
		return -EINVAL;

	list_add(&user->next, &user_list);
	return 0;
}

int try_to_add_user(char *token)
{
	struct User *user;
	int ret;

	user = malloc(sizeof(*user));
	if (!user)
		return -ENOMEM;

	token = strtok(NULL, " ");
	if (!token) {
		ret = -EINVAL;
		goto out;
	}

	strncpy(user->uname, token, USER_NAME_MAX_LEN);

	token = strtok(NULL, " ");
	if (!token) {
		ret = -EINVAL;
		goto out;
	}

	strncpy(user->passwd, token, USER_NAME_MAX_LEN);

	/* Should be end of story */
	token = strtok(NULL, " ");
	if (token) {
		ret = -EINVAL;
		goto out;
	}

	init_user(user);
	ret = add_user(user);

	return ret;

out:
	free(user);
	return ret;
}

struct alias *find_alias_by_name(char *name)
{
	struct alias *a;

	BUG_ON(!name);

	list_for_each_entry(a, &alias_list, next) {
		if (!strncmp(a->alias, name, STRING_MAX_LEN))
			return a;
	}
	return NULL;
}

static int add_alias(struct alias *alias)
{
	struct alias *a;

	a = find_alias_by_name(alias->alias);
	if (a)
		return -EINVAL;

	list_add(&alias->next, &alias_list);
	return 0;
}

void dump_alias(void)
{
	struct alias *a;

	list_for_each_entry(a, &alias_list, next) {
		printf("alias %s=%s %s\n",
			a->alias, a->cname, a->parameters);
	}
}

void dump_users(void)
{
	struct User *u;

	list_for_each_entry(u, &user_list, next) {
		printf("user %s %s\n",
			u->uname, u->passwd);
	}
}

int try_to_add_alias(char *token)
{
	struct alias *alias;
	int ret;

	alias = malloc(sizeof(*alias));
	if (!alias)
		return -ENOMEM;
	memset(alias, 0, sizeof(*alias));

	token = strtok(NULL, " ");
	if (!token) {
		ret = -EINVAL;
		goto out;
	}

	/* Second token is the alias itself */
	strncpy(alias->alias, token, STRING_MAX_LEN);

	/* Third token is the real exec */
	token = strtok(NULL, " ");
	if (!token) {
		ret = -EINVAL;
		goto out;
	}

	strncpy(alias->cname, token, STRING_MAX_LEN);

	/* We treat everything after is one parameter list */
	token = strtok(NULL, "");
	if (!token)
		goto add;

	strncpy(alias->parameters, token, STRING_MAX_LEN);

add:
	ret = add_alias(alias);
	return ret;

out:
	free(alias);
	return ret;
}

int parse_sploit(const char *filename)
{
	FILE *fp;
	char line[STRING_MAX_LEN];
	char *token;
	int ret, nr_line = 0;
	size_t len;

	fp = fopen(filename, "r+");
	if (!fp) {
		perror("Fail to open file");
		return -1;
	}

	while (fgets(line, sizeof(line), fp)) {
		len = strlen(line) - 1;
		if (line[len] == '\n')
			line[len] = '\0';

		nr_line++;

		/* Skip comment line */
		if (line[0] == '#')
			continue;

		token = strtok(line, " ");

		/* Skip empty line */
		if (!token)
			continue;

		if (!strncmp(token, "base", 4)) {
			token = strtok(NULL, " ");
			if (!token)
				goto error;

			set_PWD(token);
			
			token = strtok(NULL, "");
			if (token)
				goto error;
		} else if (!strncmp(token, "port", 4)) {
			token = strtok(NULL, " ");
			if (!token)
				goto error;

			ret = set_port(atoi(token));
			if (ret)
				goto error;

			token = strtok(NULL, "");
			if (token)
				goto error;
		} else if (!strncmp(token, "user", 4)) {
			ret = try_to_add_user(token);
			if (ret)
				goto error;
		} else if (!strncmp(token, "alias", 5)) {
			ret = try_to_add_alias(token);
			if (ret)
				goto error;
		} else
			continue;
	}

	if (server_listen_port == 0) {
		printf("You must specify server listening port!\n");
		exit(0);
	}

	dump_alias();
	dump_users();
	printf("Listening port: %d\n", server_listen_port);
	printf("PWD=%s\n", PWD);

	fclose(fp);
	return 0;

error:
	printf("ERROR: Invalid configuration at line %d\n [%s]",
		nr_line, line);
	fclose(fp);
	return -EINVAL;
}
