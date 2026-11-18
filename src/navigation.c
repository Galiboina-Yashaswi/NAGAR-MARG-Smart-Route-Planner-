#include "../include/navigation.h"
#include <stdio.h>
#include <stdlib.h>
#include <float.h>

#define PENALTY_MULTIPLIER 2.5

/**
 * @brief Checks if two Dijkstra routes follow the identical sequence of node IDs.
 */
static bool are_routes_identical(const DijkstraResult* r1, const DijkstraResult* r2) {
    if (!r1 || !r2) return false;
    if (!r1->path_found || !r2->path_found) return false;
    if (r1->node_count != r2->node_count) return false;

    for (int i = 0; i < r1->node_count; i++) {
        if (r1->path[i] != r2->path[i]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Recalculates exact unpenalized metrics (total_distance, estimated_travel_time, eco_cost)
 * along a given node sequence using current graph edge parameters.
 */
static void recalculate_route_metrics(const Graph* graph, DijkstraResult* route) {
    if (!graph || !route || !route->path_found || route->node_count <= 1) {
        return;
    }

    route->total_distance = 0.0;
    route->estimated_travel_time = 0.0;
    route->eco_cost = 0.0;

    for (int i = 0; i < route->node_count - 1; i++) {
        int u = route->path[i];
        int v = route->path[i + 1];

        for (const RoadEdge* edge = graph->adj_lists[u]; edge != NULL; edge = edge->next) {
            if (edge->target_node == v) {
                route->total_distance += edge->distance;
                route->estimated_travel_time += calculate_edge_travel_time(edge);
                route->eco_cost += calculate_edge_eco_cost(edge);
                break;
            }
        }
    }
}

/* =========================================================================
 * 1. Multi-Stop TSP / Waypoint Routing
 * ========================================================================= */

WaypointRouteResult plan_multistop_route(const Graph* graph, int source, int destination, 
                                        const int* waypoints, int waypoint_count, 
                                        DijkstraOptimizationMode mode) {
    WaypointRouteResult result;
    result.complete_route.path = NULL;
    result.complete_route.node_count = 0;
    result.complete_route.total_distance = -1.0;
    result.complete_route.estimated_travel_time = -1.0;
    result.complete_route.path_found = false;
    result.waypoint_order = NULL;
    result.waypoint_count = 0;
    result.success = false;

    /* 1. Validate inputs */
    if (!graph || source < 0 || source >= graph->num_vertices ||
        destination < 0 || destination >= graph->num_vertices) {
        fprintf(stderr, "Error: Invalid source/destination node in multi-stop planning.\n");
        return result;
    }

    if (waypoint_count < 0) {
        fprintf(stderr, "Error: Invalid waypoint count (%d).\n", waypoint_count);
        return result;
    }

    /* 0 waypoints edge case: fallback to standard Dijkstra */
    if (waypoint_count == 0 || waypoints == NULL) {
        result.complete_route = dijkstra_find_path(graph, source, destination, mode);
        result.success = result.complete_route.path_found;
        return result;
    }

    /* Validate all waypoint vertex indices */
    for (int i = 0; i < waypoint_count; i++) {
        if (waypoints[i] < 0 || waypoints[i] >= graph->num_vertices) {
            fprintf(stderr, "Error: Invalid waypoint index [%d] = %d.\n", i, waypoints[i]);
            return result;
        }
    }

    /* 2. Track visited waypoints and chosen order */
    bool* visited = (bool*)calloc((size_t)waypoint_count, sizeof(bool));
    int* order = (int*)malloc((size_t)waypoint_count * sizeof(int));
    int total_legs = waypoint_count + 1;
    DijkstraResult* legs = (DijkstraResult*)malloc((size_t)total_legs * sizeof(DijkstraResult));

    if (!visited || !order || !legs) {
        fprintf(stderr, "Error: Memory allocation failed in multi-stop planner.\n");
        free(visited);
        free(order);
        free(legs);
        return result;
    }

    int current_location = source;

    /* 3. Greedy Nearest-Neighbor Heuristic over intermediate waypoints */
    for (int step = 0; step < waypoint_count; step++) {
        int best_idx = -1;
        double best_cost = DBL_MAX;
        DijkstraResult best_candidate;
        best_candidate.path_found = false;

        for (int i = 0; i < waypoint_count; i++) {
            if (visited[i]) continue;

            int target_wp = waypoints[i];
            DijkstraResult cand = dijkstra_find_path(graph, current_location, target_wp, mode);

            if (cand.path_found) {
                double cost = (mode == OPTIMIZE_FASTEST_TIME) ? cand.estimated_travel_time : cand.total_distance;
                if (cost < best_cost) {
                    if (best_candidate.path_found) {
                        free_dijkstra_result(&best_candidate);
                    }
                    best_cost = cost;
                    best_idx = i;
                    best_candidate = cand;
                    continue; /* keep best_candidate allocated */
                }
            }
            free_dijkstra_result(&cand);
        }

        if (best_idx == -1 || !best_candidate.path_found) {
            /* Next waypoint unreachable */
            fprintf(stderr, "Warning: Waypoint unreachable from node [%d].\n", current_location);
            for (int k = 0; k < step; k++) {
                free_dijkstra_result(&legs[k]);
            }
            free(visited);
            free(order);
            free(legs);
            return result;
        }

        visited[best_idx] = true;
        order[step] = waypoints[best_idx];
        legs[step] = best_candidate;
        current_location = waypoints[best_idx];
    }

    /* 4. Final leg: from last waypoint to final destination */
    DijkstraResult final_leg = dijkstra_find_path(graph, current_location, destination, mode);
    if (!final_leg.path_found) {
        fprintf(stderr, "Warning: Final destination [%d] unreachable from last waypoint [%d].\n",
                destination, current_location);
        for (int k = 0; k < waypoint_count; k++) {
            free_dijkstra_result(&legs[k]);
        }
        free(visited);
        free(order);
        free(legs);
        return result;
    }
    legs[waypoint_count] = final_leg;

    /* 5. Calculate total stitched path size (avoid duplicate junction vertices) */
    int total_nodes = 0;
    for (int l = 0; l < total_legs; l++) {
        if (l == 0) {
            total_nodes += legs[l].node_count;
        } else {
            /* Skip the starting node of subsequent legs since it matches prior leg's destination */
            total_nodes += (legs[l].node_count > 1) ? (legs[l].node_count - 1) : 0;
        }
    }

    int* merged_path = (int*)malloc((size_t)total_nodes * sizeof(int));
    if (!merged_path) {
        fprintf(stderr, "Error: Memory allocation failed for merged path array.\n");
        for (int l = 0; l < total_legs; l++) {
            free_dijkstra_result(&legs[l]);
        }
        free(visited);
        free(order);
        free(legs);
        return result;
    }

    /* 6. Stitch all legs into complete route */
    int write_pos = 0;
    double accum_dist = 0.0;
    double accum_time = 0.0;

    for (int l = 0; l < total_legs; l++) {
        accum_dist += legs[l].total_distance;
        accum_time += legs[l].estimated_travel_time;

        int start_idx = (l == 0) ? 0 : 1;
        for (int idx = start_idx; idx < legs[l].node_count; idx++) {
            merged_path[write_pos++] = legs[l].path[idx];
        }

        free_dijkstra_result(&legs[l]);
    }

    result.complete_route.path = merged_path;
    result.complete_route.node_count = write_pos;
    result.complete_route.total_distance = accum_dist;
    result.complete_route.estimated_travel_time = accum_time;
    result.complete_route.path_found = true;
    result.waypoint_order = order;
    result.waypoint_count = waypoint_count;
    result.success = true;

    free(visited);
    free(legs);

    return result;
}

void free_waypoint_route_result(WaypointRouteResult* result) {
    if (!result) return;
    free_dijkstra_result(&result->complete_route);
    if (result->waypoint_order) {
        free(result->waypoint_order);
        result->waypoint_order = NULL;
    }
    result->waypoint_count = 0;
    result->success = false;
}

void print_waypoint_route_result(const WaypointRouteResult* result, int source, int destination, 
                                DijkstraOptimizationMode mode) {
    const char* mode_str = (mode == OPTIMIZE_FASTEST_TIME) ? "Fastest Travel Time" : "Shortest Physical Distance";
    printf(">> Multi-Stop Route: Node [%d] -> Destinations -> Node [%d] | Mode: %s\n", 
           source, destination, mode_str);

    if (!result || !result->success) {
        printf("   Status: FAILED (One or more waypoints unreachable)\n\n");
        return;
    }

    printf("   Status: ROUTE FOUND\n");
    printf("   Visited Waypoints (%d stops): ", result->waypoint_count);
    for (int i = 0; i < result->waypoint_count; i++) {
        printf("[%d]%s", result->waypoint_order[i], (i < result->waypoint_count - 1) ? " -> " : "");
    }
    printf("\n");
    printf("   Total Stitched Nodes : %d\n", result->complete_route.node_count);
    printf("   Total Road Distance  : %.2f km\n", result->complete_route.total_distance);
    printf("   Total Travel Time    : %.2f hours (%.1f minutes)\n", 
           result->complete_route.estimated_travel_time, 
           result->complete_route.estimated_travel_time * 60.0);
    printf("   Full Path Itinerary  : ");
    for (int i = 0; i < result->complete_route.node_count; i++) {
        printf("[%d]%s", result->complete_route.path[i], 
               (i < result->complete_route.node_count - 1) ? " -> " : "");
    }
    printf("\n\n");
}

/* =========================================================================
 * 2. Alternative Paths (Penalty Method)
 * ========================================================================= */

typedef struct EdgeBackup {
    RoadEdge* edge;
    double original_dist;
} EdgeBackup;

/**
 * @brief Applies penalty multiplier to road edges in a given route, saving their original values.
 */
static void penalize_route_edges(Graph* graph, const DijkstraResult* route, 
                                EdgeBackup* backups, int* backup_count) {
    if (!graph || !route || !route->path_found || route->node_count <= 1) return;

    for (int i = 0; i < route->node_count - 1; i++) {
        int u = route->path[i];
        int v = route->path[i + 1];

        /* Forward edge */
        for (RoadEdge* edge = graph->adj_lists[u]; edge != NULL; edge = edge->next) {
            if (edge->target_node == v) {
                backups[*backup_count].edge = edge;
                backups[*backup_count].original_dist = edge->distance;
                (*backup_count)++;
                edge->distance *= PENALTY_MULTIPLIER;
                break;
            }
        }

        /* Reverse edge if undirected */
        if (!graph->is_directed) {
            for (RoadEdge* edge = graph->adj_lists[v]; edge != NULL; edge = edge->next) {
                if (edge->target_node == u) {
                    backups[*backup_count].edge = edge;
                    backups[*backup_count].original_dist = edge->distance;
                    (*backup_count)++;
                    edge->distance *= PENALTY_MULTIPLIER;
                    break;
                }
            }
        }
    }
}

/**
 * @brief Restores all penalized edges to their original distances.
 */
static void restore_edge_backups(EdgeBackup* backups, int backup_count) {
    for (int i = 0; i < backup_count; i++) {
        if (backups[i].edge) {
            backups[i].edge->distance = backups[i].original_dist;
        }
    }
}

AlternativeRoutesResult find_alternative_routes(Graph* graph, int source, int destination, 
                                               DijkstraOptimizationMode mode) {
    AlternativeRoutesResult result;
    result.alt_count = 0;
    result.primary_route.path_found = false;
    result.primary_route.path = NULL;
    result.alternatives[0].path_found = false;
    result.alternatives[0].path = NULL;
    result.alternatives[1].path_found = false;
    result.alternatives[1].path = NULL;

    if (!graph || source < 0 || source >= graph->num_vertices ||
        destination < 0 || destination >= graph->num_vertices) {
        return result;
    }

    /* 1. Calculate primary optimal route */
    result.primary_route = dijkstra_find_path(graph, source, destination, mode);
    if (!result.primary_route.path_found || result.primary_route.node_count <= 1) {
        return result; /* No route or same node */
    }

    /* Allocate buffer for edge backups (max 2 * V * 2 edges) */
    int max_backups = graph->num_vertices * 8;
    EdgeBackup* backups = (EdgeBackup*)malloc((size_t)max_backups * sizeof(EdgeBackup));
    if (!backups) {
        fprintf(stderr, "Error: Memory allocation failed for edge backups.\n");
        return result;
    }
    int backup_count = 0;

    /* 2. Penalize edges along primary route to discover Alternative 1 */
    penalize_route_edges(graph, &result.primary_route, backups, &backup_count);

    DijkstraResult alt1_cand = dijkstra_find_path(graph, source, destination, mode);

    if (alt1_cand.path_found && !are_routes_identical(&result.primary_route, &alt1_cand)) {
        result.alternatives[0] = alt1_cand;
        result.alt_count++;

        /* 3. Penalize Alternative 1 edges as well to discover Alternative 2 */
        penalize_route_edges(graph, &result.alternatives[0], backups, &backup_count);

        DijkstraResult alt2_cand = dijkstra_find_path(graph, source, destination, mode);

        if (alt2_cand.path_found && 
            !are_routes_identical(&result.primary_route, &alt2_cand) &&
            !are_routes_identical(&result.alternatives[0], &alt2_cand)) {
            result.alternatives[1] = alt2_cand;
            result.alt_count++;
        } else {
            if (alt2_cand.path_found) {
                free_dijkstra_result(&alt2_cand);
            }
        }
    } else {
        if (alt1_cand.path_found) {
            free_dijkstra_result(&alt1_cand);
        }
    }

    /* 4. Restore original graph edge distances */
    restore_edge_backups(backups, backup_count);
    free(backups);

    /* 5. Recalculate true unpenalized metrics for alternatives */
    for (int i = 0; i < result.alt_count; i++) {
        recalculate_route_metrics(graph, &result.alternatives[i]);
    }

    return result;
}

void free_alternative_routes_result(AlternativeRoutesResult* result) {
    if (!result) return;
    free_dijkstra_result(&result->primary_route);
    for (int i = 0; i < result->alt_count; i++) {
        free_dijkstra_result(&result->alternatives[i]);
    }
    result->alt_count = 0;
}

void print_alternative_routes_result(const AlternativeRoutesResult* result, int source, int destination, 
                                    DijkstraOptimizationMode mode) {
    const char* mode_str = "Shortest Physical Distance";
    if (mode == OPTIMIZE_FASTEST_TIME) {
        mode_str = "Fastest Travel Time";
    } else if (mode == OPTIMIZE_ECO_FUEL) {
        mode_str = "Eco / Fuel Saver (Idling & Stop-Go Loss Minimization)";
    }
    printf(">> Route Alternatives: Node [%d] -> Node [%d] | Mode: %s\n", 
           source, destination, mode_str);

    if (!result || !result->primary_route.path_found) {
        printf("   Status: UNREACHABLE (No primary route found)\n\n");
        return;
    }

    /* Primary route */
    printf("   [Optimal Route 1 (Primary)]:\n");
    printf("      Distance: %.2f km | Time: %.2f hrs (%.1f min) | Eco Cost: %.2f | Path: ",
           result->primary_route.total_distance,
           result->primary_route.estimated_travel_time,
           result->primary_route.estimated_travel_time * 60.0,
           result->primary_route.eco_cost);
    for (int i = 0; i < result->primary_route.node_count; i++) {
        printf("[%d]%s", result->primary_route.path[i], 
               (i < result->primary_route.node_count - 1) ? " -> " : "");
    }
    printf("\n");

    /* Alternatives */
    if (result->alt_count == 0) {
        printf("   No distinct alternative routes found.\n\n");
        return;
    }

    for (int a = 0; a < result->alt_count; a++) {
        const DijkstraResult* alt = &result->alternatives[a];
        printf("   [Alternative Route %d (2.5x Penalty Method)]:\n", a + 2);
        printf("      Distance: %.2f km | Time: %.2f hrs (%.1f min) | Eco Cost: %.2f | Path: ",
               alt->total_distance,
               alt->estimated_travel_time,
               alt->estimated_travel_time * 60.0,
               alt->eco_cost);
        for (int i = 0; i < alt->node_count; i++) {
            printf("[%d]%s", alt->path[i], (i < alt->node_count - 1) ? " -> " : "");
        }
        printf("\n");
    }
    printf("\n");
}

DijkstraResult update_traffic_and_reroute(Graph* graph, int u, int v, float new_factor,
                                         int source, int destination, DijkstraOptimizationMode mode) {
    update_edge_traffic(graph, u, v, (double)new_factor);
    return dijkstra_find_path(graph, source, destination, mode);
}

