#include "../include/minheap.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Helper to create an individual HeapNode.
 */
static HeapNode* create_heap_node(int node_id, double current_cost) {
    HeapNode* node = (HeapNode*)malloc(sizeof(HeapNode));
    if (!node) {
        fprintf(stderr, "Error: Memory allocation failed for HeapNode.\n");
        return NULL;
    }
    node->node_id = node_id;
    node->current_cost = current_cost;
    return node;
}

/**
 * @brief Swaps two nodes in the min-heap array and updates their indices in pos[].
 */
static void swap_heap_nodes(MinHeap* min_heap, int i, int j) {
    HeapNode* temp = min_heap->array[i];
    min_heap->array[i] = min_heap->array[j];
    min_heap->array[j] = temp;

    /* Synchronize the position map */
    min_heap->pos[min_heap->array[i]->node_id] = i;
    min_heap->pos[min_heap->array[j]->node_id] = j;
}

/**
 * @brief Restores min-heap property downwards from index idx.
 */
static void min_heapify(MinHeap* min_heap, int idx) {
    int smallest = idx;
    int left = 2 * idx + 1;
    int right = 2 * idx + 2;

    if (left < min_heap->size && 
        min_heap->array[left]->current_cost < min_heap->array[smallest]->current_cost) {
        smallest = left;
    }

    if (right < min_heap->size && 
        min_heap->array[right]->current_cost < min_heap->array[smallest]->current_cost) {
        smallest = right;
    }

    if (smallest != idx) {
        swap_heap_nodes(min_heap, idx, smallest);
        min_heapify(min_heap, smallest);
    }
}

MinHeap* create_min_heap(int capacity) {
    if (capacity <= 0) return NULL;

    MinHeap* min_heap = (MinHeap*)malloc(sizeof(MinHeap));
    if (!min_heap) {
        fprintf(stderr, "Error: Memory allocation failed for MinHeap.\n");
        return NULL;
    }

    min_heap->pos = (int*)malloc((size_t)capacity * sizeof(int));
    min_heap->array = (HeapNode**)malloc((size_t)capacity * sizeof(HeapNode*));

    if (!min_heap->pos || !min_heap->array) {
        fprintf(stderr, "Error: Memory allocation failed for MinHeap internal buffers.\n");
        if (min_heap->pos) free(min_heap->pos);
        if (min_heap->array) free(min_heap->array);
        free(min_heap);
        return NULL;
    }

    min_heap->size = 0;
    min_heap->capacity = capacity;

    /* Initialize position map with -1 (not present in heap) */
    for (int i = 0; i < capacity; i++) {
        min_heap->pos[i] = -1;
    }

    return min_heap;
}

bool insert_min_heap(MinHeap* min_heap, int node_id, double current_cost) {
    if (!min_heap || node_id < 0 || node_id >= min_heap->capacity) {
        return false;
    }
    if (min_heap->size >= min_heap->capacity) {
        return false; /* Heap full */
    }

    HeapNode* new_node = create_heap_node(node_id, current_cost);
    if (!new_node) return false;

    /* Place at the end of heap */
    int i = min_heap->size;
    min_heap->array[i] = new_node;
    min_heap->pos[node_id] = i;
    min_heap->size++;

    /* Bubble up to maintain min-heap property */
    while (i > 0 && min_heap->array[i]->current_cost < min_heap->array[(i - 1) / 2]->current_cost) {
        swap_heap_nodes(min_heap, i, (i - 1) / 2);
        i = (i - 1) / 2;
    }

    return true;
}

HeapNode extract_min(MinHeap* min_heap) {
    HeapNode empty_node = {-1, -1.0};
    if (!min_heap || min_heap->size <= 0) {
        return empty_node;
    }

    HeapNode* root = min_heap->array[0];
    HeapNode result = *root;

    /* Mark extracted node as no longer in heap */
    min_heap->pos[root->node_id] = -1;

    /* Replace root with the last node in heap */
    HeapNode* last_node = min_heap->array[min_heap->size - 1];
    min_heap->array[0] = last_node;
    min_heap->pos[last_node->node_id] = 0;

    min_heap->size--;
    free(root);

    /* Heapify down from root */
    if (min_heap->size > 0) {
        min_heapify(min_heap, 0);
    }

    return result;
}

void decrease_key(MinHeap* min_heap, int node_id, double new_cost) {
    if (!min_heap || node_id < 0 || node_id >= min_heap->capacity) {
        return;
    }

    int i = min_heap->pos[node_id];
    if (i < 0 || i >= min_heap->size) {
        return; /* Node is not currently in heap */
    }

    min_heap->array[i]->current_cost = new_cost;

    /* Sift up */
    while (i > 0 && min_heap->array[i]->current_cost < min_heap->array[(i - 1) / 2]->current_cost) {
        swap_heap_nodes(min_heap, i, (i - 1) / 2);
        i = (i - 1) / 2;
    }
}

bool is_in_min_heap(const MinHeap* min_heap, int node_id) {
    if (!min_heap || node_id < 0 || node_id >= min_heap->capacity) {
        return false;
    }
    return min_heap->pos[node_id] != -1 && min_heap->pos[node_id] < min_heap->size;
}

bool is_empty_min_heap(const MinHeap* min_heap) {
    return (!min_heap || min_heap->size == 0);
}

void free_min_heap(MinHeap* min_heap) {
    if (!min_heap) return;

    if (min_heap->array) {
        for (int i = 0; i < min_heap->size; i++) {
            if (min_heap->array[i]) {
                free(min_heap->array[i]);
            }
        }
        free(min_heap->array);
    }

    if (min_heap->pos) {
        free(min_heap->pos);
    }

    free(min_heap);
}
