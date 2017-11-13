#include <algorithm>
#include <cstdio>
#include <iostream>
#include <malloc.h>
#include <random>
#include <stdint.h>

const size_t N = 128 * 128;

typedef int32_t memory_T;
typedef int64_t compute_T;

memory_T* memA = nullptr;

void initMem(memory_T* stuff) {
    for (size_t i = 0; i < N; i++) {
        stuff[i] = static_cast<memory_T>(rand() % 10);
    }
}

int comp(const void* a, const void* b) {
    return (*(memory_T*)a) - (*(memory_T*)b);
}

void QuickSort(memory_T* ptr) {
    std::qsort(ptr, N, sizeof(memory_T), &comp);
}

// from https://github.com/hugopeixoto/mergesort/blob/master/c/mergesort.c
#pragma region hugopeixoto / mergesort

void Merge(int* lst, int a, int b, int s) {
    int tmp[N], ti = a, ai = a, bi = b;
    while (ai < b || bi < s) {
        if (bi == s)
            tmp[ti++] = lst[ai++];
        else if (ai == b)
            tmp[ti++] = lst[bi++];
        else if (lst[ai] < lst[bi])
            tmp[ti++] = lst[ai++];
        else
            tmp[ti++] = lst[bi++];
    }

    for (ti = a; ti < s; ti++)
        lst[ti] = tmp[ti];
}

void MergeSort(int* lst, int a, int b) {
    if (b - a < 2)
        return;

    MergeSort(lst, a, a + (b - a) / 2);
    MergeSort(lst, a + (b - a) / 2, b);
    Merge(lst, a, a + (b - a) / 2, b);
}

#pragma endregion hugopeixoto / mergesort

int main() {
    srand(42);

    memA = static_cast<memory_T*>(malloc(sizeof(memory_T) * N));

    initMem(memA);
    QuickSort(memA);

    initMem(memA);
    MergeSort(memA, 0, N);

    return 0;
}
