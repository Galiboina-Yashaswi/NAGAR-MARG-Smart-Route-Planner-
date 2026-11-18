#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "../include/graph.h"
#include "../include/dijkstra.h"
#include "../include/navigation.h"

#define MAX_NODES 256
#define MAX_WAYPOINTS 32
#define BUFFER_SIZE 65536

typedef struct {
    int id;
    char name[128];
    double lat;
    double lng;
    char category[64];
} NodeMetadata;

typedef struct {
    int u;
    int v;
    double congestion;
} TrafficUpdate;

#define MAX_TRAFFIC_UPDATES 64

typedef struct {
    int source;
    int destination;
    DijkstraOptimizationMode mode;
    int waypoints[MAX_WAYPOINTS];
    int waypoint_count;
    bool request_alternatives;
    char map_file[256];
    TrafficUpdate traffic_updates[MAX_TRAFFIC_UPDATES];
    int traffic_update_count;
} RouteQuery;


/* Dynamic node metadata cache loaded from map_data.json */
static NodeMetadata g_nodes[MAX_NODES];
static int g_node_count = 0;

/* Helper: skip whitespace */
static const char* skip_ws(const char* p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Helper: read string value between quotes */
static const char* extract_json_string(const char* p, char* out, size_t max_len) {
    p = strchr(p, '\"');
    if (!p) return NULL;
    p++; /* skip opening quote */
    size_t i = 0;
    while (*p && *p != '\"' && i < max_len - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
        }
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p == '\"') p++;
    return p;
}

/* Helper: read number (int or double) */
static const char* extract_json_number(const char* p, double* out_val) {
    while (*p && !isdigit((unsigned char)*p) && *p != '-' && *p != '+') {
        p++;
    }
    if (!*p) return NULL;
    char* endptr = NULL;
    *out_val = strtod(p, &endptr);
    return endptr;
}

/* Read entire file into heap buffer */
static char* read_entire_file(const char* filepath) {
    FILE* fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz < 0) {
        fclose(fp);
        return NULL;
    }

    char* buffer = (char*)malloc((size_t)sz + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)sz, fp);
    buffer[bytes_read] = '\0';
    fclose(fp);
    return buffer;
}

/* Find node metadata by ID */
static const NodeMetadata* get_node_by_id(int id) {
    for (int i = 0; i < g_node_count; i++) {
        if (g_nodes[i].id == id) {
            return &g_nodes[i];
        }
    }
    return NULL;
}

/* Parse map_data.json into Graph* and g_nodes */
static Graph* load_map_from_json(const char* filepath) {
    char* content = read_entire_file(filepath);
    if (!content) {
        fprintf(stderr, "Warning: Could not open map file '%s', using fallback map.\n", filepath);
        return NULL;
    }

    /* 1. Parse "nodes" array */
    const char* p = strstr(content, "\"nodes\"");
    if (!p) {
        free(content);
        return NULL;
    }
    p = strchr(p, '[');
    if (!p) {
        free(content);
        return NULL;
    }
    p++;

    int max_id = -1;
    g_node_count = 0;

    while (*p && *p != ']') {
        p = strchr(p, '{');
        if (!p) break;
        const char* end_obj = strchr(p, '}');
        if (!end_obj) break;

        NodeMetadata node;
        memset(&node, 0, sizeof(node));
        node.id = -1;

        const char* id_ptr = strstr(p, "\"id\"");
        if (id_ptr && id_ptr < end_obj) {
            double v;
            extract_json_number(id_ptr + 4, &v);
            node.id = (int)v;
        }

        const char* name_ptr = strstr(p, "\"name\"");
        if (name_ptr && name_ptr < end_obj) {
            extract_json_string(name_ptr + 6, node.name, sizeof(node.name));
        }

        const char* lat_ptr = strstr(p, "\"lat\"");
        if (lat_ptr && lat_ptr < end_obj) {
            extract_json_number(lat_ptr + 5, &node.lat);
        }

        const char* lng_ptr = strstr(p, "\"lng\"");
        if (lng_ptr && lng_ptr < end_obj) {
            extract_json_number(lng_ptr + 5, &node.lng);
        }

        const char* cat_ptr = strstr(p, "\"category\"");
        if (cat_ptr && cat_ptr < end_obj) {
            extract_json_string(cat_ptr + 10, node.category, sizeof(node.category));
        }

        if (node.id >= 0 && g_node_count < MAX_NODES) {
            g_nodes[g_node_count++] = node;
            if (node.id > max_id) {
                max_id = node.id;
            }
        }
        p = end_obj + 1;
    }

    if (g_node_count == 0 || max_id < 0) {
        free(content);
        return NULL;
    }

    Graph* graph = create_graph(max_id + 1, false);
    if (!graph) {
        free(content);
        return NULL;
    }

    /* 2. Parse "edges" array */
    p = strstr(content, "\"edges\"");
    if (p) {
        p = strchr(p, '[');
        if (p) {
            p++;
            while (*p && *p != ']') {
                p = strchr(p, '{');
                if (!p) break;
                const char* end_obj = strchr(p, '}');
                if (!end_obj) break;

                int src = -1, target = -1;
                double dist = 1.0, speed = 40.0, cong = 1.0;
                char road_name[128] = "City Road";

                const char* s_ptr = strstr(p, "\"source\"");
                if (s_ptr && s_ptr < end_obj) {
                    double v;
                    extract_json_number(s_ptr + 8, &v);
                    src = (int)v;
                }

                const char* t_ptr = strstr(p, "\"target\"");
                if (t_ptr && t_ptr < end_obj) {
                    double v;
                    extract_json_number(t_ptr + 8, &v);
                    target = (int)v;
                }

                const char* d_ptr = strstr(p, "\"distance\"");
                if (d_ptr && d_ptr < end_obj) {
                    extract_json_number(d_ptr + 10, &dist);
                }

                const char* sp_ptr = strstr(p, "\"speed\"");
                if (sp_ptr && sp_ptr < end_obj) {
                    extract_json_number(sp_ptr + 7, &speed);
                }

                const char* c_ptr = strstr(p, "\"congestion\"");
                if (c_ptr && c_ptr < end_obj) {
                    extract_json_number(c_ptr + 12, &cong);
                }

                const char* r_ptr = strstr(p, "\"road_name\"");
                if (r_ptr && r_ptr < end_obj) {
                    extract_json_string(r_ptr + 11, road_name, sizeof(road_name));
                }

                if (src >= 0 && target >= 0) {
                    add_edge(graph, src, target, dist, speed, cong, road_name);
                }

                p = end_obj + 1;
            }
        }
    }

    free(content);
    return graph;
}

/* Fallback internal city dataset if file missing */
/* Fallback internal city dataset if file missing */
static Graph* create_fallback_map(void) {
    g_node_count = HYDERABAD_TOTAL_NODES;
    for (int i = 0; i < HYDERABAD_TOTAL_NODES; i++) {
        g_nodes[i].id = HYDERABAD_NODES[i].id;
        strncpy(g_nodes[i].name, HYDERABAD_NODES[i].name, sizeof(g_nodes[i].name) - 1);
        g_nodes[i].name[sizeof(g_nodes[i].name) - 1] = '\0';
        g_nodes[i].lat = HYDERABAD_NODES[i].lat;
        g_nodes[i].lng = HYDERABAD_NODES[i].lng;
        strncpy(g_nodes[i].category, HYDERABAD_NODES[i].category, sizeof(g_nodes[i].category) - 1);
        g_nodes[i].category[sizeof(g_nodes[i].category) - 1] = '\0';
    }
    return create_hyderabad_graph();
}

/* Parse query from JSON string (from stdin) */
static bool parse_query_from_json(const char* json_str, RouteQuery* query) {
    query->source = -1;
    query->destination = -1;
    query->mode = OPTIMIZE_FASTEST_TIME;
    query->waypoint_count = 0;
    query->request_alternatives = false;
    strcpy(query->map_file, "map_data.json");

    const char* src_p = strstr(json_str, "\"source\"");
    if (src_p) {
        double v;
        extract_json_number(src_p + 8, &v);
        query->source = (int)v;
    }

    const char* dst_p = strstr(json_str, "\"destination\"");
    if (dst_p) {
        double v;
        extract_json_number(dst_p + 13, &v);
        query->destination = (int)v;
    }

    const char* mode_p = strstr(json_str, "\"mode\"");
    if (mode_p) {
        char mode_str[32] = {0};
        extract_json_string(mode_p + 6, mode_str, sizeof(mode_str));
        if (strstr(mode_str, "dist") != NULL || strcmp(mode_str, "0") == 0) {
            query->mode = OPTIMIZE_SHORTEST_DISTANCE;
        } else if (strstr(mode_str, "eco") != NULL || strstr(mode_str, "fuel") != NULL || strcmp(mode_str, "2") == 0) {
            query->mode = OPTIMIZE_ECO_FUEL;
        } else {
            query->mode = OPTIMIZE_FASTEST_TIME;
        }
    }

    const char* alt_p = strstr(json_str, "\"alternatives\"");
    if (alt_p) {
        if (strstr(alt_p, "true") != NULL) {
            query->request_alternatives = true;
        }
    }

    const char* map_p = strstr(json_str, "\"map_file\"");
    if (map_p) {
        extract_json_string(map_p + 10, query->map_file, sizeof(query->map_file));
    }

    const char* wp_p = strstr(json_str, "\"waypoints\"");
    if (wp_p) {
        const char* arr_start = strchr(wp_p, '[');
        const char* arr_end = strchr(wp_p, ']');
        if (arr_start && arr_end && arr_start < arr_end) {
            const char* curr = arr_start + 1;
            while (curr < arr_end && query->waypoint_count < MAX_WAYPOINTS) {
                curr = skip_ws(curr);
                if (*curr == ']' || !*curr) break;
                if (isdigit((unsigned char)*curr) || *curr == '-') {
                    char* next_p = NULL;
                    long val = strtol(curr, &next_p, 10);
                    query->waypoints[query->waypoint_count++] = (int)val;
                    curr = next_p;
                } else {
                    curr++;
                }
            }
        }
    }

    query->traffic_update_count = 0;

    const char* tu_p = strstr(json_str, "\"traffic_updates\"");
    if (tu_p) {
        const char* arr_start = strchr(tu_p, '[');
        const char* arr_end = strchr(tu_p, ']');
        if (arr_start && arr_end && arr_start < arr_end) {
            const char* curr = arr_start + 1;
            while (curr < arr_end && query->traffic_update_count < MAX_TRAFFIC_UPDATES) {
                curr = strchr(curr, '{');
                if (!curr || curr >= arr_end) break;
                const char* end_obj = strchr(curr, '}');
                if (!end_obj || end_obj > arr_end) break;

                int u = -1, v = -1;
                double cong = 1.0;
                const char* u_p = strstr(curr, "\"u\"");
                if (u_p && u_p < end_obj) {
                    double val; extract_json_number(u_p + 3, &val); u = (int)val;
                }
                const char* v_p = strstr(curr, "\"v\"");
                if (v_p && v_p < end_obj) {
                    double val; extract_json_number(v_p + 3, &val); v = (int)val;
                }
                const char* c_p = strstr(curr, "\"congestion\"");
                if (c_p && c_p < end_obj) {
                    extract_json_number(c_p + 12, &cong);
                }

                if (u >= 0 && v >= 0) {
                    query->traffic_updates[query->traffic_update_count].u = u;
                    query->traffic_updates[query->traffic_update_count].v = v;
                    query->traffic_updates[query->traffic_update_count].congestion = cong;
                    query->traffic_update_count++;
                }
                curr = end_obj + 1;
            }
        }
    }

    return (query->source >= 0 && query->destination >= 0);
}

/* Parse query from CLI arguments */
static bool parse_query_from_cli(int argc, char* argv[], RouteQuery* query) {
    query->source = -1;
    query->destination = -1;
    query->mode = OPTIMIZE_FASTEST_TIME;
    query->waypoint_count = 0;
    query->request_alternatives = false;
    strcpy(query->map_file, "map_data.json");

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            query->source = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--destination") == 0 && i + 1 < argc) {
            query->destination = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strstr(argv[i], "dist") != NULL || strcmp(argv[i], "0") == 0) {
                query->mode = OPTIMIZE_SHORTEST_DISTANCE;
            } else if (strstr(argv[i], "eco") != NULL || strstr(argv[i], "fuel") != NULL || strcmp(argv[i], "2") == 0) {
                query->mode = OPTIMIZE_ECO_FUEL;
            } else {
                query->mode = OPTIMIZE_FASTEST_TIME;
            }
        } else if (strcmp(argv[i], "--waypoints") == 0 && i + 1 < argc) {
            i++;
            char* token = strtok(argv[i], ",");
            while (token && query->waypoint_count < MAX_WAYPOINTS) {
                query->waypoints[query->waypoint_count++] = atoi(token);
                token = strtok(NULL, ",");
            }
        } else if (strcmp(argv[i], "--alternatives") == 0) {
            query->request_alternatives = true;
        } else if (strcmp(argv[i], "--map") == 0 && i + 1 < argc) {
            strncpy(query->map_file, argv[++i], sizeof(query->map_file) - 1);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return false;
        }
    }

    return (query->source >= 0 && query->destination >= 0);
}

/* Helper to find road edge between u and v */
static const RoadEdge* find_road_edge(const Graph* graph, int u, int v) {
    if (!graph || u < 0 || u >= graph->num_vertices) return NULL;
    for (const RoadEdge* e = graph->adj_lists[u]; e != NULL; e = e->next) {
        if (e->target_node == v) return e;
    }
    return NULL;
}

/* Print a route as JSON block */
static void print_route_json(const Graph* graph, const DijkstraResult* route, bool is_primary) {
    printf("    {\n");
    printf("      \"is_primary\": %s,\n", is_primary ? "true" : "false");
    printf("      \"path_found\": %s,\n", route->path_found ? "true" : "false");
    printf("      \"total_distance_km\": %.2f,\n", route->total_distance);
    printf("      \"estimated_time_hours\": %.4f,\n", route->estimated_travel_time);
    printf("      \"estimated_time_minutes\": %.2f,\n", route->estimated_travel_time * 60.0);
    printf("      \"eco_cost\": %.2f,\n", route->eco_cost);
    printf("      \"node_count\": %d,\n", route->node_count);

    /* Nodes array */
    printf("      \"nodes\": [\n");
    for (int i = 0; i < route->node_count; i++) {
        int nid = route->path[i];
        const NodeMetadata* meta = get_node_by_id(nid);
        printf("        { \"id\": %d, \"name\": \"%s\", \"lat\": %.6f, \"lng\": %.6f }%s\n",
               nid,
               meta ? meta->name : "Unknown Intersection",
               meta ? meta->lat : 0.0,
               meta ? meta->lng : 0.0,
               (i == route->node_count - 1) ? "" : ",");
    }
    printf("      ],\n");

    /* Legs array */
    printf("      \"legs\": [\n");
    for (int i = 0; i < route->node_count - 1; i++) {
        int u = route->path[i];
        int v = route->path[i + 1];
        const NodeMetadata* u_meta = get_node_by_id(u);
        const NodeMetadata* v_meta = get_node_by_id(v);
        const RoadEdge* edge = find_road_edge(graph, u, v);

        double dist = edge ? edge->distance : 0.0;
        double speed = edge ? edge->average_speed : 40.0;
        double cong = edge ? edge->congestion_factor : 1.0;
        double leg_time = edge ? (calculate_edge_travel_time(edge) * 60.0) : 0.0;
        double leg_eco = edge ? calculate_edge_eco_cost(edge) : 0.0;
        const char* road_name = edge ? edge->road_name : "Connecting Road";

        printf("        {\n");
        printf("          \"from_id\": %d,\n", u);
        printf("          \"from_name\": \"%s\",\n", u_meta ? u_meta->name : "Node");
        printf("          \"to_id\": %d,\n", v);
        printf("          \"to_name\": \"%s\",\n", v_meta ? v_meta->name : "Node");
        printf("          \"road_name\": \"%s\",\n", road_name);
        printf("          \"distance_km\": %.2f,\n", dist);
        printf("          \"speed_kmh\": %.1f,\n", speed);
        printf("          \"congestion_factor\": %.2f,\n", cong);
        printf("          \"time_minutes\": %.2f,\n", leg_time);
        printf("          \"eco_cost\": %.2f\n", leg_eco);
        printf("        }%s\n", (i == route->node_count - 2) ? "" : ",");
    }
    printf("      ]\n");
    printf("    }");
}

int main(int argc, char* argv[]) {
    RouteQuery query;
    bool has_query = false;

    /* 1. Try parsing CLI arguments */
    if (argc > 1) {
        has_query = parse_query_from_cli(argc, argv, &query);
    }

    /* 2. If no valid CLI query, check stdin */
    if (!has_query) {
        char stdin_buffer[BUFFER_SIZE];
        size_t total_read = 0;
        size_t n;
        while ((n = fread(stdin_buffer + total_read, 1, sizeof(stdin_buffer) - total_read - 1, stdin)) > 0) {
            total_read += n;
            if (total_read >= sizeof(stdin_buffer) - 1) break;
        }
        stdin_buffer[total_read] = '\0';

        if (total_read > 0) {
            has_query = parse_query_from_json(stdin_buffer, &query);
        }
    }

    if (!has_query) {
        /* Output help / JSON error */
        printf("{\n");
        printf("  \"success\": false,\n");
        printf("  \"error\": \"Invalid or missing arguments. Provide --source <id> --destination <id> or pipe JSON to stdin.\",\n");
        printf("  \"usage\": \"route_cli.exe --source <id> --destination <id> [--mode distance|time] [--waypoints id1,id2] [--alternatives] [--map map_data.json]\"\n");
        printf("}\n");
        return EXIT_FAILURE;
    }

    /* 3. Load Map */
    Graph* map = load_map_from_json(query.map_file);
    if (!map) {
        map = create_fallback_map();
        if (!map) {
            printf("{\n  \"success\": false,\n  \"error\": \"Failed to allocate city graph.\"\n}\n");
            return EXIT_FAILURE;
        }
    }

    /* Apply dynamic traffic updates if provided */
    for (int i = 0; i < query.traffic_update_count; i++) {
        update_edge_traffic(map, query.traffic_updates[i].u, query.traffic_updates[i].v, query.traffic_updates[i].congestion);
    }

    /* Validate source and destination */
    if (query.source < 0 || query.source >= map->num_vertices ||
        query.destination < 0 || query.destination >= map->num_vertices) {
        printf("{\n");
        printf("  \"success\": false,\n");
        printf("  \"error\": \"Source node %d or Destination node %d out of bounds (max %d).\"\n",
               query.source, query.destination, map->num_vertices - 1);
        printf("}\n");
        free_graph(map);
        return EXIT_FAILURE;
    }

    const NodeMetadata* src_meta = get_node_by_id(query.source);
    const NodeMetadata* dst_meta = get_node_by_id(query.destination);

    /* 4. Execute Route Search */
    if (query.waypoint_count > 0) {
        /* Multi-Stop Routing */
        WaypointRouteResult wp_res = plan_multistop_route(
            map, query.source, query.destination,
            query.waypoints, query.waypoint_count, query.mode
        );

        if (!wp_res.success || !wp_res.complete_route.path_found) {
            printf("{\n");
            printf("  \"success\": false,\n");
            printf("  \"error\": \"No continuous route could connect all specified waypoints.\"\n");
            printf("}\n");
            free_waypoint_route_result(&wp_res);
            free_graph(map);
            return EXIT_SUCCESS;
        }

        printf("{\n");
        printf("  \"success\": true,\n");
        printf("  \"routing_type\": \"multi_stop_waypoint\",\n");
        printf("  \"optimization_mode\": \"%s\",\n", query.mode == OPTIMIZE_FASTEST_TIME ? "fastest_time" : "shortest_distance");
        printf("  \"source\": { \"id\": %d, \"name\": \"%s\", \"lat\": %.6f, \"lng\": %.6f },\n",
               query.source, src_meta ? src_meta->name : "Source",
               src_meta ? src_meta->lat : 0.0, src_meta ? src_meta->lng : 0.0);
        printf("  \"destination\": { \"id\": %d, \"name\": \"%s\", \"lat\": %.6f, \"lng\": %.6f },\n",
               query.destination, dst_meta ? dst_meta->name : "Destination",
               dst_meta ? dst_meta->lat : 0.0, dst_meta ? dst_meta->lng : 0.0);
        printf("  \"waypoint_order\": [");
        for (int i = 0; i < wp_res.waypoint_count; i++) {
            printf("%d%s", wp_res.waypoint_order[i], (i == wp_res.waypoint_count - 1) ? "" : ", ");
        }
        printf("],\n");
        printf("  \"route\":\n");
        print_route_json(map, &wp_res.complete_route, true);
        printf("\n}\n");

        free_waypoint_route_result(&wp_res);
    } else if (query.request_alternatives) {
        /* Alternative Routes */
        AlternativeRoutesResult alt_res = find_alternative_routes(
            map, query.source, query.destination, query.mode
        );

        if (!alt_res.primary_route.path_found) {
            printf("{\n");
            printf("  \"success\": false,\n");
            printf("  \"error\": \"No reachable path between source and destination.\"\n");
            printf("}\n");
            free_alternative_routes_result(&alt_res);
            free_graph(map);
            return EXIT_SUCCESS;
        }

        printf("{\n");
        printf("  \"success\": true,\n");
        printf("  \"routing_type\": \"alternatives\",\n");
        printf("  \"optimization_mode\": \"%s\",\n", 
               query.mode == OPTIMIZE_FASTEST_TIME ? "fastest_time" : (query.mode == OPTIMIZE_ECO_FUEL ? "eco_fuel" : "shortest_distance"));
        printf("  \"source\": { \"id\": %d, \"name\": \"%s\", \"lat\": %.6f, \"lng\": %.6f },\n",
               query.source, src_meta ? src_meta->name : "Source",
               src_meta ? src_meta->lat : 0.0, src_meta ? src_meta->lng : 0.0);
        printf("  \"destination\": { \"id\": %d, \"name\": \"%s\", \"lat\": %.6f, \"lng\": %.6f },\n",
               query.destination, dst_meta ? dst_meta->name : "Destination",
               dst_meta ? dst_meta->lat : 0.0, dst_meta ? dst_meta->lng : 0.0);
        printf("  \"route\":\n");
        print_route_json(map, &alt_res.primary_route, true);
        printf(",\n");
        printf("  \"alternatives\": [\n");
        for (int i = 0; i < alt_res.alt_count; i++) {
            print_route_json(map, &alt_res.alternatives[i], false);
            if (i < alt_res.alt_count - 1) printf(",\n");
        }
        printf("\n  ]\n");
        printf("}\n");

        free_alternative_routes_result(&alt_res);
    } else {
        /* Standard Single-Pair Dijkstra */
        DijkstraResult res = dijkstra_find_path(map, query.source, query.destination, query.mode);

        if (!res.path_found) {
            printf("{\n");
            printf("  \"success\": false,\n");
            printf("  \"error\": \"No reachable path between source node %d and destination node %d.\"\n",
                   query.source, query.destination);
            printf("}\n");
            free_dijkstra_result(&res);
            free_graph(map);
            return EXIT_SUCCESS;
        }

        printf("{\n");
        printf("  \"success\": true,\n");
        printf("  \"routing_type\": \"standard\",\n");
        printf("  \"optimization_mode\": \"%s\",\n", 
               query.mode == OPTIMIZE_FASTEST_TIME ? "fastest_time" : (query.mode == OPTIMIZE_ECO_FUEL ? "eco_fuel" : "shortest_distance"));
        printf("  \"source\": { \"id\": %d, \"name\": \"%s\", \"lat\": %.6f, \"lng\": %.6f },\n",
               query.source, src_meta ? src_meta->name : "Source",
               src_meta ? src_meta->lat : 0.0, src_meta ? src_meta->lng : 0.0);
        printf("  \"destination\": { \"id\": %d, \"name\": \"%s\", \"lat\": %.6f, \"lng\": %.6f },\n",
               query.destination, dst_meta ? dst_meta->name : "Destination",
               dst_meta ? dst_meta->lat : 0.0, dst_meta ? dst_meta->lng : 0.0);
        printf("  \"route\":\n");
        print_route_json(map, &res, true);
        printf("\n}\n");

        free_dijkstra_result(&res);
    }

    free_graph(map);
    return EXIT_SUCCESS;
}
