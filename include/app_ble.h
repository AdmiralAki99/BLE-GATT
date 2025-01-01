#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "esp_log.h"

#define BLE_DEVICE_NAME "ESP32_BLE"

#include "bsp_ble.h"

void app_ble_start(){
    bsp_initialize_server(BLE_DEVICE_NAME);
}

void app_ble_stop(){
    bsp_stop_server();
}

void app_ble_send_notification(uint8_t profile_id, uint8_t* data, uint16_t length){
    bsp_push_data_to_notification_queue(profile_id, data, length);
    bsp_send_notification_data(profile_id);
}