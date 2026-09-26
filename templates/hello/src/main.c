#include <aster/aster.h>

int aster_app_init(void)
{
    static const char message[] = "Hello Aster: one application, independent of the hardware.\n";
    aster_log(ASTER_LOG_INFO, message, sizeof(message) - 1);
    return aster_system_get_abi_version() == ASTER_ABI_VERSION ? ASTER_OK : ASTER_EABI;
}

void aster_app_event(const aster_event_t *event)
{
    if (event->type == ASTER_EVENT_FRAME) {
        static const char message[] = "Frame received; application lifecycle passed.\n";
        aster_log(ASTER_LOG_INFO, message, sizeof(message) - 1);
        aster_app_exit(0);
    }
}
