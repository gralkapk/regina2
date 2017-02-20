#ifndef REGINA_TRACE_REF_T_H_INCLUDED
#define REGINA_TRACE_REF_T_H_INCLUDED

#include <vector>

#include "dr_api.h"

typedef struct _trace_ref_t {
    bool is_mem_ref;
    bool write;
    void *data_addr;
    size_t size;
    app_pc instr_addr;
    app_pc target_addr;
} trace_ref_t;

typedef std::vector<trace_ref_t> thr_trc_str;

typedef std::vector<thr_trc_str> glb_trc_str;

#endif