#ifndef MINHEAP_H
#define MINHEAP_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Represents an element in the Min-Heap priority queue.
 */
typedef struct HeapNode {
    int node_id;            /**< Intersection / Vertex ID */
    double current_cost;    /**< Current accumulated path cost (distance or travel time) */
} HeapNode;

/**
 * @brief Min-Heap Priority Queue optimized for Dijkstra's shortest path algorithm.
 * 
 * Features an internal position mapping table `pos[]` enabling O(log V) decrease_key
 * operations and O(1) membership checks.
 */
typedef struct MinHeap {
    int size;               /**< Number of active elements currently in the heap */
    int capacity;           /**< Maximum allocated capacity (typically num_vertices) */
    int* pos;               /**< pos[node_id] tracks index of node_id in array[] for O(1) lookup */
    HeapNode** array;       /**< Binary heap array of pointers to HeapNodes */
} MinHeap;

/**
 * @brief Allocates and initializes a Min-Heap with given capacity.
 * 
 * @param capacity Maximum number of vertices that can be tracked.
 * @return Pointer to the allocated MinHeap, or NULL on failure.
 */
MinHeap* create_min_heap(int capacity);

/**
 * @brief Inserts or initializes a node into the Min-Heap.
 * 
 * @param min_heap Pointer to the MinHeap.
 * @param node_id Intersection vertex ID.
 * @param current_cost Initial accumulated cost.
 * @return true on success, false if full or invalid.
 */
bool insert_min_heap(MinHeap* min_heap, int node_id, double current_cost);

/**
 * @brief Extracts the node with the minimum current_cost from the heap.
 * 
 * @param min_heap Pointer to the MinHeap.
 * @return HeapNode with minimum cost (caller receives a copy; internal node is freed).
 */
HeapNode extract_min(MinHeap* min_heap);

/**
 * @brief Updates the cost of an existing node in the heap to a smaller value.
 * 
 * Runs in O(log V) time using the internal pos[] tracking array.
 * 
 * @param min_heap Pointer to the MinHeap.
 * @param node_id Intersection vertex ID.
 * @param new_cost New lower cost.
 */
void decrease_key(MinHeap* min_heap, int node_id, double new_cost);

/**
 * @brief Checks if a specific node is currently present in the Min-Heap.
 * 
 * @param min_heap Pointer to the MinHeap.
 * @param node_id Intersection vertex ID.
 * @return true if present, false otherwise.
 */
bool is_in_min_heap(const MinHeap* min_heap, int node_id);

/**
 * @brief Checks if the Min-Heap is empty.
 * 
 * @param min_heap Pointer to the MinHeap.
 * @return true if empty, false otherwise.
 */
bool is_empty_min_heap(const MinHeap* min_heap);

/**
 * @brief Frees all dynamically allocated memory of the Min-Heap.
 * 
 * @param min_heap Pointer to the MinHeap.
 */
void free_min_heap(MinHeap* min_heap);

#endif /* MINHEAP_H */
