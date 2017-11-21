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
#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <limits.h>

#include "list.h"

#define BUG_ON(condition)	assert(!(condition))

unsigned long default_max_weight = 100;

struct vertex;

/*
 * Edges are listed
 * Oh linked-list is stupid?
 */
struct vertex {
	unsigned int		id;
	unsigned long		d;		/* shortest path estimate */
	struct vertex		*parent;	/* parent in the shortest path */
	struct list_head	head;		/* list of edges */
};

struct edge {
	struct vertex		*src, *dst;	/* directed graph */
	unsigned long		weight;		/* non-negative weight */
	struct list_head	next;
};

struct graph {
	struct vertex	*vertices;
	struct edge	*edges;
	unsigned long	nr_vertices;
	unsigned long	nr_edges;
	unsigned long	max_weight;
};

/* reset path estimate d and parent */
static void reset_graph(struct graph *graph, unsigned long source_id)
{
	unsigned long i;
	struct vertex *vertex;

	for (i = 0; i < graph->nr_vertices; i++) {
		vertex = &graph->vertices[i];
		vertex->d = ULONG_MAX;
		vertex->parent = NULL;
	}

	if (source_id >= graph->nr_vertices) {
		printf("graph->nr_vertices: %lu, source_id: %lu\n",
			graph->nr_vertices, source_id);
		BUG_ON(1);
	}

	vertex = &graph->vertices[source_id];
	vertex->d = 0;
}

/* edge: (src, dst) */
static inline void relax(struct vertex *src, struct vertex *dst,
			 struct edge *edge)
{
	unsigned long new_d;

	if (src->d == ULONG_MAX)
		return;

	new_d = src->d + edge->weight;
	if (dst->d > new_d) {
		dst->d = new_d;
		dst->parent = src;
	}
}

static void BellmanFord(struct graph *graph)
{
	unsigned long i, j;
	struct edge *edge;

	reset_graph(graph, 1);

	for (i = 0; i < graph->nr_vertices; i++) {
		for (j = 0; j < graph->nr_edges; j++) {
			edge = &graph->edges[j];
			relax(edge->src, edge->dst, edge);
		}
	}
}

static void Dijkstra(struct graph *graph)
{
}

/* O(N) linear search */
static bool vertex_has_edge(struct vertex *src, struct edge *edge)
{
	struct edge *pos;

	BUG_ON(!src || !edge);
	list_for_each_entry(pos, &src->head, next) {
		if (pos == edge)
			return true;
	}
	return false;
}

static bool vertices_direct_connect(struct vertex *src, struct vertex *dst)
{
	struct edge *pos;

	list_for_each_entry(pos, &src->head, next) {
		if (pos->dst == dst)
			return true;
	}
	return false;
}

static void init_random_connected_graph(struct graph *graph)
{
	unsigned long i;
	unsigned long weight;
	struct edge *edge;
	struct vertex *vertex;

	for (i = 0; i < graph->nr_vertices; i++) {
		vertex = &graph->vertices[i];
		vertex->id = i;
		vertex->d = ULONG_MAX;
		vertex->parent = NULL;
		INIT_LIST_HEAD(&vertex->head);
	}

	for (i = 0; i < graph->nr_edges; i++) {
		edge = &graph->edges[i];
		edge->src = edge->dst = NULL;
		edge->weight = 0;
		INIT_LIST_HEAD(&edge->next);
	}

	/* minimum number of edges to form a connected graph */
	for (i = 0; i < (graph->nr_vertices-1); i++) {
		struct vertex *src, *dst;

		src = &graph->vertices[i];
		dst = &graph->vertices[i + 1];

		edge = &graph->edges[i];
		while (edge->weight == 0)
			edge->weight = rand() % graph->max_weight;
		edge->src = src;
		edge->dst = dst;
		list_add(&edge->next, &src->head);
	}

	/* now user specified extra edges */
	while (i < graph->nr_edges) {
		unsigned long rand_src, rand_dst, rand_weight;
		struct vertex *src, *dst;

retry:
		rand_src = rand() % graph->nr_vertices;
		rand_dst = rand() % graph->nr_vertices;
		if (rand_src == rand_dst || rand_src == 0 || rand_dst == 0)
			goto retry;

		src = &graph->vertices[rand_src];
		dst = &graph->vertices[rand_dst];

		/* already an edge? */
		if (vertices_direct_connect(src, dst))
			goto retry;

		/* establish this edge */
		edge = &graph->edges[i];
		while (edge->weight == 0)
			edge->weight = rand() % graph->max_weight;
		edge->src = src;
		edge->dst = dst;
		list_add(&edge->next, &src->head);

		i++;
	}
}

/*
 * Create a directed connected graph, with non-negative weighted edges.
 * @density = nr_edges / nr_vertices * nr_vertices;
 *
 * Return: pointer to graph if succeed
 * otherwise NULL is returned.
 */
static struct graph *init_random_graph(unsigned long nr_vertices,
				       double density)
{
	struct graph *graph;
	struct vertex *vertices;
	struct edge *edges;
	double min_density;
	unsigned long nr_edges;

	BUG_ON(nr_vertices == 0 || density < 0 || density > 1);

	graph = malloc(sizeof(*graph));
	if (!graph)
		return NULL;

	vertices = malloc(nr_vertices * sizeof(*vertices));
	if (!vertices) {
		free(graph);
		return NULL;
	}

	/* we want it to be a connected graph */
	min_density = (double)(nr_vertices - 1) / (nr_vertices * nr_vertices);
	if (density < min_density)
		density = min_density;

	/* only need those edges */
	nr_edges = nr_vertices * nr_vertices * density;

	edges = malloc(nr_edges * sizeof(*edges));
	if (!edges) {
		free(graph);
		free(vertices);
		return NULL;
	}

	graph->vertices = vertices;
	graph->edges = edges;
	graph->nr_vertices = nr_vertices;
	graph->nr_edges = nr_edges;
	graph->max_weight = default_max_weight;

	/* now init vertices and edges array */
	srand(time(NULL));
	init_random_connected_graph(graph);

	return graph;
}

static void dump_graph(struct graph *graph)
{
	unsigned long i;

	printf("----- dump graph start ----\n");
	printf("nr_vertices: %lu, nr_edges: %lu, max_weight: %lu\n",
		graph->nr_vertices, graph->nr_edges, graph->max_weight);

	for (i = 0; i < graph->nr_vertices; i++) {
		struct vertex *v;

		v = &graph->vertices[i];
		printf("vertex%lu, shortest=%Ld\n", v->id,
			(v->d == ULONG_MAX) ? -1 : v->d);
	}

	for (i = 0; i < graph->nr_edges; i++) {
		struct edge *e;
		struct vertex *src, *dst;

		e = &graph->edges[i];
		src = e->src;
		dst = e->dst;
		printf("(%Ld, %Ld) weight=%lu\n",
			src ? src->id : -1,
			dst ? dst->id : -1,
			e->weight);
	}
	printf("----- dump graph end ----\n");
}

int main(void)
{
	struct graph *graph;

	graph = init_random_graph(10, 0.3);
	BUG_ON(!graph);
	dump_graph(graph);

	Dijkstra(graph);
	BellmanFord(graph);
	dump_graph(graph);

	return 0;
}
