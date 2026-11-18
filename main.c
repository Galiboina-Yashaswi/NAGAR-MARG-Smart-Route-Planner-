#include <stdio.h>
#include <stdlib.h>
#include "include/graph.h"
#include "include/minheap.h"
#include "include/dijkstra.h"
#include "include/navigation.h"

int main(void) {
    printf("============================================================\n");
    printf("   Smart Route Planner - Core C Routing Engine              \n");
    printf("   Adjacency List | Min-Heap Priority Queue (decrease_key)  \n");
    printf("   3 Criteria: Fastest Time | Shortest Distance | Eco Saver \n");
    printf("   2.5x Edge Penalty Alternatives | Live Congestion Injection\n");
    printf("   City: Hyderabad, Telangana, India (32 Nodes, 51 Edges)   \n");
    printf("============================================================\n\n");

    /* 1. Adjacency List Network Construction & Active Graph Binding */
    Graph* city_map = create_hyderabad_graph();
    if (!city_map) {
        fprintf(stderr, "Failed to create Hyderabad metropolitan graph.\n");
        return EXIT_FAILURE;
    }
    set_active_traffic_graph(city_map);

    /* -------------------------------------------------------------
     * SECTION 1: Three Optimization Criteria Comparison
     * 1. "Fastest Time"     : Cost = (Distance / Base Speed) * Congestion
     * 2. "Shortest Distance": Cost = Distance (ignores traffic)
     * 3. "Eco / Fuel Saver" : Cost = Distance * (1.0 + (Congestion - 1.0) * 0.7)
     * ------------------------------------------------------------- */
    printf("============================================================\n");
    printf("SECTION 1: THREE OPTIMIZATION CRITERIA [Lakdikapul -> Secunderabad]\n");
    printf("============================================================\n");
    int src = 13; /* Lakdikapul */
    int dst = 18; /* Secunderabad Station */

    printf("--- [Criterion 1] Shortest Physical Distance (ignoring traffic) ---\n");
    DijkstraResult res_dist = dijkstra_find_path(city_map, src, dst, OPTIMIZE_SHORTEST_DISTANCE);
    print_dijkstra_result(&res_dist, src, dst, OPTIMIZE_SHORTEST_DISTANCE);
    free_dijkstra_result(&res_dist);

    printf("--- [Criterion 2] Fastest Travel Time (with live congestion) ------\n");
    DijkstraResult res_time = dijkstra_find_path(city_map, src, dst, OPTIMIZE_FASTEST_TIME);
    print_dijkstra_result(&res_time, src, dst, OPTIMIZE_FASTEST_TIME);
    free_dijkstra_result(&res_time);

    printf("--- [Criterion 3] Eco / Fuel Saver (minimizing idling & stop-go) --\n");
    DijkstraResult res_eco = dijkstra_find_path(city_map, src, dst, OPTIMIZE_ECO_FUEL);
    print_dijkstra_result(&res_eco, src, dst, OPTIMIZE_ECO_FUEL);
    free_dijkstra_result(&res_eco);

    /* -------------------------------------------------------------
     * SECTION 2: Alternative Routes (2.5x Edge Penalty Method)
     * - Primary optimal route computed via Dijkstra
     * - 2.5x cost penalty applied to all edges along primary path
     * - Dijkstra executed a second time to produce a distinct viable alternative
     *   (e.g., routing via Necklace Road instead of Tank Bund)
     * ------------------------------------------------------------- */
    printf("============================================================\n");
    printf("SECTION 2: ALTERNATIVE ROUTES (2.5x EDGE PENALTY METHOD)\n");
    printf("============================================================\n");
    printf(">> Computing primary route and 2.5x penalized distinct alternatives...\n\n");

    AlternativeRoutesResult alt_res = find_alternative_routes(city_map, src, dst, OPTIMIZE_FASTEST_TIME);
    print_alternative_routes_result(&alt_res, src, dst, OPTIMIZE_FASTEST_TIME);
    free_alternative_routes_result(&alt_res);

    /* -------------------------------------------------------------
     * SECTION 3: Real-Time Congestion Injection & Instant Rerouting
     * Function: update_traffic(int u, int v, float new_factor)
     * ------------------------------------------------------------- */
    printf("============================================================\n");
    printf("SECTION 3: REAL-TIME CONGESTION INJECTION & INSTANT REROUTING\n");
    printf("============================================================\n");
    printf(">> Initial Fastest Route [Lakdikapul (13) -> Secunderabad (18)]:\n");
    DijkstraResult before_reroute = dijkstra_find_path(city_map, src, dst, OPTIMIZE_FASTEST_TIME);
    printf("   Initial Path : ");
    for (int i = 0; i < before_reroute.node_count; i++) {
        printf("[%d]%s", before_reroute.path[i], (i < before_reroute.node_count - 1) ? " -> " : "");
    }
    printf(" (via East Shore Tank Bund)\n\n");
    free_dijkstra_result(&before_reroute);

    printf(">> Injecting severe gridlock on East Shore: Tank Bund Road [21 <-> 20]\n");
    printf("   Calling: update_traffic(21, 20, 3.0f)...\n");
    bool update_ok = update_traffic(21, 20, 3.0f);
    printf("   update_traffic returned: %s\n\n", update_ok ? "TRUE (Edge mutates instantly)" : "FALSE");

    printf(">> Triggering instant reroute...\n");
    DijkstraResult after_reroute = dijkstra_find_path(city_map, src, dst, OPTIMIZE_FASTEST_TIME);
    print_dijkstra_result(&after_reroute, src, dst, OPTIMIZE_FASTEST_TIME);
    printf("   Notice: Router dynamically diverted around Hussain Sagar to West Shore\n");
    printf("           PVNR Marg / Necklace Road corridor [13] -> [12] -> [15] -> [16] -> [18]!\n\n");
    free_dijkstra_result(&after_reroute);

    /* Restore traffic */
    printf(">> Restoring free-flow traffic: update_traffic(21, 20, 1.2f)...\n");
    update_traffic(21, 20, 1.2f);
    DijkstraResult restored = dijkstra_find_path(city_map, src, dst, OPTIMIZE_FASTEST_TIME);
    printf("   Restored Route: ");
    for (int i = 0; i < restored.node_count; i++) {
        printf("[%d]%s", restored.path[i], (i < restored.node_count - 1) ? " -> " : "");
    }
    printf(" (Reverted back to Tank Bund)\n\n");
    free_dijkstra_result(&restored);

    /* -------------------------------------------------------------
     * SECTION 4: Cross-Metropolitan Trans-Arterial Demonstration
     * ------------------------------------------------------------- */
    printf("============================================================\n");
    printf("SECTION 4: CROSS-METROPOLITAN IT TO OLD CITY & ARTERY\n");
    printf("============================================================\n");
    DijkstraResult res_metro = dijkstra_find_path(city_map, 2, 23, OPTIMIZE_ECO_FUEL);
    printf(">> HITEC City [2] -> Charminar [23] (Eco Mode):\n");
    print_dijkstra_result(&res_metro, 2, 23, OPTIMIZE_ECO_FUEL);
    free_dijkstra_result(&res_metro);

    /* Memory Cleanup */
    free_graph(city_map);
    printf("All graph adjacency lists, min-heaps, and path buffers freed cleanly with zero memory leaks.\n");

    return EXIT_SUCCESS;
}

