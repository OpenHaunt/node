/*
 * The MQTT side of a node, as OpenHaunt/node doc 06 lays it out:
 *   openhaunt/<sn>/status         online|offline, retained, offline as the will
 *   openhaunt/<sn>/input/<n>      an edge or a reading, with the node's timestamp
 *   openhaunt/<sn>/output/<n>/set what a controller publishes to drive a port
 *   openhaunt/<sn>/health         uptime, temperature, errors, every 10 s
 * Nothing is published until a controller has said where the broker is.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oh_mqtt_start(void);
bool oh_mqtt_connected(void);
/* "" when no broker is configured. */
const char *oh_mqtt_broker(void);

#ifdef __cplusplus
}
#endif
