#ifndef REGINA_TRACE_REF_T_H_INCLUDED
#define REGINA_TRACE_REF_T_H_INCLUDED

#include "dr_api.h"

typedef struct _trace_ref_t {
    bool is_mem_ref;
    bool write;
    void *data_addr;
    size_t size;
    app_pc instr_addr;
    app_pc target_addr;
} trace_ref_t;

#endif