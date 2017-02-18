#include <vector>

#include "dr_api.h"

#include "drmgr.h"

#include "log.h"
#include "trace_ref_t.h"


// Forward declarations
static void event_exit(void);
static void event_thread_init(void *drcontext);
static void event_thread_exit(void *drcontext);
//---------------------


// Global variables
static int tls_index;
static std::vector<std::vector<trace_ref_t>> trace_storage;
//-----------------

/*
 * dr_client_main
 */
DR_EXPORT void dr_client_main(client_id_t id, int argc, const char *argv[]) {
    dr_log(NULL, LOG_ALL, 1, "Client 'regina' initializing\n");
    REGINA_LOG("Client 'regina' initializing\n");

    // Client setup
    dr_set_client_name("regina -- mem- and call-trace", "-");
    dr_set_client_version_string("0.1.0");

    // Init extension
    if (!drmgr_init()) {
        REGINA_LOG_ERROR("Failed to initialize drmgr\n");
        return;
    }

    // Register events
    dr_register_exit_event(event_exit);
    if (!drmgr_register_thread_init_event(event_thread_init) ||
        !drmgr_register_thread_exit_event(event_thread_exit)) {
        REGINA_LOG_ERROR("Unable to register drmgr events\n");
        return;
    }

    tls_index = drmgr_register_tls_field();
    if (tls_index == -1) {
        REGINA_LOG_ERROR("No TLS fields available\n");
        return;
    }

    // Notify dr log of this client
    dr_log(NULL, LOG_ALL, 1, "Client 'regina' is running\n");
    REGINA_LOG("Client 'regina' is running\n");
}


/*
 * event_exit
 */
static void event_exit(void) {
    // Unregister events
    if (!drmgr_unregister_thread_init_event(event_thread_init) ||
        !drmgr_unregister_thread_exit_event(event_thread_exit)) {
        REGINA_LOG_ERROR("Unable to unregister drmgr events\n");
    }

    // Exit extensions
    drmgr_exit();
}


/*
 * event_thread_init
 */
static void event_thread_init(void *drcontext) {

}


/*
 * event_thread_exit
 */
static void event_thread_exit(void *drcontext) {

}