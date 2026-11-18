#ifndef GRAPH_H
#define GRAPH_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Represents a road segment (directed edge) in the city map graph.
 */
typedef struct RoadEdge {
    int target_node;            /**< Destination intersection ID */
    double distance;           /**< Physical road length in km/meters (base weight) */
    double average_speed;      /**< Speed limit or typical free-flow speed in km/h */
    double congestion_factor;  /**< Dynamic traffic multiplier (clamped between 1.0 and 3.0) */
    char* road_name;           /**< Human-readable road or street name */
    struct RoadEdge* next;     /**< Pointer to the next edge in the adjacency list */
} RoadEdge;

/**
 * @brief Represents the road network map using an adjacency list.
 */
typedef struct Graph {
    int num_vertices;          /**< Total number of intersections (0 to num_vertices - 1) */
    bool is_directed;          /**< True if edges are one-way by default, false for bidirectional */
    RoadEdge** adj_lists;      /**< Array of pointers to adjacency lists of RoadEdges */
} Graph;

/**
 * @brief Allocates and initializes a new city map graph.
 * 
 * @param num_vertices Number of intersections (nodes).
 * @param is_directed True for directed graph, false for undirected (bidirectional roads).
 * @return Pointer to the allocated Graph, or NULL on allocation failure.
 */
Graph* create_graph(int num_vertices, bool is_directed);

/**
 * @brief Adds a road (edge) between two intersections.
 * 
 * In an undirected graph, this adds the reverse edge automatically with the same attributes.
 * Congestion factor is clamped within [1.0, 3.0].
 * 
 * @param graph Pointer to the Graph.
 * @param src Source intersection ID.
 * @param dest Destination intersection ID.
 * @param distance Length of the road.
 * @param average_speed Average free-flow speed.
 * @param congestion_factor Traffic multiplier (1.0 = clear, 3.0 = severe gridlock).
 * @param road_name Name of the road/avenue.
 * @return true on success, false on invalid parameters or memory failure.
 */
bool add_edge(Graph* graph, int src, int dest, double distance, 
              double average_speed, double congestion_factor, const char* road_name);

/**
 * @brief Calculates effective travel time over a road edge considering traffic congestion.
 * 
 * Formula: Effective Time = (Distance / Average Speed) * Congestion Factor
 * 
 * @param edge Pointer to the RoadEdge.
 * @return Effective travel time in hours.
 */
double calculate_edge_travel_time(const RoadEdge* edge);

/**
 * @brief Calculates eco / fuel consumption cost over a road edge.
 * 
 * Formula: Edge Cost = Distance * (1.0 + (Congestion Factor - 1.0) * 0.7)
 * Minimizes idling and stop-and-go fuel loss.
 * 
 * @param edge Pointer to the RoadEdge.
 * @return Eco / fuel cost index.
 */
double calculate_edge_eco_cost(const RoadEdge* edge);

/**
 * @brief Calculates edge cost based on the specified Dijkstra optimization mode.
 * 
 * - OPTIMIZE_SHORTEST_DISTANCE (0): Distance (ignores traffic)
 * - OPTIMIZE_FASTEST_TIME (1): (Distance / Base Speed) * Congestion Factor
 * - OPTIMIZE_ECO_FUEL (2): Distance * (1.0 + (Congestion Factor - 1.0) * 0.7)
 * 
 * @param edge Pointer to RoadEdge.
 * @param mode Optimization mode (0, 1, or 2).
 * @return Computed edge cost.
 */
double calculate_edge_cost(const RoadEdge* edge, int mode);

/**
 * @brief Prints an ASCII representation of the graph's adjacency list.
 * 
 * @param graph Pointer to the Graph.
 */
void print_graph(const Graph* graph);

/**
 * @brief Frees all allocated memory associated with the graph (nodes, edges, road names).
 * 
 * @param graph Pointer to the Graph to free.
 */
void free_graph(Graph* graph);

/**
 * @brief Updates the congestion factor for the road edge between u and v in real-time.
 * 
 * In an undirected graph, both u -> v and v -> u edges are updated.
 * Congestion factor is clamped within [1.0, 3.0].
 * 
 * @param graph Pointer to the Graph.
 * @param u Source intersection ID.
 * @param v Destination intersection ID.
 * @param new_congestion_factor Updated traffic multiplier.
 * @return true if the edge was found and updated, false otherwise.
 */
bool update_edge_traffic(Graph* graph, int u, int v, double new_congestion_factor);

/**
 * @brief Sets the active graph instance for global real-time traffic mutations.
 */
void set_active_traffic_graph(Graph* graph);

/**
 * @brief Gets the active graph instance.
 */
Graph* get_active_traffic_graph(void);

/**
 * @brief Real-time congestion injection on the active city graph.
 * 
 * Dynamically mutates edge costs between u and v to trigger instant rerouting.
 * 
 * @param u Source intersection ID.
 * @param v Destination intersection ID.
 * @param new_factor Updated congestion factor (clamped between 1.0 and 3.0).
 * @return true on success, false if invalid or no active graph.
 */
bool update_traffic(int u, int v, float new_factor);

/**
 * @brief Real-time congestion injection on an explicit graph instance.
 */
bool update_graph_traffic(Graph* graph, int u, int v, float new_factor);

/* =========================================================================
 * Hyderabad Master Metropolitan Graph Dataset (25+ Intersections)
 * ========================================================================= */

/**
 * @brief Represents a real-world city intersection landmark with geographic coordinates.
 */
typedef struct CityNodeDef {
    int id;
    const char* name;
    double lat;
    double lng;
    const char* category;
} CityNodeDef;

/**
 * @brief Represents a physical road corridor segment.
 */
typedef struct CityEdgeDef {
    int source;
    int target;
    double distance;
    double speed;
    double congestion;
    const char* road_name;
} CityEdgeDef;

#define HYDERABAD_TOTAL_NODES 20
#define HYDERABAD_TOTAL_EDGES 28

extern const CityNodeDef HYDERABAD_NODES[HYDERABAD_TOTAL_NODES];
extern const CityEdgeDef HYDERABAD_EDGES[HYDERABAD_TOTAL_EDGES];

/**
 * @brief Constructs and populates the refactored 20-node Hyderabad metropolitan graph.
 * 
 * Accurately models the 20 clean areas with exact Google Maps coordinates
 * and strict dual-shore lake perimeter corridors around Hussain Sagar without intersecting water.
 * 
 * @return Allocated Graph* ready for routing operations.
 */
Graph* create_hyderabad_graph(void);

#endif /* GRAPH_H */

