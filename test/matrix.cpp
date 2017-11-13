#include <algorithm>
#include <cstdio>
#include <iostream>
#include <malloc.h>
#include <random>

const size_t N = 64;

typedef float memory_T;
typedef double compute_T;

const size_t cacheline_size = 64;
const size_t B = cacheline_size / sizeof(memory_T);

memory_T *memA = nullptr, *memB = nullptr, *memC = nullptr;

void initMem(memory_T* stuff) {
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            stuff[i * N + j] = static_cast<memory_T>(rand() % 10);
        }
    }
}

// from http://www.cc.gatech.edu/~bader/COURSES/UNM/ece637-Fall2003/papers/KW03.pdf
#pragma region kowarschik_weiss

compute_T loop_interchange_bad() {
    compute_T sum = static_cast<compute_T>(0);

    for (size_t j = 0; j < N; j++) {
        for (size_t i = 0; i < N; i++) {
            sum += memA[i * N + j];
        }
    }
    return sum;
}

compute_T loop_interchange_good() {
    compute_T sum = static_cast<compute_T>(0);

    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            sum += memA[i * N + j];
        }
    }
    return sum;
}

void loop_fusion_off() {
    for (size_t i = 0; i < N * N; i++) {
        memB[i] = static_cast<memory_T>(memA[i] + static_cast<compute_T>(1));
    }
    for (size_t i = 0; i < N * N; i++) {
        memC[i] = static_cast<memory_T>(memB[i] * static_cast<compute_T>(4));
    }
}

void loop_fusion_on() {
    for (size_t i = 0; i < N * N; i++) {
        memB[i] = static_cast<memory_T>(memA[i] + static_cast<compute_T>(1));
        memC[i] = static_cast<memory_T>(memB[i] * static_cast<compute_T>(4));
    }
}

void loop_blocking_off() {
    for (size_t i = 0; i < N; i++) {
        for (size_t j = 0; j < N; j++) {
            memB[j * N + i] = memA[i * N + j];
        }
    }
}

void loop_blocking_on() {
    for (size_t ii = 0; ii < N; ii += B) {
        for (size_t jj = 0; jj < N; jj += B) {
            for (size_t i = ii; i < std::min(ii + B - 1, N); i++) {
                for (size_t j = jj; j < std::min(jj + B - 1, N); j++) {
                    memB[j * N + i] = memA[i * N + j];
                }
            }
        }
    }
}

#pragma endregion kowarschik_weiss

int main() {

    srand(42);

    memA = static_cast<memory_T*>(malloc(sizeof(memory_T) * N * N));
    memB = static_cast<memory_T*>(malloc(sizeof(memory_T) * N * N));
    memC = static_cast<memory_T*>(malloc(sizeof(memory_T) * N * N));
    initMem(memA);
    initMem(memB);
    initMem(memC);

    compute_T result = static_cast<compute_T>(0);

    result = loop_interchange_bad();
    std::cout << "loop_interchange_bad: " << result << std::endl;

    result = loop_interchange_good();
    std::cout << "loop_interchange_good: " << result << std::endl;

    loop_fusion_off();
    std::cout << "loop_fusion_off" << std::endl;

    loop_fusion_on();
    std::cout << "loop_fusion_on" << std::endl;

    loop_blocking_off();
    std::cout << "loop_blocking_off" << std::endl;

    loop_blocking_on();
    std::cout << "loop_blocking_on" << std::endl;

    return 0;
}
