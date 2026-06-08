#include <stdbool.h>
#include <string.h>
#include <systemd/sd-device.h>

#include "drm.h"
#include "lib/log.h"

int drm_seat_has_display(const char *seat_name) {
    sd_device_enumerator *e = NULL;
    int                   result = 0;

    int r = sd_device_enumerator_new(&e);
    if (r < 0) {
        log_error("drm_seat_has_display: sd_device_enumerator_new: %s", strerror(-r));
        return -1;
    }

    r = sd_device_enumerator_add_match_subsystem(e, "drm", true);
    if (r < 0) {
        log_error("drm_seat_has_display: add_match_subsystem: %s", strerror(-r));
        sd_device_enumerator_unref(e);
        return -1;
    }

    for (sd_device *dev = sd_device_enumerator_get_device_first(e); dev;
         dev = sd_device_enumerator_get_device_next(e)) {

        /* DRM connector entries have a "status" sysattr; card and renderD
        devices do not. Skip anything that is not a connected connector. */
        const char *status;
        if (sd_device_get_sysattr_value(dev, "status", &status) < 0)
            continue;
        if (strcmp(status, "connected") != 0)
            continue;

        /* Resolve the seat for this connector. Check the device itself first,
        then its parent (the card). Devices without ID_SEAT belong to seat0. */
        const char *id_seat = "seat0";
        const char *val;
        if (sd_device_get_property_value(dev, "ID_SEAT", &val) == 0) {
            id_seat = val;
        } else {
            sd_device *parent;
            if (sd_device_get_parent(dev, &parent) == 0 &&
                sd_device_get_property_value(parent, "ID_SEAT", &val) == 0)
                id_seat = val;
        }

        if (strcmp(id_seat, seat_name) == 0) {
            result = 1;
            break;
        }
    }

    sd_device_enumerator_unref(e);
    return result;
}
