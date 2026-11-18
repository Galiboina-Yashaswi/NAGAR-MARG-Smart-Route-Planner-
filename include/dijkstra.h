#ifndef DIJKSTRA_H
#define DIJKSTRA_H

#include "graph.h"
#include <stdbool.h>

/**
 * @brief Routing optimization criteria for Dijkstra's algorithm.
 */
typedef enum DijkstraOptimizationMode {
    OPTIMIZE_SHORTEST_DISTANCE = 0, /**< Minimizes physical distance: Edge Cost = Distance (ignores traffic) */
    OPTIMIZE_FASTEST_TIME = 1,      /**< Minimizes travel time: Edge Cost = (Distance / Base Speed) * Congestion Factor */
    OPTIMIZE_ECO_FUEL = 2           /**< Eco / Fuel Saver: Edge Cost = Distance * (1.0 + (Congestion Factor - 1.0) * 1.8) */
} DijkstraOptimizationMode;

/**
 * @brief Result structure returned by Dijkstra's shortest path computation.
 */
typedef struct DijkstraResult {
    double total_distance;          /**< Total physical road distance in km */
    double estimated_travel_time;   /**< Total estimated travel time in hours */
    double eco_cost;                /**< Total eco / fuel consumption index */
    int* path;                      /**< Ordered array of intersection node IDs from source to destination */
    int node_count;                 /**< Number of nodes traversed along the path */
    bool path_found;                /**< True if a valid route was found, false otherwise */
} DijkstraResult;

/**
 * @brief Computes the optimal path between source and destination intersections.
 * 
 * Uses Dijkstra's algorithm backed by MinHeap priority queue.
 * 
 * @param graph Pointer to the road network Graph.
 * @param source Source intersection ID.
 * @param destination Destination intersection ID.
 * @param mode Optimization mode (OPTIMIZE_SHORTEST_DISTANCE or OPTIMIZE_FASTEST_TIME).
 * @return DijkstraResult containing path metrics, node array, and status.
 */
DijkstraResult dijkstra_find_path(const Graph* graph, int source, int destination, DijkstraOptimizationMode mode);

/**
 * @brief Frees memory allocated inside a DijkstraResult struct.
 * 
 * @param result Pointer to DijkstraResult whose path buffer will be freed.
 */
void free_dijkstra_result(DijkstraResult* result);

/**
 * @brief Prints a human-readable summary of the Dijkstra routing result.
 * 
 * @param result Pointer to DijkstraResult.
 * @param source Source node ID.
 * @param destination Destination node ID.
 * @param mode The mode that was used.
 */
void print_dijkstra_result(const DijkstraResult* result, int source, int destination, DijkstraOptimizationMode mode);

#endif /* DIJKSTRA_H */
