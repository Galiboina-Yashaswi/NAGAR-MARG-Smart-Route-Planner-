#include "../include/graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Helper function to safely duplicate strings across C standards.
 */
static char* safe_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

/**
 * @brief Clamps congestion factor strictly within [1.0, 3.0].
 */
static double clamp_congestion(double factor) {
    if (factor < 1.0) return 1.0;
    if (factor > 3.0) return 3.0;
    return factor;
}

/**
 * @brief Internal helper to allocate a new RoadEdge node.
 */
static RoadEdge* create_edge_node(int target_node, double distance, 
                                 double average_speed, double congestion_factor, 
                                 const char* road_name) {
    RoadEdge* new_edge = (RoadEdge*)malloc(sizeof(RoadEdge));
    if (!new_edge) {
        fprintf(stderr, "Error: Memory allocation failed for RoadEdge.\n");
        return NULL;
    }

    new_edge->target_node = target_node;
    new_edge->distance = distance;
    new_edge->average_speed = (average_speed > 0.0) ? average_speed : 40.0; /* default fallback speed */
    new_edge->congestion_factor = clamp_congestion(congestion_factor);
    new_edge->road_name = safe_strdup(road_name ? road_name : "Unnamed Road");
    new_edge->next = NULL;

    return new_edge;
}

Graph* create_graph(int num_vertices, bool is_directed) {
    if (num_vertices <= 0) {
        fprintf(stderr, "Error: Number of vertices must be greater than 0.\n");
        return NULL;
    }

    Graph* graph = (Graph*)malloc(sizeof(Graph));
    if (!graph) {
        fprintf(stderr, "Error: Memory allocation failed for Graph.\n");
        return NULL;
    }

    graph->num_vertices = num_vertices;
    graph->is_directed = is_directed;

    /* Allocate adjacency list array initialized to NULL */
    graph->adj_lists = (RoadEdge**)calloc((size_t)num_vertices, sizeof(RoadEdge*));
    if (!graph->adj_lists) {
        fprintf(stderr, "Error: Memory allocation failed for adjacency lists.\n");
        free(graph);
        return NULL;
    }

    return graph;
}

bool add_edge(Graph* graph, int src, int dest, double distance, 
              double average_speed, double congestion_factor, const char* road_name) {
    if (!graph || src < 0 || src >= graph->num_vertices || 
        dest < 0 || dest >= graph->num_vertices) {
        fprintf(stderr, "Error: Invalid graph or vertex indices (src: %d, dest: %d).\n", src, dest);
        return false;
    }

    /* 1. Add forward edge: src -> dest */
    RoadEdge* forward_edge = create_edge_node(dest, distance, average_speed, congestion_factor, road_name);
    if (!forward_edge) return false;

    forward_edge->next = graph->adj_lists[src];
    graph->adj_lists[src] = forward_edge;

    /* 2. If undirected, add reverse edge: dest -> src */
    if (!graph->is_directed) {
        RoadEdge* reverse_edge = create_edge_node(src, distance, average_speed, congestion_factor, road_name);
        if (!reverse_edge) return false;

        reverse_edge->next = graph->adj_lists[dest];
        graph->adj_lists[dest] = reverse_edge;
    }

    return true;
}

double calculate_edge_travel_time(const RoadEdge* edge) {
    if (!edge || edge->average_speed <= 0.0) return 0.0;
    /* Travel time in hours = (distance / speed) * congestion multiplier */
    return (edge->distance / edge->average_speed) * edge->congestion_factor;
}

double calculate_edge_eco_cost(const RoadEdge* edge) {
    if (!edge || edge->distance <= 0.0) return 0.0;
    /* Eco / Fuel Saver Criterion:
     * Edge Cost = Distance * (1.0 + (Congestion Factor - 1.0) * 0.7)
     * Minimizes fuel waste from idling and stop-and-go congestion cycles.
     */
    double congestion_delta = edge->congestion_factor - 1.0;
    if (congestion_delta < 0.0) congestion_delta = 0.0;
    return edge->distance * (1.0 + (congestion_delta * 1.8));
}

double calculate_edge_cost(const RoadEdge* edge, int mode) {
    if (!edge) return 0.0;
    if (mode == 1 /* OPTIMIZE_FASTEST_TIME */) {
        return calculate_edge_travel_time(edge);
    } else if (mode == 2 /* OPTIMIZE_ECO_FUEL */) {
        return calculate_edge_eco_cost(edge);
    } else {
        return edge->distance; /* OPTIMIZE_SHORTEST_DISTANCE */
    }
}

void print_graph(const Graph* graph) {
    if (!graph) return;

    printf("=== Smart Route Planner: Road Network Graph ===\n");
    printf("Vertices (Intersections): %d | Mode: %s\n", 
           graph->num_vertices, graph->is_directed ? "Directed" : "Undirected (Bidirectional)");

    for (int v = 0; v < graph->num_vertices; v++) {
        printf("Intersection [%d]:\n", v);
        RoadEdge* curr = graph->adj_lists[v];
        if (!curr) {
            printf("  └── (No outgoing roads)\n");
            continue;
        }

        while (curr) {
            double travel_time_min = calculate_edge_travel_time(curr) * 60.0;
            printf("  └───> [%d] | Road: %-18s | Dist: %6.2f km | Speed: %5.1f km/h | Congestion: %.2fx | Time: %5.1f min\n",
                   curr->target_node,
                   curr->road_name,
                   curr->distance,
                   curr->average_speed,
                   curr->congestion_factor,
                   travel_time_min);
            curr = curr->next;
        }
    }
    printf("===============================================\n\n");
}

void free_graph(Graph* graph) {
    if (!graph) return;

    if (graph->adj_lists) {
        for (int v = 0; v < graph->num_vertices; v++) {
            RoadEdge* curr = graph->adj_lists[v];
            while (curr) {
                RoadEdge* temp = curr;
                curr = curr->next;

                if (temp->road_name) {
                    free(temp->road_name);
                }
                free(temp);
            }
        }
        free(graph->adj_lists);
    }
    free(graph);
}

bool update_edge_traffic(Graph* graph, int u, int v, double new_congestion_factor) {
    if (!graph || u < 0 || u >= graph->num_vertices || 
        v < 0 || v >= graph->num_vertices) {
        fprintf(stderr, "Error: Invalid graph or vertices for traffic update (u: %d, v: %d).\n", u, v);
        return false;
    }

    double clamped_factor = clamp_congestion(new_congestion_factor);
    bool forward_found = false;

    /* Update forward edge u -> v */
    for (RoadEdge* edge = graph->adj_lists[u]; edge != NULL; edge = edge->next) {
        if (edge->target_node == v) {
            edge->congestion_factor = clamped_factor;
            forward_found = true;
            break;
        }
    }

    if (!forward_found) {
        return false;
    }

    /* If undirected, also update reverse edge v -> u */
    if (!graph->is_directed) {
        for (RoadEdge* edge = graph->adj_lists[v]; edge != NULL; edge = edge->next) {
            if (edge->target_node == u) {
                edge->congestion_factor = clamped_factor;
                break;
            }
        }
    }

    return true;
}

static Graph* g_active_traffic_graph = NULL;

void set_active_traffic_graph(Graph* graph) {
    g_active_traffic_graph = graph;
}

Graph* get_active_traffic_graph(void) {
    return g_active_traffic_graph;
}

bool update_traffic(int u, int v, float new_factor) {
    if (!g_active_traffic_graph) {
        fprintf(stderr, "Warning: update_traffic called but no active traffic graph is set.\n");
        return false;
    }
    return update_edge_traffic(g_active_traffic_graph, u, v, (double)new_factor);
}

bool update_graph_traffic(Graph* graph, int u, int v, float new_factor) {
    return update_edge_traffic(graph, u, v, (double)new_factor);
}

/* =========================================================================
 * Hyderabad Master Metropolitan Graph Dataset (20 Nodes, 24 Edges)
 * ========================================================================= */

const CityNodeDef HYDERABAD_NODES[HYDERABAD_TOTAL_NODES] = {
    { 0,  "Gachibowli",     17.4401, 78.3489, "Financial District" },
    { 1,  "HITEC City",     17.4474, 78.3762, "IT Hub" },
    { 2,  "Madhapur",       17.4483, 78.3915, "IT Corridor" },
    { 3,  "Jubilee Hills",  17.4319, 78.4073, "Western Hub" },
    { 4,  "Banjara Hills",  17.4156, 78.4350, "Central Hub" },
    { 5,  "Panjagutta",     17.4265, 78.4526, "Major Transit Core" },
    { 6,  "Ameerpet",       17.4375, 78.4483, "Metro Interchange" },
    { 7,  "Begumpet",       17.4448, 78.4674, "North Corridor" },
    { 8,  "Secunderabad",   17.4399, 78.4983, "Railway Hub" },
    { 9,  "Khairatabad",    17.4125, 78.4585, "West Shore Gateway" },
    { 10, "Necklace Road",  17.4310, 78.4635, "West Shore Lake Perimeter" },
    { 11, "Tank Bund",      17.4239, 78.4835, "East Shore Lake Perimeter" },
    { 12, "Lakdikapul",     17.4042, 78.4633, "Administrative Core" },
    { 13, "Mehdipatnam",    17.3916, 78.4398, "South-West Transit" },
    { 14, "Charminar",      17.3616, 78.4747, "Old City Core" },
    { 15, "Koti",           17.3824, 78.4867, "Central Trade Hub" },
    { 16, "Tarnaka",        17.4287, 78.5312, "University Zone" },
    { 17, "Amberpet",       17.3934, 78.5178, "East-Central Hub" },
    { 18, "Uppal",          17.4022, 78.5601, "Eastern Ring Road" },
    { 19, "LB Nagar",       17.3533, 78.5511, "South-East Highway Gateway" }
};

const CityEdgeDef HYDERABAD_EDGES[HYDERABAD_TOTAL_EDGES] = {
    { 0,  1,  5.2, 45.0, 1.0, "Gachibowli - HITEC City Rd" },
    { 1,  2,  2.1, 35.0, 1.0, "HITEC City - Madhapur Main Rd" },
    { 2,  3,  3.8, 35.0, 1.0, "Madhapur - Jubilee Hills Rd" },
    { 3,  4,  4.1, 40.0, 1.0, "Jubilee Hills - Banjara Hills Rd" },
    { 4,  5,  2.8, 30.0, 1.0, "Banjara Hills - Panjagutta Rd" },
    { 5,  6,  1.7, 25.0, 1.0, "Panjagutta - Ameerpet Rd" },
    { 6,  7,  3.0, 35.0, 1.0, "Ameerpet - Begumpet Rd" },
    { 7,  8,  3.4, 40.0, 1.0, "Begumpet - Secunderabad Rd" },
    { 4,  9,  3.2, 35.0, 1.0, "Banjara Hills - Khairatabad Rd" },
    { 9,  12, 1.4, 30.0, 1.0, "Khairatabad - Lakdikapul Flyover" },
    { 9,  10, 1.6, 40.0, 1.0, "Khairatabad - Necklace Rd (West Shore)" },
    { 10, 7,  2.4, 40.0, 1.0, "Necklace Rd - Begumpet (West Shore)" },
    { 12, 11, 2.6, 35.0, 1.0, "Lakdikapul - Tank Bund (East Shore)" },
    { 11, 8,  3.2, 40.0, 1.0, "Tank Bund - Secunderabad (East Shore)" },
    { 12, 13, 3.9, 35.0, 1.0, "Lakdikapul - Mehdipatnam Rd" },
    { 13, 14, 5.3, 30.0, 1.0, "Mehdipatnam - Charminar Rd" },
    { 12, 15, 3.1, 30.0, 1.0, "Lakdikapul - Koti Rd" },
    { 15, 14, 2.2, 25.0, 1.0, "Koti - Charminar Rd" },
    { 15, 17, 3.5, 30.0, 1.0, "Koti - Amberpet Rd" },
    { 8,  16, 4.2, 45.0, 1.0, "Secunderabad - Tarnaka Rd" },
    { 16, 18, 4.8, 50.0, 1.0, "Tarnaka - Uppal Rd" },
    { 17, 18, 4.5, 40.0, 1.0, "Amberpet - Uppal Rd" },
    { 17, 19, 6.8, 40.0, 1.0, "Amberpet - LB Nagar Rd" },
    { 18, 19, 6.5, 45.0, 1.0, "Uppal - LB Nagar Rd" },
    { 3,  5,  4.0, 35.0, 1.0, "Jubilee Hills - Panjagutta Rd (Rd 36)" },
    { 5,  9,  2.2, 35.0, 1.0, "Panjagutta - Khairatabad Rd" },
    { 4,  13, 3.4, 35.0, 1.0, "Banjara Hills - Mehdipatnam Rd" },
    { 7,  11, 3.6, 35.0, 1.0, "Begumpet - Tank Bund Rd (Minister Rd)" }
};

Graph* create_hyderabad_graph(void) {
    Graph* graph = create_graph(HYDERABAD_TOTAL_NODES, false);
    if (!graph) return NULL;

    for (int i = 0; i < HYDERABAD_TOTAL_EDGES; i++) {
        const CityEdgeDef* e = &HYDERABAD_EDGES[i];
        add_edge(graph, e->source, e->target, e->distance, e->speed, e->congestion, e->road_name);
    }

    set_active_traffic_graph(graph);
    return graph;
}

