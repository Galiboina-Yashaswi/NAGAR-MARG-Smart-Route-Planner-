#include "../include/dijkstra.h"
#include "../include/minheap.h"
#include <stdio.h>
#include <stdlib.h>
#include <float.h>

/**
 * @brief Helper to initialize an empty/failed DijkstraResult.
 */
static DijkstraResult make_empty_result(void) {
    DijkstraResult result;
    result.total_distance = -1.0;
    result.estimated_travel_time = -1.0;
    result.eco_cost = -1.0;
    result.path = NULL;
    result.node_count = 0;
    result.path_found = false;
    return result;
}

/**
 * @brief Finds the best connecting road edge between adjacent nodes u and v.
 */
static const RoadEdge* find_connecting_edge(const Graph* graph, int u, int v, DijkstraOptimizationMode mode) {
    const RoadEdge* best = NULL;
    double best_weight = DBL_MAX;

    for (const RoadEdge* e = graph->adj_lists[u]; e != NULL; e = e->next) {
        if (e->target_node == v) {
            double w = calculate_edge_cost(e, (int)mode);
            if (w < best_weight) {
                best_weight = w;
                best = e;
            }
        }
    }
    return best;
}

DijkstraResult dijkstra_find_path(const Graph* graph, int source, int destination, DijkstraOptimizationMode mode) {
    /* 1. Validate inputs */
    if (!graph || source < 0 || source >= graph->num_vertices ||
        destination < 0 || destination >= graph->num_vertices) {
        fprintf(stderr, "Error: Invalid graph or intersection index (source: %d, destination: %d).\n", 
                source, destination);
        return make_empty_result();
    }

    int V = graph->num_vertices;

    /* 2. Edge Case: Identical source and destination */
    if (source == destination) {
        DijkstraResult result;
        result.total_distance = 0.0;
        result.estimated_travel_time = 0.0;
        result.eco_cost = 0.0;
        result.node_count = 1;
        result.path_found = true;
        result.path = (int*)malloc(sizeof(int));
        if (!result.path) {
            fprintf(stderr, "Error: Memory allocation failed for single-node path.\n");
            return make_empty_result();
        }
        result.path[0] = source;
        return result;
    }

    /* 3. Allocate Dijkstra auxiliary tracking arrays */
    double* dist = (double*)malloc((size_t)V * sizeof(double));
    int* parent = (int*)malloc((size_t)V * sizeof(int));
    bool* visited = (bool*)calloc((size_t)V, sizeof(bool));
    MinHeap* min_heap = create_min_heap(V);

    if (!dist || !parent || !visited || !min_heap) {
        fprintf(stderr, "Error: Memory allocation failed for Dijkstra algorithm buffers.\n");
        free(dist);
        free(parent);
        free(visited);
        if (min_heap) free_min_heap(min_heap);
        return make_empty_result();
    }

    /* 4. Initialize distances, predecessor array, and min-heap */
    for (int v = 0; v < V; v++) {
        dist[v] = DBL_MAX;
        parent[v] = -1;
        insert_min_heap(min_heap, v, dist[v]);
    }

    /* Distance to source is 0 */
    dist[source] = 0.0;
    decrease_key(min_heap, source, 0.0);

    /* 5. Priority Queue Dijkstra processing */
    while (!is_empty_min_heap(min_heap)) {
        HeapNode min_node = extract_min(min_heap);
        int u = min_node.node_id;

        /* Cycle / Redundancy Prevention: Skip if already closed */
        if (visited[u]) continue;
        visited[u] = true;

        /* If remaining nodes in queue have infinite cost, they are unreachable */
        if (dist[u] == DBL_MAX) {
            break;
        }

        /* Early termination if destination reached */
        if (u == destination) {
            break;
        }

        /* Relax all outgoing edges from node u */
        for (const RoadEdge* edge = graph->adj_lists[u]; edge != NULL; edge = edge->next) {
            int v = edge->target_node;

            /* Cycle Prevention: do not relax already settled nodes */
            if (visited[v]) continue;

            /* Calculate weight based on selected optimization mode:
             * - Shortest Distance: physical road length (km)
             * - Fastest Time: travel time = (distance / speed) * congestion_factor (hours)
             * - Eco / Fuel Saver: distance * (1.0 + (congestion_factor - 1.0) * 0.7)
             */
            double weight = calculate_edge_cost(edge, (int)mode);

            if (dist[u] + weight < dist[v]) {
                dist[v] = dist[u] + weight;
                parent[v] = u;
                decrease_key(min_heap, v, dist[v]);
            }
        }
    }

    /* 6. Edge Case: Unreachable destination */
    if (!visited[destination] || dist[destination] == DBL_MAX || parent[destination] == -1) {
        free(dist);
        free(parent);
        free(visited);
        free_min_heap(min_heap);
        return make_empty_result();
    }

    /* 7. Path reconstruction using parent array */
    int* temp_path = (int*)malloc((size_t)V * sizeof(int));
    if (!temp_path) {
        fprintf(stderr, "Error: Memory allocation failed during path reconstruction.\n");
        free(dist);
        free(parent);
        free(visited);
        free_min_heap(min_heap);
        return make_empty_result();
    }

    int count = 0;
    int curr = destination;

    /* Trace backwards from destination to source with cycle safeguard (limit to V steps) */
    while (curr != -1 && count < V) {
        temp_path[count++] = curr;
        if (curr == source) break;
        curr = parent[curr];
    }

    /* Ensure back-trace successfully reached the source */
    if (temp_path[count - 1] != source) {
        fprintf(stderr, "Error: Incomplete or broken path reconstruction.\n");
        free(temp_path);
        free(dist);
        free(parent);
        free(visited);
        free_min_heap(min_heap);
        return make_empty_result();
    }

    /* 8. Construct final DijkstraResult with ordered path [source -> ... -> destination] */
    DijkstraResult result;
    result.node_count = count;
    result.path_found = true;
    result.path = (int*)malloc((size_t)count * sizeof(int));

    if (!result.path) {
        fprintf(stderr, "Error: Memory allocation failed for ordered path array.\n");
        free(temp_path);
        free(dist);
        free(parent);
        free(visited);
        free_min_heap(min_heap);
        return make_empty_result();
    }

    for (int i = 0; i < count; i++) {
        result.path[i] = temp_path[count - 1 - i];
    }

    /* 9. Compute exact cumulative metrics (total_distance, travel_time, and eco_cost) along path */
    result.total_distance = 0.0;
    result.estimated_travel_time = 0.0;
    result.eco_cost = 0.0;

    for (int i = 0; i < count - 1; i++) {
        int u = result.path[i];
        int v = result.path[i + 1];
        const RoadEdge* edge = find_connecting_edge(graph, u, v, mode);
        if (edge) {
            result.total_distance += edge->distance;
            result.estimated_travel_time += calculate_edge_travel_time(edge);
            result.eco_cost += calculate_edge_eco_cost(edge);
        }
    }

    /* 10. Clean up internal scratch memory */
    free(temp_path);
    free(dist);
    free(parent);
    free(visited);
    free_min_heap(min_heap);

    return result;
}

void free_dijkstra_result(DijkstraResult* result) {
    if (!result) return;
    if (result->path) {
        free(result->path);
        result->path = NULL;
    }
    result->node_count = 0;
    result->path_found = false;
    result->total_distance = -1.0;
    result->estimated_travel_time = -1.0;
    result->eco_cost = -1.0;
}

void print_dijkstra_result(const DijkstraResult* result, int source, int destination, DijkstraOptimizationMode mode) {
    const char* mode_str = "Shortest Physical Distance";
    if (mode == OPTIMIZE_FASTEST_TIME) {
        mode_str = "Fastest Travel Time";
    } else if (mode == OPTIMIZE_ECO_FUEL) {
        mode_str = "Eco / Fuel Saver (Idling & Stop-Go Loss Minimization)";
    }
    printf(">> Route Query: Node [%d] -> Node [%d] | Mode: %s\n", source, destination, mode_str);

    if (!result || !result->path_found) {
        printf("   Status: UNREACHABLE (No path exists between node %d and %d)\n\n", source, destination);
        return;
    }

    printf("   Status: ROUTE FOUND\n");
    printf("   Nodes Traversed : %d\n", result->node_count);
    printf("   Total Distance  : %.2f km\n", result->total_distance);
    printf("   Estimated Time  : %.2f hours (%.1f minutes)\n", 
           result->estimated_travel_time, result->estimated_travel_time * 60.0);
    printf("   Eco Cost Index  : %.2f\n", result->eco_cost);
    printf("   Optimal Path    : ");

    for (int i = 0; i < result->node_count; i++) {
        printf("[%d]%s", result->path[i], (i < result->node_count - 1) ? " -> " : "");
    }
    printf("\n\n");
}
