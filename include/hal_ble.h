#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

// Hardware Specific Libraries

#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"

#include "esp_gap_ble_api.h"
#include "esp_timer.h"
#include "esp_sleep.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"

#include "sdkconfig.h"

/*
    HAL BLE API
*/

// Getters & Setters for BLE GATT Server

/*!
    @brief Get Attribute Value
    @param attr_handle : The attribute handle
    @param attribute_length : The length of the attribute
    @param attribute_value : The value of the attribute
    @return 
            - ESP_GATT_OK : Success - otherwise, error code
*/
esp_gatt_status_t hal_ble_get_attr_value(uint16_t attr_handle,uint16_t *attribute_length,uint8_t *attribute_value);

/*!
    @brief Set Attribute Value
    @param attr_handle : The attribute handle
    @param attribute_length : The length of the attribute
    @param attribute_value : The value of the attribute
    @return
            - ESP_GATT_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_set_attr_value(uint16_t attr_handle,uint16_t attribute_length,uint8_t *attribute_value);

/*!
    @brief Set Advertisement Tx Power To Low
    @return
            - ESP_GATT_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_set_adv_tx_power_low();

/*!
    @brief Set Advertisement Tx Power To High
    @return
            - ESP_GATT_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_set_adv_tx_power_high();

/*!
    @brief Set Device Name
    @param device_name : The name of the device
    @return
            - ESP_GATT_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_set_device_name(char *device_name);

/*!
    @brief Set Local MTU
    @param mtu : The MTU value
    @return
            - ESP_GATT_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_set_local_mtu(uint16_t mtu);

/*!
    @brief Get Time
    @param in_ms : The time in milliseconds
    @return time in milliseconds
*/
uint64_t hal_ble_get_time(bool in_ms);


// Add Methods for BLE GATT Server

/*!
    @brief Add Characteristic
    @param service_handle : The service handle
    @param characteristic_uuid : The UUID of the characteristic
    @param permissions : The permissions of the characteristic
    @param properties : The properties of the characteristic
    @param attribute_value : The value of the attribute
    @return
            - ESP_GATT_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_add_characteristic(uint16_t service_handle,esp_bt_uuid_t *characteristic_uuid,esp_gatt_perm_t permissions,esp_gatt_char_prop_t properties,esp_attr_value_t *attribute_value);

/*!
    @brief Add Characteristic Descriptor
    @param service_handle : The service handle
    @param cccd_uuid : The UUID of the characteristic descriptor
    @param permissions : The permissions of the characteristic descriptor
    @param inital_value : The initial value of the characteristic descriptor
    @return
            - ESP_GATT_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_add_characteristic_descriptor(uint16_t service_handle,esp_bt_uuid_t *cccd_uuid,esp_gatt_perm_t permissions,uint16_t inital_value);

/*
    Configuration Methods for BLE GATT Server
*/

/*!
    @brief Initialize Non Volatile Storage (NVS)
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_init_nvs();

/*!
    @brief Release Bluetooth Controller Memory
    @param bt_mode : The mode of the bluetooth controller
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_release_bt_controller_mem(esp_bt_mode_t bt_mode);

/*!
    @brief Initialize Bluetooth Controller
    @param ble_cfg : The configuration of the bluetooth controller
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_init_bt_controller(esp_bt_controller_config_t *ble_cfg);

/*!
    @brief Enable Bluetooth Controller
    @param bt_mode : The mode of the bluetooth controller
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_enable_bt_controller(esp_bt_mode_t bt_mode);

/*!
    @brief Initialize Bluedroid Stack
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_init_bluedroid();

/*!
    @brief Enable Bluedroid Stack
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_enable_bluedroid();

/*!
    @brief Register GATT Server Callback
    @param gatts_cb : The GATT Server Callback
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_register_gatt_server_callback(esp_gatts_cb_t gatts_cb);

/*!
    @brief Register GAP Server Callback
    @param gap_cb : The GAP Server Callback
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_register_gap_server_callback(esp_gap_ble_cb_t gap_cb);

/*!
    @brief Register GATT Server Application Profile
    @param app_id : The GATT Server Application Profile ID
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_register_gatt_server_app_profile(uint16_t app_id);

/*!
    @brief Set GAP Server Config Advertisement Data
    @param adv_data : The Advertisement Data
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_set_gap_server_config_adv_data(esp_ble_adv_data_t *adv_data);

/*!
    @brief Start GAP Server Advertisement
    @param adv_params : The Advertisement Parameters
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_start_gap_server_advertisement(esp_ble_adv_params_t *adv_params);

/*!
    @brief Stop GAP Server Advertisement
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_stop_gap_server_advertisement();

/*!
    @brief Send Notification
    @param gatt_if : The GATT Interface
    @param conn_id : The Connection ID
    @param char_handle : The Characteristic Handle
    @param length : The length of the data
    @param value : The value of the data
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_send_notification(uint16_t gatt_if,uint16_t conn_id,uint16_t char_handle,uint16_t length,uint8_t *value);

/*!
    @brief Send Indication
    @param gatt_if : The GATT Interface
    @param conn_id : The Connection ID
    @param char_handle : The Characteristic Handle
    @param length : The length of the data
    @param value : The value of the data
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_send_indication(uint16_t gatt_if,uint16_t conn_id,uint16_t char_handle,uint16_t length,uint8_t *value);

/*!
    @brief Update Connection Parammeters
    @param params : The connection parameters
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_update_conn_params(esp_ble_conn_update_params_t *params);

/*!
    @brief Set GATT Response
    @param gatt_if : The GATT Interface
    @param conn_id : The Connection
    @param trans_id : The Transfer ID
    @param status : The status of the response
    @param rsp : The response data
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_send_gatt_response(uint16_t gatt_if,uint16_t conn_id,uint32_t trans_id,esp_gatt_status_t status,esp_gatt_rsp_t *rsp);

/*!
    @brief Create Service
    @param gatt_if : The GATT Interface
    @param service_id : The Service ID
    @param num_handles : The number of handles
    @return
            - ESP_OK : Success - otherwise, error code
*/
esp_err_t hal_ble_create_service(uint16_t gatt_if,uint16_t *service_id,uint16_t num_handles);







