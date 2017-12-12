/*
 * Copyright (c) 2017 Yizhou Shan. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * Direct connected non-negative graph testing.
 * Outbound edges of a vertex are linked into a list.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <sys/time.h>

#include "list.h"

#define BUG_ON(condition)	assert(!(condition))

/* Maximum weight of an edge */
unsigned long default_max_weight = 1000;

struct vertex;

#define VERTEX_UNMARKED	0
#define VERTEX_MARKED	1

/*
 * Edges are listed
 * Oh linked-list is stupid?
 */
struct vertex {
	unsigned int		id;
	unsigned int		flags;		/* For Dijkstra's array impl */
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
	double		density;
};

static inline void unmark_vertex(struct vertex *v)
{
	v->flags = VERTEX_UNMARKED;
}

static inline void mark_vertex(struct vertex *v)
{
	v->flags = VERTEX_MARKED;
}

static inline bool vertex_marked(struct vertex *v)
{
	if (v->flags == VERTEX_MARKED)
		return true;
	return false;
}

/*
 * result: diff
 * x: end time
 * y: start time
 */
static inline int timeval_sub(struct timeval *result, struct timeval *x,
			  struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}

/* reset path estimate d and parent */
static void reset_graph(struct graph *graph, unsigned long source_id)
{
	unsigned long i;
	struct vertex *vertex;

	for (i = 0; i < graph->nr_vertices; i++) {
		vertex = &graph->vertices[i];
		vertex->d = ULONG_MAX;
		vertex->parent = NULL;
		unmark_vertex(vertex);
	}

	if (source_id >= graph->nr_vertices) {
		printf("graph->nr_vertices: %lu, source_id: %lu\n",
			graph->nr_vertices, source_id);
		BUG_ON(1);
	}

	vertex = &graph->vertices[source_id];
	vertex->d = 0;
}

static inline void relax(struct edge *edge)
{
	struct vertex *src = edge->src;
	struct vertex *dst = edge->dst;
	unsigned long new_d;

	/*
	 * avoid overflow (Bellman-Ford only)
	 * Since Dijkstra always starts from min estimation vertex
	 */
	if (src->d == ULONG_MAX)
		return;

	new_d = src->d + edge->weight;
	if (dst->d > new_d) {
		dst->d = new_d;
		dst->parent = src;
	}
}

/*
 * Bellman-Ford algorithm:
 *	O(VE)
 */
static void bellman_ford(struct graph *graph, unsigned long src_vertex)
{
	unsigned long i, j, base, index;
	struct edge *edge;
	struct timeval start, end, diff;
	unsigned long nr_vertices = graph->nr_vertices;
	unsigned long nr_edges = graph->nr_edges;

	reset_graph(graph, src_vertex);

	base = rand() % nr_edges;

	gettimeofday(&start, NULL);
	for (i = 0; i < nr_vertices; i++) {
		for (j = 0; j < nr_edges; j++) {
			index = base % nr_edges;
			base++;
			edge = &graph->edges[index];
			relax(edge);
		}
	}
	gettimeofday(&end, NULL);

	timeval_sub(&diff, &end, &start);
	printf("%s(): (nr_vertices: %lu, nr_edges: %lu, density: %lf), runtime: %ld.%06ld s\n",
		__func__, graph->nr_vertices, graph->nr_edges, graph->density,
		diff.tv_sec, diff.tv_usec);
}

/*
 * Find the vertex currently has the minimum path estimation.
 * Array implementation: O(V)
 *
 * Can be implemented as a binary min-heap or Fibnacci heap.
 */
static struct vertex *extract_min(struct graph *graph)
{
	unsigned long i, min_d;
	struct vertex *v, *min_v;

	min_d = ULONG_MAX;
	min_v = NULL;

	for (i = 0; i < graph->nr_vertices; i++) {
		v = &graph->vertices[i];
		if (vertex_marked(v))
			continue;
		if (v->d < min_d) {
			min_v = v;
			min_d = v->d;
		}
	}

	BUG_ON(!min_v);

	mark_vertex(min_v);
	return min_v;
}

/* relaxt @v's all adjacent edges */
static void vertex_relax_edges(struct vertex *v)
{
	struct edge *edge;

	list_for_each_entry(edge, &v->head, next)
		relax(edge);
}

/*
 * Dijkstra's algorithm with array implementation:
 * 	O(V^2 + E)
 */
static void dijkstra_array(struct graph *graph, unsigned long src_vertex)
{
	unsigned long i;
	struct vertex *v;
	struct timeval start, end, diff;

	reset_graph(graph, src_vertex);

	gettimeofday(&start, NULL);
	for (i = 0; i < graph->nr_vertices; i++) {
		v = extract_min(graph);
		vertex_relax_edges(v);
	}
	gettimeofday(&end, NULL);

	timeval_sub(&diff, &end, &start);
	printf("%s(): (nr_vertices: %lu, nr_edges: %lu, density: %lf), runtime: %ld.%06ld s\n",
		__func__, graph->nr_vertices, graph->nr_edges, graph->density,
		diff.tv_sec, diff.tv_usec);
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

static inline void graph_add_edge(struct vertex *src, struct vertex *dst,
				  struct edge *edge, struct graph *graph)
{
	while (edge->weight == 0)
		edge->weight = rand() % graph->max_weight;
	edge->src = src;
	edge->dst = dst;
	list_add(&edge->next, &src->head);
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
		unmark_vertex(vertex);
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
		graph_add_edge(src, dst, edge, graph);

		edge = &graph->edges[i + graph->nr_vertices - 1];
		graph_add_edge(dst, src, edge, graph);
	}

	/* now user specified extra edges */
	i = (graph->nr_vertices - 1) * 2;
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

		/* already has an edge? */
		if (vertices_direct_connect(src, dst))
			goto retry;

		edge = &graph->edges[i];
		graph_add_edge(src, dst, edge, graph);

		i++;
	}
}

/**
 * init_random_graph
 * @nr_vertices: number of vertices within this graph
 * @density = nr_edges / nr_vertices * nr_vertices;
 *
 * Create a directed connected graph, with non-negative weighted edges.
 * Return: pointer to graph if succeed, otherwise NULL is returned.
 */
static struct graph *init_random_graph(unsigned long nr_vertices,
				       double density)
{
	struct graph *graph;
	struct vertex *vertices;
	struct edge *edges;
	unsigned long nr_edges, min_nr_edges;

	printf("Generating graph (vertices: %lu, density: %lf)...",
		nr_vertices, density);

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
	min_nr_edges = (nr_vertices - 1) * 2;
	nr_edges = nr_vertices * nr_vertices * density;
	if (nr_edges < min_nr_edges)
		nr_edges = min_nr_edges;

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
	graph->density = (double)nr_edges / (nr_vertices * nr_vertices);

	/* now init vertices and edges array */
	srand(time(NULL));
	init_random_connected_graph(graph);

	printf("OK\n");
	return graph;
}

static void dump_graph_vertics(struct graph *graph)
{
	unsigned long i;

	printf("  - Vertices:\n");
	for (i = 0; i < graph->nr_vertices; i++) {
		struct vertex *v;

		v = &graph->vertices[i];
		printf("vertex%lu, marked=%d shortest=%Ld\n", v->id,
			v->flags,
			(v->d == ULONG_MAX) ? -1 : v->d);
	}
}

static void dump_graph_edges(struct graph *graph)
{
	unsigned long i;

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
}

static void dump_graph(struct graph *graph)
{
	unsigned long i;

	printf("----- dump graph start ----\n");
	printf("nr_vertices: %lu, nr_edges: %lu, max_weight: %lu\n",
		graph->nr_vertices, graph->nr_edges, graph->max_weight);

	dump_graph_vertics(graph);
	dump_graph_edges(graph);
	printf("----- dump graph end ----\n");
}

int main(void)
{
	struct graph *graph;
	unsigned long src_vertex;

	graph = init_random_graph(64, 0.01);
	BUG_ON(!graph);

	src_vertex = 5;
	dijkstra_array(graph, src_vertex);
	//dump_graph_vertics(graph);

	bellman_ford(graph, src_vertex);
	//dump_graph_vertics(graph);

	return 0;
}
