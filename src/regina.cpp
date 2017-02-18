#include "dr_api.h"

#include "drmgr.h"

#include "log.h"


// Forward declarations
static void event_exit(void);
//---------------------

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

    // Notify dr log of this client
    dr_log(NULL, LOG_ALL, 1, "Client 'regina' is running\n");
    REGINA_LOG("Client 'regina' is running\n");
}


/*
 * event_exit
 */
static void event_exit(void) {
    // Exit extensions
    drmgr_exit();
}