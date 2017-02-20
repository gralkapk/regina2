#include <vector>
#include <stdio.h>

#include "dr_api.h"

#include "drmgr.h"
#include "drreg.h"
#include "drutil.h"
#include "drsyms.h"

#include "log.h"
#include "per_thread_t.h"
#include "trace_ref_t.h"


// Forward declarations
static void event_exit(void);
static void event_thread_init(void *drcontext);
static void event_thread_exit(void *drcontext);
static dr_emit_flags_t event_app_instruction(void *drcontext, void *tag,
    instrlist_t *bb, instr_t *instr, bool for_trace, bool translating,
    void *user_data);
static dr_emit_flags_t event_bb_app2app(void *drcontext, void *tag, instrlist_t *bb,
    bool for_trace, bool translating);
static void cb_mem_ref();
//---------------------


// Global variables
static int tls_index;
static glb_trc_str trace_storage;
static int thread_idx;
static app_pc code_cache;
//-----------------


static void
code_cache_init(void) {
    void         *drcontext;
    instrlist_t  *ilist;
    instr_t      *where;
    byte         *end;

    drcontext = dr_get_current_drcontext();
    code_cache = static_cast<app_pc>(dr_nonheap_alloc(dr_page_size(),
        DR_MEMPROT_READ |
        DR_MEMPROT_WRITE |
        DR_MEMPROT_EXEC));
    ilist = instrlist_create(drcontext);
    /* The lean procecure simply performs a clean call, and then jump back */
    /* jump back to the DR's code cache */
    where = INSTR_CREATE_jmp_ind(drcontext, opnd_create_reg(DR_REG_XCX));
    instrlist_meta_append(ilist, where);
    /* clean call */
    dr_insert_clean_call(drcontext, ilist, where, (void *)cb_mem_ref, false, 0);
    /* Encodes the instructions into memory and then cleans up. */
    end = instrlist_encode(drcontext, ilist, code_cache, false);
    //DR_ASSERT((size_t)(end - code_cache) < page_size);
    instrlist_clear_and_destroy(drcontext, ilist);
    /* set the memory as just +rx now */
    dr_memory_protect(code_cache, dr_page_size(), DR_MEMPROT_READ | DR_MEMPROT_EXEC);
}


/*
 * dr_client_main
 */
DR_EXPORT void dr_client_main(client_id_t id, int argc, const char *argv[]) {
    dr_log(NULL, LOG_ALL, 1, "Client 'regina' initializing\n");
    //REGINA_LOG("Client 'regina' initializing\n");

    // Client setup
    dr_set_client_name("regina -- mem- and call-trace", "-");
    dr_set_client_version_string("0.1.0");

    drreg_options_t ops = {sizeof(ops), 3, false};

    /* Specify priority relative to other instrumentation operations: */
    drmgr_priority_t priority = {
        sizeof(priority), /* size of struct */
        "memtrace",       /* name of our operation */
        NULL,             /* optional name of operation we should precede */
        NULL,             /* optional name of operation we should follow */
        0};               /* numeric priority */

    // Init extension
    if (!drmgr_init()) {
        //REGINA_LOG_ERROR("Failed to initialize drmgr\n");
        return;
    }
    

    // Register events
    dr_register_exit_event(event_exit);
    if (!drmgr_register_thread_init_event(event_thread_init) ||
        !drmgr_register_thread_exit_event(event_thread_exit) ||
        !drmgr_register_bb_app2app_event(event_bb_app2app, &priority) ||
        !drmgr_register_bb_instrumentation_event(NULL, event_app_instruction, &priority) ||
        drreg_init(&ops) != DRREG_SUCCESS ||
        drsym_init(NULL) != DRSYM_SUCCESS) {
        //REGINA_LOG_ERROR("Unable to register drmgr events\n");
        DR_ASSERT(false);
        return;
    }

    tls_index = drmgr_register_tls_field();
    if (tls_index == -1) {
        //REGINA_LOG_ERROR("No TLS fields available\n");
        return;
    }

    thread_idx = 0;

    code_cache_init();

    // Notify dr log of this client
    dr_log(NULL, LOG_ALL, 1, "Client 'regina' is running\n");
    //REGINA_LOG("Client 'regina' is running\n");
}


/*
 * event_exit
 */
static void event_exit(void) {
    // Unregister events
    if (!drmgr_unregister_thread_init_event(event_thread_init) ||
        !drmgr_unregister_thread_exit_event(event_thread_exit) ||
        !drmgr_unregister_bb_insertion_event(event_app_instruction)) {
        //REGINA_LOG_ERROR("Unable to unregister drmgr events\n");
    }

    // Exit extensions
    drmgr_exit();
}


/*
 * event_thread_init
 */
static void event_thread_init(void *drcontext) {
    // Push a storage for this thread
    trace_storage.push_back(thr_trc_str());

    // Create thread local storage
    per_thread_t *data;

    data = static_cast<per_thread_t *>(dr_thread_alloc(drcontext, sizeof(per_thread_t)));
    drmgr_set_tls_field(drcontext, tls_index, data);

    data->thread_idx = thread_idx;
    data->buf = static_cast<trace_ref_t *>(dr_thread_alloc(drcontext, sizeof(trace_ref_t)));
    char filename[1024];
    sprintf(filename, "regina.%d.log", thread_idx);
    data->f = fopen(filename, "w");

    thread_idx++;
}


/*
 * event_thread_exit
 */
static void event_thread_exit(void *drcontext) {
    // Free thread local storage
    per_thread_t *data;

    data = static_cast<per_thread_t *>(drmgr_get_tls_field(drcontext, tls_index));
    fclose(data->f);

    dr_thread_free(drcontext, data->buf, sizeof(trace_ref_t));
    dr_thread_free(drcontext, data, sizeof(per_thread_t));
}


// global callbacks forward decl
//static void cb_mem_ref(void *drcontext, instr_t *where, int pos, bool is_write);
//------------------------------

#define MAX_SYM_RESULT 256
static void
print_address(FILE *f, app_pc addr, const char *prefix) {
    drsym_error_t symres;
    drsym_info_t sym;
    char name[MAX_SYM_RESULT];
    char file[MAXIMUM_PATH];
    module_data_t *data;
    data = dr_lookup_module(addr);
    if (data == NULL) {
        fprintf(f, "%s %p ? ??:0\n", prefix, addr);
        return;
    }
    sym.struct_size = sizeof(sym);
    sym.name = name;
    sym.name_size = MAX_SYM_RESULT;
    sym.file = file;
    sym.file_size = MAXIMUM_PATH;
    symres = drsym_lookup_address(data->full_path, addr - data->start, &sym,
        DRSYM_DEFAULT_FLAGS);
    if (symres == DRSYM_SUCCESS || symres == DRSYM_ERROR_LINE_NOT_AVAILABLE) {
        const char *modname = dr_module_preferred_name(data);
        if (modname == NULL)
            modname = "<noname>";
        fprintf(f, "%s %p %s!%s+%p", prefix, addr,
            modname, sym.name, addr - data->start - sym.start_offs);
        if (symres == DRSYM_ERROR_LINE_NOT_AVAILABLE) {
            fprintf(f, " ??:0\n");
        } else {
            fprintf(f, " %s:%d + %d\n",
                sym.file, sym.line, sym.line_offs);
        }
    } else
        dr_fprintf(f, "%s %p ? ??:0\n", prefix, addr);
    dr_free_module_data(data);
}


static dr_mcontext_t mc;
/*
* cb_mem_ref
*/
static void cb_mem_ref(/*instr_t *where, int pos, bool is_write*/) {
    void *drcontext = dr_get_current_drcontext();
    per_thread_t *data = static_cast<per_thread_t *>(drmgr_get_tls_field(drcontext, tls_index));

    int thread_idx = data->thread_idx;
    if (trace_storage[data->thread_idx].size() > 1000) {
        // print out
        for (int i = 0; i < trace_storage[data->thread_idx].size(); i++) {
            trace_ref_t &tmp = trace_storage[data->thread_idx][i];
            if (tmp.is_mem_ref) {
                //fprintf(data->f, "mem %p\n", tmp.instr_addr);
                print_address(data->f, tmp.instr_addr, "\t\t mem @ ");
            } else {
                if (tmp.is_call) {
                    //fprintf(data->f, "call %p to %p\n", tmp.instr_addr, tmp.target_addr);
                    print_address(data->f, tmp.instr_addr, "call @ ");
                    print_address(data->f, tmp.target_addr, "\t to ");
                } else {
                    //fprintf(data->f, "return %p to %p\n", tmp.instr_addr, tmp.target_addr);
                    print_address(data->f, tmp.instr_addr, "return @ ");
                    print_address(data->f, tmp.target_addr, "\t to ");
                }
            }
        }
        trace_storage[data->thread_idx].clear();
    }

    trace_ref_t trace;
    trace.instr_addr = data->buf->instr_addr;
    trace_storage[data->thread_idx].push_back(trace);
    memset(data->buf, 0, sizeof(trace_ref_t));
}


static void instrument_mem(void *drcontext, instrlist_t *ilist, instr_t *where, int pos, bool is_write) {
    drvector_t allowed;

    drreg_init_and_fill_vector(&allowed, false);
    drreg_set_vector_entry(&allowed, DR_REG_XCX, true);

    reg_id_t reg_ptr, reg_tmp;
    if (drreg_reserve_register(drcontext, ilist, where, &allowed, &reg_ptr) !=
        DRREG_SUCCESS ||
        drreg_reserve_register(drcontext, ilist, where, NULL, &reg_tmp) !=
        DRREG_SUCCESS) {
        DR_ASSERT(false); /* cannot recover */
        return;
    }
    drvector_delete(&allowed);

    instr_t *instr, *restore;
    opnd_t opnd1, opnd2;

    drmgr_insert_read_tls_field(drcontext, tls_index, ilist, where, reg_ptr);

    opnd1 = opnd_create_reg(reg_ptr);
    opnd2 = OPND_CREATE_MEMPTR(reg_ptr, offsetof(per_thread_t, buf));
    instr = INSTR_CREATE_mov_ld(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    // store pc
    opnd1 = OPND_CREATE_MEMPTR(reg_ptr, offsetof(trace_ref_t, instr_addr));
    instrlist_insert_mov_immed_ptrsz(drcontext, (ptr_int_t)instr_get_app_pc(where), opnd1, ilist, where, NULL, NULL);

    // store return address
    restore = INSTR_CREATE_label(drcontext);
    opnd1 = opnd_create_reg(reg_ptr);
    opnd2 = opnd_create_instr(restore);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    // jump to clean call
    opnd1 = opnd_create_pc(code_cache);
    instr = INSTR_CREATE_jmp(drcontext, opnd1);
    instrlist_meta_preinsert(ilist, where, instr);

    instrlist_meta_preinsert(ilist, where, restore);

    if (drreg_unreserve_register(drcontext, ilist, where, reg_ptr) != DRREG_SUCCESS ||
        drreg_unreserve_register(drcontext, ilist, where, reg_tmp) != DRREG_SUCCESS)
        DR_ASSERT(false);
}


static void at_call(app_pc instr_addr, app_pc target_addr) {
    void *drcontext = dr_get_current_drcontext();
    per_thread_t *data = static_cast<per_thread_t *>(drmgr_get_tls_field(drcontext, tls_index));

    trace_ref_t trace;
    trace.is_mem_ref = false;
    trace.is_call = true;
    trace.instr_addr = instr_addr;
    trace.target_addr = target_addr;

    trace_storage[data->thread_idx].push_back(trace);
}


static void at_call_ind(app_pc instr_addr, app_pc target_addr) {
    void *drcontext = dr_get_current_drcontext();
    per_thread_t *data = static_cast<per_thread_t *>(drmgr_get_tls_field(drcontext, tls_index));

    trace_ref_t trace;
    trace.is_mem_ref = false;
    trace.is_call = true;
    trace.instr_addr = instr_addr;
    trace.target_addr = target_addr;

    trace_storage[data->thread_idx].push_back(trace);
}


static void at_return(app_pc instr_addr, app_pc target_addr) {
    void *drcontext = dr_get_current_drcontext();
    per_thread_t *data = static_cast<per_thread_t *>(drmgr_get_tls_field(drcontext, tls_index));

    trace_ref_t trace;
    trace.is_mem_ref = false;
    trace.is_call = false;
    trace.instr_addr = instr_addr;
    trace.target_addr = target_addr;

    trace_storage[data->thread_idx].push_back(trace);
}


/*
 * event_app_instruction
 */
static dr_emit_flags_t event_app_instruction(void *drcontext, void *tag,
    instrlist_t *bb, instr_t *instr, bool for_trace, bool translating,
    void *user_data) {
    // instrument calls, returns, and MOVs
    if (instr_get_app_pc(instr) == NULL)
        return DR_EMIT_DEFAULT;

    if (instr_is_call_direct(instr)) {
        dr_insert_call_instrumentation(drcontext, bb, instr, (app_pc)at_call);
    } else if (instr_is_call_indirect(instr)) {
        dr_insert_mbr_instrumentation(drcontext, bb, instr, (app_pc)at_call_ind,
            SPILL_SLOT_1);
    } else if (instr_is_return(instr)) {
        dr_insert_mbr_instrumentation(drcontext, bb, instr, (app_pc)at_return,
            SPILL_SLOT_1);
    } else if (instr_reads_memory(instr)) {
        int opcode = instr_get_opcode(instr);
        if (std::strncmp(decode_opcode_name(opcode), "mov", 3ul) == 0) {
            for (int i = 0; i < instr_num_srcs(instr); i++) {
                if (opnd_is_memory_reference(instr_get_src(instr, i))) {
                    instrument_mem(drcontext, bb, instr, i, false);
                    //cb_mem_ref(drcontext, instr, i, false);
                    //dr_insert_clean_call(drcontext, bb, instr, cb_mem_ref, false, 2, OPND_CREATE_ABSMEM(instr, OPSZ_8), OPND_CREATE_INT32(i)/*, OPND_CREATE_INT8(false)*/);
                }
            }
        }
    } else if (instr_writes_memory(instr)) {
        int opcode = instr_get_opcode(instr);
        if (std::strncmp(decode_opcode_name(opcode), "mov", 3ul) == 0) {
            for (int i = 0; i < instr_num_dsts(instr); i++) {
                if (opnd_is_memory_reference(instr_get_dst(instr, i))) {
                    instrument_mem(drcontext, bb, instr, i, true);
                    //cb_mem_ref(drcontext, instr, i, true);
                    //dr_insert_clean_call(drcontext, bb, instr, cb_mem_ref, false, 2, OPND_CREATE_ABSMEM(instr, OPSZ_8), OPND_CREATE_INT32(i)/*, OPND_CREATE_INT8(true)*/);
                }
            }
        }
    }

    return DR_EMIT_DEFAULT;
}


static dr_emit_flags_t
event_bb_app2app(void *drcontext, void *tag, instrlist_t *bb,
    bool for_trace, bool translating) {
    if (!drutil_expand_rep_string(drcontext, bb)) {
        DR_ASSERT(false);
        /* in release build, carry on: we'll just miss per-iter refs */
    }
    return DR_EMIT_DEFAULT;
}