/* ******************************************************************************
 * Copyright (c) 2011-2021 Google, Inc.  All rights reserved.
 * Copyright (c) 2010 Massachusetts Institute of Technology  All rights reserved.
 * ******************************************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of VMware, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* Code Manipulation API Sample:
 * memtrace_x86.c
 *
 * Collects the instruction address, data address, and size of every
 * memory reference and dumps the results to a file.
 * This is an x86-specific implementation of a memory tracing client.
 * For a simpler (and slower) arch-independent version, please see memtrace_simple.c.
 *
 * Illustrates how to create generated code in a local code cache and
 * perform a lean procedure call to that generated code.
 *
 * (1) Fills a buffer and dumps the buffer when it is full.
 * (2) Inlines the buffer filling code to avoid a full context switch.
 * (3) Uses a lean procedure call for clean calls to reduce code cache size.
 *
 * This sample illustrates
 * - the use of drutil_expand_rep_string() to expand string loops to obtain
 *   every memory reference;
 * - the use of drx_expand_scatter_gather() to expand scatter/gather instrs
 *   into a set of functionally equivalent stores/loads;
 * - the use of drutil_opnd_mem_size_in_bytes() to obtain the size of OP_enter
 *   memory references.
 *
 * The OUTPUT_TEXT define controls the format of the trace: text or binary.
 * Creating a text trace file makes the tool an order of magnitude (!) slower
 * than creating a binary file; thus, the default is binary.
 */

#include "dr_api.h"
#include "drmgr.h"
#include "drreg.h"
#include "drutil.h"
#include "drsyms.h"
#include "drx.h"
#include "utils.h"
#include <stddef.h> /* for offsetof */
#include <stdio.h>
#include <string.h> /* for memset */

#include <vector>
#include <fstream>
#include <string>
#include <unordered_map>
#include <sstream>
#include <corecrt_io.h>

#define MAX_SYM_RESULT 256

/* Each mem_ref_t includes the type of reference (read or write),
 * the address referenced, and the size of the reference.
 */
typedef struct _mem_ref_t {
    bool memRef;
    bool write;
    bool call;
    bool ind;
    uint32_t pad;
    void* addr;
    size_t size;
    app_pc pc;
    app_pc target;
} mem_ref_t;

/* Max number of mem_ref a buffer can have */
#define MAX_NUM_MEM_REFS 8192
/* The size of memory buffer for holding mem_refs. When it fills up,
 * we dump data from the buffer to the file.
 */
#define MEM_BUF_SIZE (sizeof(mem_ref_t) * MAX_NUM_MEM_REFS)

//#define OUTPUT_TEXT 1

/* thread private log file and counter */
typedef struct {
    char* buf_ptr;
    char* buf_base;
    /* buf_end holds the negative value of real address of buffer end. */
    ptr_int_t buf_end;
    void* cache;
    FILE* logf;
    uint64 threadID;
    uint64 num_refs;
} per_thread_t;

/* Cross-instrumentation-phase data. */
typedef struct {
    app_pc last_pc;
} instru_data_t;

static size_t page_size;
static client_id_t client_id;
static app_pc code_cache;
static void* mutex; /* for multithread support */
static uint64 global_num_refs; /* keep a global memory reference count */
static int tls_index;

//static std::vector<file_t> delayed_files;
static std::unordered_map<std::string, size_t> symbol_lookup;
static size_t symbol_idx = 0;
static int file_idx = 0;
static char symName[MAX_SYM_RESULT];
static char modName[MAX_SYM_RESULT];
static uint64 thread_idx = 0;

static void
event_exit(void);
static void
event_thread_init(void* drcontext);
static void
event_thread_exit(void* drcontext);
static dr_emit_flags_t
event_bb_app2app(void* drcontext, void* tag, instrlist_t* bb, bool for_trace,
    bool translating);
static dr_emit_flags_t
event_bb_analysis(void* drcontext, void* tag, instrlist_t* bb, bool for_trace,
    bool translating, void** user_data);
static dr_emit_flags_t
event_bb_insert(void* drcontext, void* tag, instrlist_t* bb, instr_t* instr,
    bool for_trace, bool translating, void* user_data);

static void
clean_call(void);
static void
memtrace(void* drcontext);
static void
code_cache_init(void);
static void
code_cache_exit(void);
static void
instrument_mem(void* drcontext, instrlist_t* ilist, instr_t* where, app_pc pc,
    instr_t* memref_instr, int pos, bool write);

DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char* argv[]) {
    /* We need 2 reg slots beyond drreg's eflags slots => 3 slots */
    drreg_options_t ops = { sizeof(ops), 3, false };
    /* Specify priority relative to other instrumentation operations: */
    drmgr_priority_t priority = { sizeof(priority), /* size of struct */
        "memtrace", /* name of our operation */
        NULL, /* optional name of operation we should precede */
        NULL, /* optional name of operation we should follow */
        0 }; /* numeric priority */
    dr_set_client_name("DynamoRIO Sample Client 'memtrace'",
        "http://dynamorio.org/issues");
    page_size = dr_page_size();
    drmgr_init();
    drutil_init();
    client_id = id;
    mutex = dr_mutex_create();
    dr_register_exit_event(event_exit);
    if (!drmgr_register_thread_init_event(event_thread_init) || !drmgr_register_thread_exit_event(event_thread_exit) || !drmgr_register_bb_app2app_event(event_bb_app2app, &priority) || !drmgr_register_bb_instrumentation_event(event_bb_analysis, event_bb_insert, &priority) || drreg_init(&ops) != DRREG_SUCCESS || !drx_init()) {
        /* something is wrong: can't continue */
        DR_ASSERT(false);
        return;
    }
    if (drsym_init(0) != DRSYM_SUCCESS) {
        dr_log(NULL, DR_LOG_ALL, 1, "WARNING: unable to initialize symbol translation\n");
        dr_printf("Failed to init DR Sym\n");
    }
    tls_index = drmgr_register_tls_field();
    DR_ASSERT(tls_index != -1);

    code_cache_init();
    /* make it easy to tell, by looking at log file, which client executed */
    dr_log(NULL, DR_LOG_ALL, 1, "Client 'memtrace' initializing\n");
#ifdef SHOW_RESULTS
    if (dr_is_notify_on()) {
#ifdef WINDOWS
        /* ask for best-effort printing to cmd window.  must be called at init. */
        dr_enable_console_printing();
#endif
        dr_fprintf(STDERR, "Client memtrace is running\n");
    }
#endif
}

static void
print_address(file_t f, app_pc addr, const char* prefix) {
    drsym_error_t symres;
    drsym_info_t sym;
    char name[MAX_SYM_RESULT];
    char file[MAXIMUM_PATH];
    module_data_t* data;
    data = dr_lookup_module(addr);
    if (data == NULL) {
        dr_fprintf(f, "%s " PFX " ? ??:0\n", prefix, addr);
        return;
    }
    sym.struct_size = sizeof(sym);
    sym.name = name;
    sym.name_size = MAX_SYM_RESULT;
    sym.file = file;
    sym.file_size = MAXIMUM_PATH;
    symres = drsym_lookup_address(data->full_path, addr - data->start, &sym,
        DRSYM_DEMANGLE_PDB_TEMPLATES);
    if (symres == DRSYM_SUCCESS || symres == DRSYM_ERROR_LINE_NOT_AVAILABLE) {
        const char* modname = dr_module_preferred_name(data);
        if (modname == NULL)
            modname = "<noname>";
        dr_fprintf(f, "%s " PFX " %s!%s+" PIFX, prefix, addr, modname, sym.name,
            addr - data->start - sym.start_offs);
        if (symres == DRSYM_ERROR_LINE_NOT_AVAILABLE) {
            dr_fprintf(f, " ??:0\n");
        } else {
            dr_fprintf(f, " %s:%" UINT64_FORMAT_CODE "+" PIFX "\n", sym.file, sym.line,
                sym.line_offs);
        }
    } else
        dr_fprintf(f, "%s " PFX " ? ??:0\n", prefix, addr);
    dr_free_module_data(data);
}

static void
simple_address(app_pc addr, char* modname, char* symname) {
    dr_fprintf(STDOUT, "fetch sym\n");
    drsym_error_t symres;
    drsym_info_t sym;
    char name[MAX_SYM_RESULT];
    char file[MAXIMUM_PATH];
    module_data_t* data;
    data = dr_lookup_module(addr);
    if (data == NULL) {
        dr_snprintf(modname, MAX_SYM_RESULT, "###");
        dr_snprintf(symname, MAX_SYM_RESULT, "###");
        return;
    }
    sym.struct_size = sizeof(sym);
    sym.name = name;
    sym.name_size = MAX_SYM_RESULT;
    sym.file = file;
    sym.file_size = MAXIMUM_PATH;
    symres = drsym_lookup_address(data->full_path, addr - data->start, &sym,
        DRSYM_DEMANGLE_PDB_TEMPLATES);
    if (symres == DRSYM_SUCCESS || symres == DRSYM_ERROR_LINE_NOT_AVAILABLE) {
        const char* mod = dr_module_preferred_name(data);
        if (mod == NULL) {
            dr_snprintf(modname, MAX_SYM_RESULT, "noname");
        } else {
            dr_snprintf(modname, MAX_SYM_RESULT, "%s", mod);
        }
        dr_snprintf(sym.name, MAX_SYM_RESULT, "%s", sym.name);
    } else {
        dr_snprintf(modname, MAX_SYM_RESULT, "###");
        dr_snprintf(symname, MAX_SYM_RESULT, "###");
    }
    dr_free_module_data(data);
}

static void translate_addr(app_pc addr, std::string& sym_string) {
    //dr_printf("test\n");
    std::ostringstream stringStream;
    stringStream << std::hex;
    drsym_error_t symres;
    drsym_info_t sym;
    char name[MAX_SYM_RESULT];
    char file[MAXIMUM_PATH];
    module_data_t* data;
    data = dr_lookup_module(addr);
    if (data == NULL) {
        stringStream << "###";
        sym_string = stringStream.str();
        dr_printf("failed to lookup module\n");
        return;
    }
    //dr_printf("test2\n");
    sym.struct_size = sizeof(sym);
    sym.name = name;
    sym.name_size = MAX_SYM_RESULT;
    sym.file = file;
    sym.file_size = MAXIMUM_PATH;
    //dr_printf("test2 %s\n", data->full_path);
    symres = drsym_lookup_address(data->full_path, addr - data->start, &sym,
        DRSYM_DEMANGLE_PDB_TEMPLATES);
    //dr_printf("test3\n");
    if (symres == DRSYM_SUCCESS || symres == DRSYM_ERROR_LINE_NOT_AVAILABLE) {
        const char* modname = dr_module_preferred_name(data);
        if (modname == NULL)
            modname = "<noname>";
        stringStream << modname << "#" << sym.name; // << "+" << addr - data->start - sym.start_offs;
        /*if (symres == DRSYM_ERROR_LINE_NOT_AVAILABLE) {
            stringStream << "##";
        } else {
            stringStream << "#" << sym.file << "#" << std::dec << sym.line << "+" << sym.line_offs;
        }*/
    } else
        stringStream << "###";
    sym_string = stringStream.str();
    dr_free_module_data(data);
}

struct mem_dump {
    unsigned char write;
    uint64 data;
    unsigned char size;
    uint64 symIdx;
};

struct call_dump {
    unsigned char subType;
    uint64 instr;
    uint64 target;
    uint64 instrSymIdx;
    uint64 targetSymIdx;
};

static void process_file(FILE* f, int file_idx) {
    fseek(f, 0, SEEK_END);
    auto const fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    auto const num_refs = fsz / sizeof(mem_ref_t);
    dr_printf("Num Refs %d by %d size and %d type\n", num_refs, fsz, sizeof(mem_ref_t));
    std::vector<mem_ref_t> ref_buffer(num_refs);
    //dr_read_file(f, ref_buffer.data(), num_refs * sizeof(mem_ref_t));
    fread(ref_buffer.data(), sizeof(mem_ref_t), num_refs, f);

    auto ofile = std::ofstream(std ::string("regina.") + std::to_string(file_idx) + std::string(".mmtrd"), std::ios::binary);
    for (auto const& el : ref_buffer) {
        //dr_printf("Addr: %p\n", el.pc);
        //print_address(STDOUT, el.pc, "MEM @\t");
        if (el.memRef) {
            mem_dump md = {};
            unsigned char type = 0;
            ofile.write(reinterpret_cast<const char*>(&type), sizeof(type));
            md.write = el.write ? 1 : 2;
            md.data = (size_t)el.addr;
            md.size = el.size;
            std::string str;
            translate_addr(el.pc, str);
            auto it = symbol_lookup.find(str);
            if (it != symbol_lookup.end()) {
                md.symIdx = it->second;
            } else {
                md.symIdx = symbol_idx;
                symbol_lookup.insert(std::make_pair(str, symbol_idx++));
            }
            //md.symIdx = 0;
            //ofile.write(reinterpret_cast<const char*>(&md), sizeof(md));
            ofile.write(reinterpret_cast<const char*>(&md.write), sizeof(md.write));
            ofile.write(reinterpret_cast<const char*>(&md.data), sizeof(md.data));
            ofile.write(reinterpret_cast<const char*>(&md.size), sizeof(md.size));
            ofile.write(reinterpret_cast<const char*>(&md.symIdx), sizeof(md.symIdx));
        } else {
            call_dump cd = {};
            unsigned char type = 1;
            ofile.write(reinterpret_cast<const char*>(&type), sizeof(type));
            if (el.call && el.ind) {
                cd.subType = 1;
            } else if (el.call) {
                cd.subType = 0;
            } else {
                cd.subType = 2;
            }
            cd.instr = (size_t)el.pc;
            std::string str;
            translate_addr(el.pc, str);
            auto it = symbol_lookup.find(str);
            if (it != symbol_lookup.end()) {
                cd.instrSymIdx = it->second;
            } else {
                cd.instrSymIdx = symbol_idx;
                symbol_lookup.insert(std::make_pair(str, symbol_idx++));
            }
            cd.target = (size_t)el.target;
            translate_addr(el.target, str);
            it = symbol_lookup.find(str);
            if (it != symbol_lookup.end()) {
                cd.targetSymIdx = it->second;
            } else {
                cd.targetSymIdx = symbol_idx;
                symbol_lookup.insert(std::make_pair(str, symbol_idx++));
            }
            /*cd.instrSymIdx = 0;
            cd.targetSymIdx = 0;*/
            //ofile.write(reinterpret_cast<const char*>(&cd), sizeof(cd));
            ofile.write(reinterpret_cast<const char*>(&cd.subType), sizeof(cd.subType));
            ofile.write(reinterpret_cast<const char*>(&cd.instr), sizeof(cd.instr));
            ofile.write(reinterpret_cast<const char*>(&cd.target), sizeof(cd.target));
            ofile.write(reinterpret_cast<const char*>(&cd.instrSymIdx), sizeof(cd.instrSymIdx));
            ofile.write(reinterpret_cast<const char*>(&cd.targetSymIdx), sizeof(cd.targetSymIdx));
        }
    }
    ofile.close();
}

static void
event_exit() {
#ifdef SHOW_RESULTS
    char msg[512];
    int len;
    len = dr_snprintf(msg, sizeof(msg) / sizeof(msg[0]),
        "Instrumentation results:\n"
        "  saw %llu memory references\n",
        global_num_refs);
    DR_ASSERT(len > 0);
    NULL_TERMINATE_BUFFER(msg);
    DISPLAY_STRING(msg);
#endif /* SHOW_RESULTS */
    /*int file_idx = 0;
    for (auto& f : delayed_files) {
        process_file(f, file_idx);
        log_file_close(f);
        ++file_idx;
    }*/

    FILE* lookupIO = std::fopen("regina.0.mmtrd.txt", "w");
    for (auto& e : symbol_lookup) {
        std::string tmp = std::to_string(e.second) + "|" + e.first + "\n";
        std::fwrite(tmp.c_str(), strlen(tmp.c_str()), 1, lookupIO);
    }
    std::fclose(lookupIO);

    code_cache_exit();

    if (!drmgr_unregister_tls_field(tls_index) || !drmgr_unregister_thread_init_event(event_thread_init) || !drmgr_unregister_thread_exit_event(event_thread_exit) || !drmgr_unregister_bb_insertion_event(event_bb_insert) || drreg_exit() != DRREG_SUCCESS)
        DR_ASSERT(false);

    if (drsym_exit() != DRSYM_SUCCESS) {
        dr_log(NULL, DR_LOG_ALL, 1, "WARNING: error cleaning up symbol library\n");
        dr_printf("Failed to cleanup symbol library\n");
    }

    dr_mutex_destroy(mutex);
    drutil_exit();
    drmgr_exit();
    drx_exit();
}

#ifdef WINDOWS
#define IF_WINDOWS(x) x
#else
#define IF_WINDOWS(x) /* nothing */
#endif

static void
event_thread_init(void* drcontext) {
    per_thread_t* data;

    /* allocate thread private data */
    data = (per_thread_t*)dr_thread_alloc(drcontext, sizeof(per_thread_t));
    drmgr_set_tls_field(drcontext, tls_index, data);
    data->buf_base = (char*)dr_thread_alloc(drcontext, MEM_BUF_SIZE);
    data->buf_ptr = data->buf_base;
    /* set buf_end to be negative of address of buffer end for the lea later */
    data->buf_end = -(ptr_int_t)(data->buf_base + MEM_BUF_SIZE);
    data->num_refs = 0;

    /* We're going to dump our data to a per-thread file.
     * On Windows we need an absolute path so we place it in
     * the same directory as our library. We could also pass
     * in a path as a client argument.
     */
//    data->log = log_file_open(client_id, drcontext, NULL /* using client lib path */, "memtrace",
//#ifndef WINDOWS
//        DR_FILE_CLOSE_ON_FORK |
//#endif
//            DR_FILE_ALLOW_LARGE);
//    data->logf = log_stream_from_file(data->log);
    data->threadID = thread_idx++;
#if OUTPUT_TEXT
    data->logf = fopen((std::string("regina.tmp.") + std::to_string(data->threadID) + std::string(".mmd")).c_str(), "w");
    fprintf(data->logf,
        "Format: <instr address>,<(r)ead/(w)rite>,<data size>,<data address>\n");
#else
    data->logf = fopen((std::string("regina.tmp.") + std::to_string(data->threadID) + std::string(".mmd")).c_str(), "wb");
#endif
}

static void
event_thread_exit(void* drcontext) {
    per_thread_t* data;

    memtrace(drcontext);
    data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_index);
    dr_mutex_lock(mutex);
    global_num_refs += data->num_refs;
    dr_mutex_unlock(mutex);
#ifdef OUTPUT_TEXT
    log_stream_close(data->logf); /* closes fd too */
#else
    fclose(data->logf);
    data->logf = fopen((std::string("regina.tmp.") + std::to_string(data->threadID) + std::string(".mmd")).c_str(), "rb");
    process_file(data->logf, file_idx++);
    fclose(data->logf);
    //log_file_close(data->log);
    //delayed_files.push_back(data->log);
#endif
    dr_thread_free(drcontext, data->buf_base, MEM_BUF_SIZE);
    dr_thread_free(drcontext, data, sizeof(per_thread_t));
}

/* we transform string loops into regular loops so we can more easily
 * monitor every memory reference they make
 */
static dr_emit_flags_t
event_bb_app2app(void* drcontext, void* tag, instrlist_t* bb, bool for_trace,
    bool translating) {
    if (!drutil_expand_rep_string(drcontext, bb)) {
        DR_ASSERT(false);
        /* in release build, carry on: we'll just miss per-iter refs */
    }
    if (!drx_expand_scatter_gather(drcontext, bb, NULL)) {
        DR_ASSERT(false);
    }
    return DR_EMIT_DEFAULT;
}

static dr_emit_flags_t
event_bb_analysis(void* drcontext, void* tag, instrlist_t* bb, bool for_trace,
    bool translating, void** user_data) {
    instru_data_t* data = (instru_data_t*)dr_thread_alloc(drcontext, sizeof(*data));
    data->last_pc = NULL;
    *user_data = (void*)data;
    return DR_EMIT_DEFAULT;
}

static void at_call(app_pc instr_addr, app_pc target_addr) {
    void* drcontext = dr_get_current_drcontext();
    per_thread_t* data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_index);

#ifdef OUTPUT_TEXT
    fprintf(data->logf, PIFX ",%c,%d," PIFX "\n", (ptr_uint_t)instr_addr,
        'c', 0, (ptr_uint_t)target_addr);
#else
    mem_ref_t mem_ref = {};
    mem_ref.memRef = false;
    mem_ref.call = true;
    mem_ref.ind = false;
    mem_ref.pc = instr_addr;
    mem_ref.target = target_addr;
    //dr_write_file(data->log, &mem_ref, sizeof(mem_ref));
    fwrite(&mem_ref, sizeof(mem_ref), 1, data->logf);
#endif
}

static void
at_call_ind(app_pc instr_addr, app_pc target_addr) {
    void* drcontext = dr_get_current_drcontext();
    per_thread_t* data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_index);

#ifdef OUTPUT_TEXT
    fprintf(data->logf, PIFX ",%c,%d," PIFX "\n", (ptr_uint_t)instr_addr,
        'i', 0, (ptr_uint_t)target_addr);
#else
    mem_ref_t mem_ref = {};
    mem_ref.memRef = false;
    mem_ref.call = true;
    mem_ref.ind = true;
    mem_ref.pc = instr_addr;
    mem_ref.target = target_addr;
    //dr_write_file(data->log, &mem_ref, sizeof(mem_ref));
    fwrite(&mem_ref, sizeof(mem_ref), 1, data->logf);
#endif
}

static void
at_return(app_pc instr_addr, app_pc target_addr) {
    void* drcontext = dr_get_current_drcontext();
    per_thread_t* data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_index);

#ifdef OUTPUT_TEXT
    fprintf(data->logf, PIFX ",%c,%d," PIFX "\n", (ptr_uint_t)instr_addr,
        'e', 0, (ptr_uint_t)target_addr);
#else
    mem_ref_t mem_ref = {};
    mem_ref.memRef = false;
    mem_ref.call = false;
    mem_ref.ind = false;
    mem_ref.pc = instr_addr;
    mem_ref.target = target_addr;
    //dr_write_file(data->log, &mem_ref, sizeof(mem_ref));
    fwrite(&mem_ref, sizeof(mem_ref), 1, data->logf);
#endif
}

/* event_bb_insert calls instrument_mem to instrument every
 * application memory reference.
 */
static dr_emit_flags_t
event_bb_insert(void* drcontext, void* tag, instrlist_t* bb, instr_t* where,
    bool for_trace, bool translating, void* user_data) {
    int i;
    instru_data_t* data = (instru_data_t*)user_data;
    /* Use the drmgr_orig_app_instr_* interface to properly handle our own use
     * of drutil_expand_rep_string() and drx_expand_scatter_gather() (as well
     * as another client/library emulating the instruction stream).
     */
    instr_t* instr_fetch = drmgr_orig_app_instr_for_fetch(drcontext);
    if (instr_fetch != NULL)
        data->last_pc = instr_get_app_pc(instr_fetch);
    app_pc last_pc = data->last_pc;
    if (drmgr_is_last_instr(drcontext, where))
        dr_thread_free(drcontext, data, sizeof(*data));

    instr_t* instr_operands = drmgr_orig_app_instr_for_operands(drcontext);
    if (instr_operands == NULL || (!instr_writes_memory(instr_operands) && !instr_reads_memory(instr_operands)))
        return DR_EMIT_DEFAULT;
    DR_ASSERT(instr_is_app(instr_operands));
    DR_ASSERT(last_pc != NULL);

    if (instr_is_call_direct(instr_operands)) {
        dr_insert_call_instrumentation(drcontext, bb, where, (app_pc)at_call);
    } else if (instr_is_call_indirect(instr_operands)) {
        dr_insert_mbr_instrumentation(drcontext, bb, where, (app_pc)at_call_ind,
            SPILL_SLOT_1);
    } else if (instr_is_return(instr_operands)) {
        dr_insert_mbr_instrumentation(drcontext, bb, where, (app_pc)at_return,
            SPILL_SLOT_1);
    }

    if (instr_reads_memory(instr_operands)) {
        for (i = 0; i < instr_num_srcs(instr_operands); i++) {
            if (opnd_is_memory_reference(instr_get_src(instr_operands, i))) {
                instrument_mem(drcontext, bb, where, last_pc, instr_operands, i, false);
            }
        }
    }
    if (instr_writes_memory(instr_operands)) {
        for (i = 0; i < instr_num_dsts(instr_operands); i++) {
            if (opnd_is_memory_reference(instr_get_dst(instr_operands, i))) {
                instrument_mem(drcontext, bb, where, last_pc, instr_operands, i, true);
            }
        }
    }
    return DR_EMIT_DEFAULT;
}

static void
memtrace(void* drcontext) {
    per_thread_t* data;
    int num_refs;
    mem_ref_t* mem_ref;
#ifdef OUTPUT_TEXT
    int i;
#endif

    data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_index);
    mem_ref = (mem_ref_t*)data->buf_base;
    num_refs = (int)((mem_ref_t*)data->buf_ptr - mem_ref);

#ifdef OUTPUT_TEXT
    /* We use libc's fprintf as it is buffered and much faster than dr_fprintf
     * for repeated printing that dominates performance, as the printing does here.
     */
    for (i = 0; i < num_refs; i++) {
        /* We use PIFX to avoid leading zeroes and shrink the resulting file. */
        fprintf(data->logf, PIFX ",%c,%d," PIFX "\n", (ptr_uint_t)mem_ref->pc,
            mem_ref->write ? 'w' : 'r', (int)mem_ref->size,
            (ptr_uint_t)mem_ref->addr);
        ++mem_ref;
    }
#else
    //dr_write_file(data->log, data->buf_base, (size_t)(data->buf_ptr - data->buf_base));
    fwrite(data->buf_base, (size_t)(data->buf_ptr - data->buf_base), 1, data->logf);
#endif

    memset(data->buf_base, 0, MEM_BUF_SIZE);
    data->num_refs += num_refs;
    data->buf_ptr = data->buf_base;
}

/* clean_call dumps the memory reference info to the log file */
static void
clean_call(void) {
    void* drcontext = dr_get_current_drcontext();
    memtrace(drcontext);
}

static void
code_cache_init(void) {
    void* drcontext;
    instrlist_t* ilist;
    instr_t* where;
    byte* end;

    drcontext = dr_get_current_drcontext();
    code_cache = (app_pc)dr_nonheap_alloc(page_size, DR_MEMPROT_READ | DR_MEMPROT_WRITE | DR_MEMPROT_EXEC);
    ilist = instrlist_create(drcontext);
    /* The lean procedure simply performs a clean call, and then jumps back
     * to the DR code cache.
     */
    where = INSTR_CREATE_jmp_ind(drcontext, opnd_create_reg(DR_REG_XCX));
    instrlist_meta_append(ilist, where);
    /* clean call */
    dr_insert_clean_call(drcontext, ilist, where, (void*)clean_call, false, 0);
    /* Encodes the instructions into memory and then cleans up. */
    end = instrlist_encode(drcontext, ilist, code_cache, false);
    DR_ASSERT((size_t)(end - code_cache) < page_size);
    instrlist_clear_and_destroy(drcontext, ilist);
    /* set the memory as just +rx now */
    dr_memory_protect(code_cache, page_size, DR_MEMPROT_READ | DR_MEMPROT_EXEC);
}

static void
code_cache_exit(void) {
    dr_nonheap_free(code_cache, page_size);
}

/*
 * instrument_mem is called whenever a memory reference is identified.
 * It inserts code before the memory reference to to fill the memory buffer
 * and jump to our own code cache to call the clean_call when the buffer is full.
 */
static void
instrument_mem(void* drcontext, instrlist_t* ilist, instr_t* where, app_pc pc,
    instr_t* memref_instr, int pos, bool write) {
    instr_t *instr, *call, *restore;
    opnd_t ref, opnd1, opnd2;
    reg_id_t reg1, reg2;
    drvector_t allowed;
    per_thread_t* data;

    data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_index);

    /* Steal two scratch registers.
     * reg2 must be ECX or RCX for jecxz.
     */
    drreg_init_and_fill_vector(&allowed, false);
    drreg_set_vector_entry(&allowed, DR_REG_XCX, true);
    if (drreg_reserve_register(drcontext, ilist, where, &allowed, &reg2) != DRREG_SUCCESS || drreg_reserve_register(drcontext, ilist, where, NULL, &reg1) != DRREG_SUCCESS) {
        DR_ASSERT(false); /* cannot recover */
        drvector_delete(&allowed);
        return;
    }
    drvector_delete(&allowed);

    if (write)
        ref = instr_get_dst(memref_instr, pos);
    else
        ref = instr_get_src(memref_instr, pos);

    /* use drutil to get mem address */
    drutil_insert_get_mem_addr(drcontext, ilist, where, ref, reg1, reg2);

    /* The following assembly performs the following instructions
     * buf_ptr->write = write;
     * buf_ptr->addr  = addr;
     * buf_ptr->size  = size;
     * buf_ptr->pc    = pc;
     * buf_ptr++;
     * if (buf_ptr >= buf_end_ptr)
     *    clean_call();
     */
    drmgr_insert_read_tls_field(drcontext, tls_index, ilist, where, reg2);
    /* Load data->buf_ptr into reg2 */
    opnd1 = opnd_create_reg(reg2);
    opnd2 = OPND_CREATE_MEMPTR(reg2, offsetof(per_thread_t, buf_ptr));
    instr = INSTR_CREATE_mov_ld(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Set memRef */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(mem_ref_t, memRef));
    opnd2 = OPND_CREATE_INT32(true);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Set call */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(mem_ref_t, call));
    opnd2 = OPND_CREATE_INT32(false);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Set ind */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(mem_ref_t, ind));
    opnd2 = OPND_CREATE_INT32(false);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Move write/read to write field */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(mem_ref_t, write));
    opnd2 = OPND_CREATE_INT32(write);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Store address in memory ref */
    opnd1 = OPND_CREATE_MEMPTR(reg2, offsetof(mem_ref_t, addr));
    opnd2 = opnd_create_reg(reg1);
    instr = INSTR_CREATE_mov_st(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Store size in memory ref */
    opnd1 = OPND_CREATE_MEMPTR(reg2, offsetof(mem_ref_t, size));
    /* drutil_opnd_mem_size_in_bytes handles OP_enter */
    opnd2 = OPND_CREATE_INT32(drutil_opnd_mem_size_in_bytes(ref, memref_instr));
    instr = INSTR_CREATE_mov_st(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Store pc in memory ref */
    /* For 64-bit, we can't use a 64-bit immediate so we split pc into two halves.
     * We could alternatively load it into reg1 and then store reg1.
     * We use a convenience routine that does the two-step store for us.
     */
    opnd1 = OPND_CREATE_MEMPTR(reg2, offsetof(mem_ref_t, pc));
    instrlist_insert_mov_immed_ptrsz(drcontext, (ptr_int_t)pc, opnd1, ilist, where, NULL,
        NULL);

    /* Increment reg value by pointer size using lea instr */
    opnd1 = opnd_create_reg(reg2);
    opnd2 = opnd_create_base_disp(reg2, DR_REG_NULL, 0, sizeof(mem_ref_t), OPSZ_lea);
    instr = INSTR_CREATE_lea(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Update the data->buf_ptr */
    drmgr_insert_read_tls_field(drcontext, tls_index, ilist, where, reg1);
    opnd1 = OPND_CREATE_MEMPTR(reg1, offsetof(per_thread_t, buf_ptr));
    opnd2 = opnd_create_reg(reg2);
    instr = INSTR_CREATE_mov_st(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* we use lea + jecxz trick for better performance
     * lea and jecxz won't disturb the eflags, so we won't insert
     * code to save and restore application's eflags.
     */
    /* lea [reg2 - buf_end] => reg2 */
    opnd1 = opnd_create_reg(reg1);
    opnd2 = OPND_CREATE_MEMPTR(reg1, offsetof(per_thread_t, buf_end));
    instr = INSTR_CREATE_mov_ld(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);
    opnd1 = opnd_create_reg(reg2);
    opnd2 = opnd_create_base_disp(reg1, reg2, 1, 0, OPSZ_lea);
    instr = INSTR_CREATE_lea(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* jecxz call */
    call = INSTR_CREATE_label(drcontext);
    opnd1 = opnd_create_instr(call);
    instr = INSTR_CREATE_jecxz(drcontext, opnd1);
    instrlist_meta_preinsert(ilist, where, instr);

    /* jump restore to skip clean call */
    restore = INSTR_CREATE_label(drcontext);
    opnd1 = opnd_create_instr(restore);
    instr = INSTR_CREATE_jmp(drcontext, opnd1);
    instrlist_meta_preinsert(ilist, where, instr);

    /* clean call */
    /* We jump to lean procedure which performs full context switch and
     * clean call invocation. This is to reduce the code cache size.
     */
    instrlist_meta_preinsert(ilist, where, call);
    /* mov restore DR_REG_XCX */
    opnd1 = opnd_create_reg(reg2);
    /* this is the return address for jumping back from lean procedure */
    opnd2 = opnd_create_instr(restore);
    /* We could use instrlist_insert_mov_instr_addr(), but with a register
     * destination we know we can use a 64-bit immediate.
     */
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);
    /* jmp code_cache */
    opnd1 = opnd_create_pc(code_cache);
    instr = INSTR_CREATE_jmp(drcontext, opnd1);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Restore scratch registers */
    instrlist_meta_preinsert(ilist, where, restore);
    if (drreg_unreserve_register(drcontext, ilist, where, reg1) != DRREG_SUCCESS || drreg_unreserve_register(drcontext, ilist, where, reg2) != DRREG_SUCCESS)
        DR_ASSERT(false);
}