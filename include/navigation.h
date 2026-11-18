#ifndef NAVIGATION_H
#define NAVIGATION_H

#include "graph.h"
#include "dijkstra.h"
#include <stdbool.h>

/**
 * @brief Result structure for Multi-Stop TSP / Waypoint Routing.
 */
typedef struct WaypointRouteResult {
    DijkstraResult complete_route;  /**< Full unified path merged across all legs */
    int* waypoint_order;            /**< Sequence of waypoints visited in greedy order */
    int waypoint_count;             /**< Number of waypoints */
    bool success;                   /**< True if all legs were found and connected */
} WaypointRouteResult;

/**
 * @brief Result structure for top distinct alternative routes.
 */
typedef struct AlternativeRoutesResult {
    DijkstraResult primary_route;   /**< Optimal primary route */
    DijkstraResult alternatives[2]; /**< Up to 2 distinct alternative routes */
    int alt_count;                  /**< Number of distinct alternative routes found (0, 1, or 2) */
} AlternativeRoutesResult;

/**
 * @brief Plans a route visiting all specified waypoints before reaching the final destination.
 * 
 * Uses a greedy nearest-neighbor heuristic with Dijkstra's algorithm to determine
 * an efficient visit order and stitches the legs into a single continuous path.
 * 
 * @param graph Pointer to the road network Graph.
 * @param source Starting intersection ID.
 * @param destination Final destination intersection ID.
 * @param waypoints Array of intermediate intersection IDs (e.g. 2 to 4 stops).
 * @param waypoint_count Number of waypoints in the array.
 * @param mode Optimization mode (OPTIMIZE_SHORTEST_DISTANCE or OPTIMIZE_FASTEST_TIME).
 * @return WaypointRouteResult containing the merged route and visit sequence.
 */
WaypointRouteResult plan_multistop_route(const Graph* graph, int source, int destination, 
                                        const int* waypoints, int waypoint_count, 
                                        DijkstraOptimizationMode mode);

/**
 * @brief Frees dynamically allocated memory within a WaypointRouteResult.
 * 
 * @param result Pointer to WaypointRouteResult to clean up.
 */
void free_waypoint_route_result(WaypointRouteResult* result);

/**
 * @brief Prints a human-readable summary of the multi-stop routing itinerary.
 * 
 * @param result Pointer to WaypointRouteResult.
 * @param source Source node ID.
 * @param destination Destination node ID.
 * @param mode Optimization mode used.
 */
void print_waypoint_route_result(const WaypointRouteResult* result, int source, int destination, 
                                DijkstraOptimizationMode mode);

/**
 * @brief Finds the primary optimal route and top 2 distinct alternative routes.
 * 
 * Implements the Penalty Method: progressively penalizes previously traversed edges
 * to guide Dijkstra into discovering distinct corridors, and evaluates each route's
 * true metrics against unpenalized road attributes.
 * 
 * @param graph Pointer to the road network Graph.
 * @param source Starting intersection ID.
 * @param destination Destination intersection ID.
 * @param mode Optimization mode (OPTIMIZE_SHORTEST_DISTANCE or OPTIMIZE_FASTEST_TIME).
 * @return AlternativeRoutesResult with primary route and up to 2 distinct alternatives.
 */
AlternativeRoutesResult find_alternative_routes(Graph* graph, int source, int destination, 
                                               DijkstraOptimizationMode mode);

/**
 * @brief Frees dynamically allocated memory within an AlternativeRoutesResult.
 * 
 * @param result Pointer to AlternativeRoutesResult to clean up.
 */
void free_alternative_routes_result(AlternativeRoutesResult* result);

/**
 * @brief Prints a formatted comparison between the primary route and alternative routes.
 * 
 * @param result Pointer to AlternativeRoutesResult.
 * @param source Source node ID.
 * @param destination Destination node ID.
 * @param mode Optimization mode used.
 */
void print_alternative_routes_result(const AlternativeRoutesResult* result, int source, int destination, 
                                    DijkstraOptimizationMode mode);

/**
 * @brief Dynamic congestion injection and immediate path recalculation.
 * 
 * Mutates traffic on edge (u, v) and re-runs Dijkstra from source to destination.
 * 
 * @param graph Pointer to Graph.
 * @param u Source intersection ID.
 * @param v Destination intersection ID.
 * @param new_factor Updated congestion factor (clamped 1.0 to 3.0).
 * @param source Route source ID.
 * @param destination Route destination ID.
 * @param mode Optimization criterion.
 * @return Newly recalculated DijkstraResult.
 */
DijkstraResult update_traffic_and_reroute(Graph* graph, int u, int v, float new_factor,
                                         int source, int destination, DijkstraOptimizationMode mode);

#endif /* NAVIGATION_H */
