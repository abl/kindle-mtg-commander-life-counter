#ifndef MTG_LIFE_COUNTER_DEVICE_H
#define MTG_LIFE_COUNTER_DEVICE_H

#include <stdbool.h>

typedef bool (*DeviceReadInt)(void *context, const char *property, int *value);
typedef bool (*DeviceWriteInt)(void *context, const char *property, int value);
typedef bool (*DeviceReadSleeping)(void *context, bool *sleeping);
typedef bool (*DevicePulseIdleTimeout)(void *context);

typedef struct {
    void *context;
    DeviceReadInt read_int;
    DeviceWriteInt write_int;
    DeviceReadSleeping read_sleeping;
    DevicePulseIdleTimeout pulse_idle_timeout;
} DeviceBackend;

typedef struct {
    const char *get_program;
    const char *set_program;
    unsigned int timeout_ms;
} DeviceCommandConfig;

typedef struct {
    bool battery_available;
    int battery_percent;
    bool light_available;
    int light_level;
    int light_max;
} DeviceStatus;

typedef enum {
    DEVICE_POWER_NONE,
    DEVICE_POWER_SLEEP,
    DEVICE_POWER_WAKE,
} DevicePowerEvent;

typedef enum {
    DEVICE_POWER_OBSERVED_UNKNOWN,
    DEVICE_POWER_OBSERVED_SLEEPING,
    DEVICE_POWER_OBSERVED_AWAKE,
} DevicePowerObservedState;

typedef enum {
    DEVICE_IDLE_TIMEOUT_FAILED,
    DEVICE_IDLE_TIMEOUT_SKIPPED,
    DEVICE_IDLE_TIMEOUT_APPLIED,
} DeviceIdleTimeoutResult;

typedef struct {
    bool sleeping;
    bool light_off;
    bool restore_light;
    bool light_before_sleep_known;
    int light_before_sleep;
} DevicePowerState;

DeviceBackend device_command_backend(DeviceCommandConfig *config);
DeviceBackend device_lipc_backend(void);
DeviceIdleTimeoutResult device_idle_timeout_pulse(DeviceBackend *backend);
bool device_refresh(DeviceBackend *backend, DeviceStatus *status);
bool device_toggle_front_light(DeviceBackend *backend, DeviceStatus *status,
                               int *remembered_brightness);
DevicePowerEvent device_power_event_parse(const char *line);
void device_power_state_init(DevicePowerState *state);
bool device_power_sleep(DeviceBackend *backend, DeviceStatus *status,
                        DevicePowerState *state);
bool device_power_wake(DeviceBackend *backend, DeviceStatus *status,
                       DevicePowerState *state);
bool device_power_exit(DeviceBackend *backend, DeviceStatus *status,
                       DevicePowerState *state);
DevicePowerObservedState device_power_reconcile(DeviceBackend *backend,
                                                 DeviceStatus *status,
                                                 DevicePowerState *state);

#endif
