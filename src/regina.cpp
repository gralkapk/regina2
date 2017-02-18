#include "dr_api.h"


// Forward declarations
static void event_exit(void);
//---------------------

DR_EXPORT void dr_client_main(client_id_t id, int argc, const char *argv[]) {
    // Client setup
    dr_set_client_name("regina - mem- and call-trace", "-");
    dr_set_client_version_string("0.1.0");

    dr_register_exit_event(event_exit);
}


static void event_exit(void) {

}