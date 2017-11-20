/*
 * Copyright (c) 2017 Yizhou Shan. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct vertex;

struct vertex {
	unsigned int	id;
	unsigned int	d;		/* shortest path estimate */
	struct vertex	*parent;	/* parent in the shortest path */
};

struct edge {
	struct vertex	*src, *dst;
	unsigned int	weight;
};

static void relax(struct vertex *src, struct vertex *dst, struct edge *edge)
{

}

static void bellman_ford(void)
{

}

static void dijkstra(void)
{

}

int main(void)
{
	return 0;
}
