/**
 * @file test_route.c
 * @brief Comprehensive Test Harness and Benchmarking Suite for Smart Route Planner
 * 
 * Verifies:
 * 1. Disconnected graph components (unreachable nodes & robust error handling)
 * 2. Dynamic edge traffic weight updates and automatic rerouting
 * 3. High-throughput performance benchmark on a 500-node random geometric graph
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include <time.h>

#include "include/graph.h"
#include "include/minheap.h"
#include "include/dijkstra.h"
#include "include/navigation.h"

/* Terminal Colors for test output */
#define COLOR_RESET  "\033[0m"
#define COLOR_GREEN  "\033[32m"
#define COLOR_RED    "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_CYAN   "\033[36m"
#define COLOR_BOLD   "\033[1m"

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(expr, msg) do { \
    g_tests_total++; \
    if (expr) { \
        printf("  [" COLOR_GREEN "PASS" COLOR_RESET "] %s\n", msg); \
        g_tests_passed++; \
    } else { \
        printf("  [" COLOR_RED "FAIL" COLOR_RESET "] %s (Line %d: %s)\n", msg, __LINE__, #expr); \
        assert(expr); \
    } \
} while (0)

/* =========================================================================
 * Test 1: Disconnected Graph Components & Reachability
 * ========================================================================= */
static void test_disconnected_components(void) {
    printf("\n" COLOR_BOLD "============================================================\n");
    printf("TEST SUITE 1: Disconnected Graph Components & Boundary Cases\n");
    printf("============================================================" COLOR_RESET "\n");

    /*
     * Graph topology:
     * Component A: [0] <---> [1] <---> [2]
     * Component B: [3] <---> [4]
     * Isolated Node: [5]
     */
    int num_vertices = 6;
    Graph* g = create_graph(num_vertices, false);
    assert(g != NULL);

    /* Component A edges */
    add_edge(g, 0, 1, 2.0, 40.0, 1.0, "Alpha Ave");
    add_edge(g, 1, 2, 3.0, 50.0, 1.0, "Beta Blvd");

    /* Component B edges */
    add_edge(g, 3, 4, 1.5, 30.0, 1.0, "Gamma Way");

    /* Node 5 has 0 edges */

    /* 1.1 Path within Component A should succeed */
    DijkstraResult res_a = dijkstra_find_path(g, 0, 2, OPTIMIZE_SHORTEST_DISTANCE);
    TEST_ASSERT(res_a.path_found == true, "Path found between connected nodes [0] -> [2] in Component A");
    TEST_ASSERT(res_a.node_count == 3, "Node count is 3 (0 -> 1 -> 2)");
    TEST_ASSERT(res_a.total_distance == 5.0, "Total distance is exactly 5.0 km");
    free_dijkstra_result(&res_a);

    /* 1.2 Path across Component A -> Component B must fail gracefully */
    DijkstraResult res_ab = dijkstra_find_path(g, 0, 3, OPTIMIZE_SHORTEST_DISTANCE);
    TEST_ASSERT(res_ab.path_found == false, "No path found between disconnected components [0] -> [3]");
    TEST_ASSERT(res_ab.path == NULL, "Result path pointer is NULL on failure");
    TEST_ASSERT(res_ab.node_count == 0, "Result node count is 0 on failure");
    free_dijkstra_result(&res_ab);

    /* 1.3 Path to isolated node [5] must fail gracefully */
    DijkstraResult res_iso = dijkstra_find_path(g, 1, 5, OPTIMIZE_FASTEST_TIME);
    TEST_ASSERT(res_iso.path_found == false, "No path found to isolated node [5]");
    free_dijkstra_result(&res_iso);

    /* 1.4 Self-loop / identical source & destination */
    DijkstraResult res_self = dijkstra_find_path(g, 4, 4, OPTIMIZE_SHORTEST_DISTANCE);
    TEST_ASSERT(res_self.path_found == true, "Path found for identical source & destination [4] -> [4]");
    TEST_ASSERT(res_self.total_distance == 0.0, "Distance is 0.0 km for identical source & destination");
    TEST_ASSERT(res_self.node_count == 1 && res_self.path[0] == 4, "Path contains only the source node [4]");
    free_dijkstra_result(&res_self);

    /* 1.5 Multi-stop routing with an unreachable waypoint must fail safely */
    int bad_waypoints[2] = {1, 5}; // Node 5 is unreachable
    WaypointRouteResult wp_res = plan_multistop_route(g, 0, 2, bad_waypoints, 2, OPTIMIZE_SHORTEST_DISTANCE);
    TEST_ASSERT(wp_res.success == false, "Multi-stop routing fails when any waypoint is unreachable");
    free_waypoint_route_result(&wp_res);

    free_graph(g);
}

/* =========================================================================
 * Test 2: Dynamic Traffic Congestion Updates & Rerouting
 * ========================================================================= */
static void test_dynamic_rerouting(void) {
    printf("\n" COLOR_BOLD "============================================================\n");
    printf("TEST SUITE 2: Dynamic Edge Traffic Updates & Rerouting Checks\n");
    printf("============================================================" COLOR_RESET "\n");

    /*
     * Diamond topology between Node [0] (Source) and Node [3] (Destination):
     * Corridor 1 (via Node 1): 0 -> 1 -> 3
     *   - 0 -> 1: dist = 4.0 km, speed = 40 km/h, cong = 1.0 => base time = 0.100 hr (6.0 min)
     *   - 1 -> 3: dist = 4.0 km, speed = 40 km/h, cong = 1.0 => base time = 0.100 hr (6.0 min)
     *   Total Corridor 1 base time = 12.0 min, base dist = 8.0 km
     * 
     * Corridor 2 (via Node 2 - Expressway): 0 -> 2 -> 3
     *   - 0 -> 2: dist = 6.0 km, speed = 60 km/h, cong = 1.0 => base time = 0.100 hr (6.0 min)
     *   - 2 -> 3: dist = 6.0 km, speed = 60 km/h, cong = 1.0 => base time = 0.100 hr (6.0 min)
     *   Total Corridor 2 base time = 12.0 min, base dist = 12.0 km
     * 
     * Initial tie-breaker: Corridor 1 has shorter distance (8.0 km vs 12.0 km).
     */
    Graph* g = create_graph(4, false);
    assert(g != NULL);

    add_edge(g, 0, 1, 4.0, 40.0, 1.0, "Corridor 1A");
    add_edge(g, 1, 3, 4.0, 40.0, 1.0, "Corridor 1B");

    add_edge(g, 0, 2, 7.0, 60.0, 1.0, "Highway 2A"); // 7 km @ 60 km/h = 7.0 min
    add_edge(g, 2, 3, 7.0, 60.0, 1.0, "Highway 2B"); // 7 km @ 60 km/h = 7.0 min (Total: 14.0 min)

    /* 2.1 Initial fastest route should be Corridor 1 (via node 1, 12 min vs 14 min) */
    DijkstraResult init_res = dijkstra_find_path(g, 0, 3, OPTIMIZE_FASTEST_TIME);
    TEST_ASSERT(init_res.path_found == true, "Initial fastest path found");
    TEST_ASSERT(init_res.node_count == 3 && init_res.path[1] == 1, 
                "Initial fastest route traverses Corridor 1 ([0] -> [1] -> [3])");
    double initial_time_min = init_res.estimated_travel_time * 60.0;
    TEST_ASSERT(fabs(initial_time_min - 12.0) < 0.01, "Initial travel time is 12.0 minutes");
    free_dijkstra_result(&init_res);

    /* 2.2 Simulate severe traffic gridlock on Corridor 1A [0 <-> 1]: cong = 3.0x */
    bool updated = update_edge_traffic(g, 0, 1, 3.0);
    TEST_ASSERT(updated == true, "Traffic update on edge [0 <-> 1] succeeded");

    /* With 3.0x congestion on 0->1, leg time becomes: (4.0/40.0)*3.0 = 0.3 hr = 18 min!
     * Total Corridor 1 time = 18 + 6 = 24.0 min.
     * Meanwhile, Highway 2 time is still 14.0 min.
     * Fastest path MUST dynamically divert to Highway 2 ([0] -> [2] -> [3])!
     */
    DijkstraResult diverted_res = dijkstra_find_path(g, 0, 3, OPTIMIZE_FASTEST_TIME);
    TEST_ASSERT(diverted_res.path_found == true, "Path found after traffic update");
    TEST_ASSERT(diverted_res.node_count == 3 && diverted_res.path[1] == 2, 
                "Traffic reroute successful: Engine diverted to Highway Corridor 2 ([0] -> [2] -> [3])");
    double diverted_time_min = diverted_res.estimated_travel_time * 60.0;
    TEST_ASSERT(fabs(diverted_time_min - 14.0) < 0.01, "Diverted travel time is 14.0 minutes (faster than 24.0 min)");
    free_dijkstra_result(&diverted_res);

    /* 2.3 But Optimization Mode: SHORTEST_DISTANCE should still pick Corridor 1 (8 km < 14 km) */
    DijkstraResult dist_res = dijkstra_find_path(g, 0, 3, OPTIMIZE_SHORTEST_DISTANCE);
    TEST_ASSERT(dist_res.path[1] == 1, 
                "Shortest distance mode remains on Corridor 1 regardless of congestion multiplier");
    TEST_ASSERT(fabs(dist_res.total_distance - 8.0) < 0.01, "Shortest distance is 8.0 km");
    free_dijkstra_result(&dist_res);

    /* 2.4 Test Eco / Fuel Saver Mode:
     * Corridor 1: 0->1 (cong=3.0, dist=4.0): eco = 4.0 * (1.0 + (3.0-1.0)*0.7) = 4.0 * 2.4 = 9.6
     *             1->3 (cong=1.0, dist=4.0): eco = 4.0 * 1.0 = 4.0
     *             Total Corridor 1 Eco Cost = 13.6
     * Highway 2 : 0->2 (cong=1.0, dist=7.0): eco = 7.0 * 1.0 = 7.0
     *             2->3 (cong=1.0, dist=7.0): eco = 7.0 * 1.0 = 7.0
     *             Total Highway 2 Eco Cost = 14.0
     * Since 13.6 < 14.0, Eco Saver picks Corridor 1!
     * Now if cong on 0->1 is 3.0 and cong on 1->3 is 2.0:
     * Corridor 1 Eco Cost = 9.6 + 4.0*(1.0+0.7) = 9.6 + 6.8 = 16.4 > 14.0!
     * Eco mode diverts to Highway 2!
     */
    update_edge_traffic(g, 1, 3, 2.0);
    DijkstraResult eco_res = dijkstra_find_path(g, 0, 3, OPTIMIZE_ECO_FUEL);
    TEST_ASSERT(eco_res.path_found == true && eco_res.path[1] == 2, 
                "Eco / Fuel Saver mode diverts to Highway 2 to prevent excessive idling loss");
    free_dijkstra_result(&eco_res);

    /* 2.5 Test update_traffic function with active graph binding */
    set_active_traffic_graph(g);
    bool dyn_update = update_traffic(0, 1, 1.0f);
    TEST_ASSERT(dyn_update == true, "update_traffic(0, 1, 1.0f) successfully mutates active graph edge");
    update_traffic(1, 3, 1.0f);

    /* 2.6 Restore traffic on Corridor 1 back to free-flow 1.0x */
    DijkstraResult restored_res = dijkstra_find_path(g, 0, 3, OPTIMIZE_FASTEST_TIME);
    TEST_ASSERT(restored_res.path[1] == 1, "After clearing traffic, route reverts back to Corridor 1");
    free_dijkstra_result(&restored_res);

    /* 2.7 Test 2.5x Penalty Method alternative routes */
    AlternativeRoutesResult alts = find_alternative_routes(g, 0, 3, OPTIMIZE_FASTEST_TIME);
    TEST_ASSERT(alts.primary_route.path_found == true, "Primary route found in alternative routes search");
    TEST_ASSERT(alts.alt_count >= 1, "2.5x penalty method successfully discovered distinct alternative corridor");
    TEST_ASSERT(alts.alternatives[0].path[1] == 2, "Alternative route correctly diverts via Highway 2");
    free_alternative_routes_result(&alts);

    free_graph(g);
}

/* =========================================================================
 * Test 3: Performance Benchmark on a 500-Node Random Geometric Graph
 * ========================================================================= */

typedef struct {
    double x;
    double y;
} Point2D;

static double euclidean_dist(Point2D a, Point2D b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}

static void test_500_node_benchmark(void) {
    printf("\n" COLOR_BOLD "============================================================\n");
    printf("TEST SUITE 3: Performance Benchmark on 500-Node Geometric Graph\n");
    printf("============================================================" COLOR_RESET "\n");

    const int NUM_NODES = 500;
    const double AREA_SIZE = 1000.0;  // 1000 x 1000 grid
    const double CONNECT_RADIUS = 95.0; // Connection threshold distance

    printf(">> Synthesizing %d-node Random Geometric Graph in [%.0f x %.0f] grid...\n", 
           NUM_NODES, AREA_SIZE, AREA_SIZE);

    srand(1337); // Deterministic seed for reproducible benchmarks

    Point2D* coords = (Point2D*)malloc(sizeof(Point2D) * NUM_NODES);
    assert(coords != NULL);

    for (int i = 0; i < NUM_NODES; i++) {
        coords[i].x = ((double)rand() / (double)RAND_MAX) * AREA_SIZE;
        coords[i].y = ((double)rand() / (double)RAND_MAX) * AREA_SIZE;
    }

    Graph* benchmark_graph = create_graph(NUM_NODES, false);
    assert(benchmark_graph != NULL);

    int total_edges = 0;
    for (int i = 0; i < NUM_NODES; i++) {
        for (int j = i + 1; j < NUM_NODES; j++) {
            double d = euclidean_dist(coords[i], coords[j]);
            if (d <= CONNECT_RADIUS) {
                double speed = 40.0 + ((double)rand() / (double)RAND_MAX) * 40.0; // 40-80 km/h
                double cong = 1.0 + ((double)rand() / (double)RAND_MAX) * 0.5;   // 1.0-1.5x
                add_edge(benchmark_graph, i, j, d / 100.0, speed, cong, "Urban Link");
                total_edges++;
            }
        }
    }

    printf("   Vertices: %d | Bidirectional Edges: %d (Avg degree: %.2f)\n\n",
           NUM_NODES, total_edges, (double)(total_edges * 2) / NUM_NODES);

    /* Run 200 random routing queries */
    const int NUM_QUERIES = 200;
    printf(">> Executing %d randomized Dijkstra shortest path queries...\n", NUM_QUERIES);

    clock_t start_time = clock();

    int reachable_count = 0;
    double cumulative_distance = 0.0;

    for (int q = 0; q < NUM_QUERIES; q++) {
        int src = rand() % NUM_NODES;
        int dst = rand() % NUM_NODES;
        DijkstraOptimizationMode mode = (q % 2 == 0) ? OPTIMIZE_FASTEST_TIME : OPTIMIZE_SHORTEST_DISTANCE;

        DijkstraResult res = dijkstra_find_path(benchmark_graph, src, dst, mode);
        if (res.path_found) {
            reachable_count++;
            cumulative_distance += res.total_distance;
        }
        free_dijkstra_result(&res);
    }

    clock_t end_time = clock();
    double total_time_sec = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    double avg_time_ms = (total_time_sec / NUM_QUERIES) * 1000.0;
    double queries_per_sec = (total_time_sec > 0.0) ? (NUM_QUERIES / total_time_sec) : 0.0;

    printf("\n" COLOR_BOLD ">> BENCHMARK RESULTS:" COLOR_RESET "\n");
    printf("   Total Queries:          %d\n", NUM_QUERIES);
    printf("   Reachable Paths Found:  %d (%.1f%%)\n", reachable_count, ((double)reachable_count / NUM_QUERIES) * 100.0);
    printf("   Total Execution Time:   " COLOR_CYAN "%.4f seconds" COLOR_RESET "\n", total_time_sec);
    printf("   Average Query Latency:  " COLOR_GREEN "%.3f ms (%.1f microseconds)" COLOR_RESET "\n", avg_time_ms, avg_time_ms * 1000.0);
    printf("   Throughput:             " COLOR_YELLOW "%.1f queries/second" COLOR_RESET "\n\n", queries_per_sec);

    TEST_ASSERT(reachable_count > (NUM_QUERIES / 2), "Graph connectivity confirmed (>50% pairs connected)");
    TEST_ASSERT(avg_time_ms < 10.0, "Average query latency is well below 10.0 ms SLA threshold");

    /* Also benchmark Alternative Routes (Penalty Method) on the 500-node graph */
    printf(">> Benchmarking Alternative Routes (Penalty Method) on 500-node graph...\n");
    clock_t alt_start = clock();
    AlternativeRoutesResult alt_res = find_alternative_routes(benchmark_graph, 10, 450, OPTIMIZE_FASTEST_TIME);
    clock_t alt_end = clock();
    double alt_time_ms = ((double)(alt_end - alt_start) / CLOCKS_PER_SEC) * 1000.0;

    printf("   Alternative Routes Time: " COLOR_CYAN "%.3f ms" COLOR_RESET " (Primary found: %s, Alts found: %d)\n",
           alt_time_ms, alt_res.primary_route.path_found ? "YES" : "NO", alt_res.alt_count);

    TEST_ASSERT(alt_time_ms < 25.0, "Alternative routes calculation completed in under 25 ms");
    free_alternative_routes_result(&alt_res);

    free(coords);
    free_graph(benchmark_graph);
}

/* =========================================================================
 * Main Test Runner
 * ========================================================================= */
int main(void) {
    printf(COLOR_BOLD "\n");
    printf("************************************************************\n");
    printf("   SMART ROUTE PLANNER - AUTOMATED TEST HARNESS             \n");
    printf("   Dijkstra, MinHeap, Multi-Stop TSP & Performance Engine   \n");
    printf("************************************************************\n" COLOR_RESET);

    test_disconnected_components();
    test_dynamic_rerouting();
    test_500_node_benchmark();

    printf("\n" COLOR_BOLD "============================================================\n");
    printf("TEST SUMMARY: %d / %d Tests Passed (%.1f%%)\n",
           g_tests_passed, g_tests_total, ((double)g_tests_passed / g_tests_total) * 100.0);
    printf("============================================================" COLOR_RESET "\n\n");

    if (g_tests_passed == g_tests_total) {
        printf(COLOR_GREEN COLOR_BOLD ">> ALL TESTS PASSED SUCCESSFULLY!" COLOR_RESET "\n\n");
        return EXIT_SUCCESS;
    } else {
        printf(COLOR_RED COLOR_BOLD ">> SOME TESTS FAILED!" COLOR_RESET "\n\n");
        return EXIT_FAILURE;
    }
}
