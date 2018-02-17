#ifndef _DYN_ARRAY_H_
#define _DYN_ARRAY_H_

#include <stdlib.h>
#include <string.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

struct dyn_array {
    char *data;
    int slot_size;
    int len;
    int capacity;
};

static void dyn_array_init(struct dyn_array *out, int slot_size) {
    out -> data = NULL;
    out -> slot_size = slot_size;
    out -> len = 0;
    out -> capacity = 0;
}

static void dyn_array_destroy(struct dyn_array *arr) {
    if(arr -> data) free(arr -> data);
    arr -> data = NULL;
    arr -> len = 0;
    arr -> capacity = 0;
}

static void dyn_array_push(struct dyn_array *arr, char *data) {
    if(arr -> len + arr -> slot_size > arr -> capacity) {
        // arr -> len can never be greater than arr -> capacity
        arr -> capacity += MAX(MIN(1024, arr -> capacity / 2), arr -> slot_size);
        arr -> data = (char *) realloc(arr -> data, arr -> capacity);
    }
    memcpy(&arr -> data[arr -> len], data, arr -> slot_size);
    arr -> len += arr -> slot_size;
}

static void dyn_array_pop(struct dyn_array *arr, char *out) {
    if(arr -> len - arr -> slot_size < 0) {
        abort();
    }
    arr -> len -= arr -> slot_size;
    memcpy(out, &arr -> data[arr -> len], arr -> slot_size);
}

static void dyn_array_index(struct dyn_array *arr, char **ref_out, int index) {
    int real_index;
    
    real_index = index * arr -> slot_size;
    if(real_index >= arr -> len || real_index < 0) {
        abort();
    }
    *ref_out = &arr -> data[real_index];
}

static int dyn_array_n_elems(struct dyn_array *arr) {
    return arr -> len / arr -> slot_size;
}

#endif
