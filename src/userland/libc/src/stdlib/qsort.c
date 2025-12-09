#include <stdlib.h>
#include <stddef.h>

static void swap(void *a, void *b, size_t size) {
    char *p = a;
    char *q = b;
    char tmp;
    for (size_t i = 0; i < size; i++) {
        tmp = p[i];
        p[i] = q[i];
        q[i] = tmp;
    }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    if (nmemb < 2) return;

    // Standard partition
    char *arr = (char *)base;
    char *pivot = arr + (nmemb / 2) * size;
    
    // We can't easily swap pivot to end without allocating memory for swap or assuming we can modify array freely (we can).
    // Let's implement a simple partition.
    
    // Actually, let's use a simpler recursive approach that doesn't require extra buffer but might not be optimal.
    // Hoare partition scheme or similar.
    
    // Since pivot is in the array, we need to be careful not to lose it if we swap.
    // But usually people just pick the value. But we don't know the type.
    // So we pick an index.
    
    size_t i = 0;
    size_t j = nmemb - 1;
    
    // For pivot, we need a value. We can't just keep 'pivot' pointer valid if we swap elements.
    // So we should swap pivot to last element first.
    
    // Let's swap middle element to end
    swap(arr + (nmemb / 2) * size, arr + (nmemb - 1) * size, size);
    
    // Pivot is now at arr[nmemb-1]
    void *pivot_val = arr + (nmemb - 1) * size;
    
    size_t cur = 0;
    for (size_t k = 0; k < nmemb - 1; k++) {
        if (compar(arr + k * size, pivot_val) < 0) {
            swap(arr + k * size, arr + cur * size, size);
            cur++;
        }
    }
    
    // Place pivot
    swap(arr + cur * size, arr + (nmemb - 1) * size, size);
    
    // Recurse
    qsort(arr, cur, size, compar);
    qsort(arr + (cur + 1) * size, nmemb - cur - 1, size, compar);
}
