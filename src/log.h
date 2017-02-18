#ifndef REGINA_LOG_H_INCLUDED
#define REGINA_LOG_H_INCLUDED

#include <cstdio>


#define REGINA_LOG_ERROR(...) std::fprintf(stderr, __VA_ARGS__)

#define REGINA_LOG(...) std::fprintf(stdout, __VA_ARGS__)

#endif