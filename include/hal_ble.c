#include "hal_ble.h"

esp_gatt_status_t hal_ble_get_attr_value(uint16_t attr_handle,uint16_t *attribute_length,const uint8_t **attribute_value){
    esp_gatt_status_t status = esp_ble_gatts_get_attr_value(attr_handle,attribute_length,attribute_value);
    return status;
}

esp_err_t hal_ble_set_attr_value(uint16_t attr_handle,uint16_t attribute_length,uint8_t *attribute_value){
    esp_err_t err = esp_ble_gatts_set_attr_value(attr_handle,attribute_length,attribute_value);
    return err;
}

esp_err_t hal_ble_set_adv_tx_power_low(){
    esp_err_t err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,ESP_PWR_LVL_N12);
    return err;
}

esp_err_t hal_ble_set_adv_tx_power_high(){
    esp_err_t err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,ESP_PWR_LVL_P9);
    return err;
}

esp_err_t hal_ble_set_device_name(char *device_name){
    esp_err_t err = esp_ble_gap_set_device_name(device_name);
    return err;
}

esp_err_t hal_ble_enable_bluedroid(){
    esp_err_t err = esp_bluedroid_enable();
    return err;
}

esp_err_t hal_ble_set_local_mtu(uint16_t mtu){
    esp_err_t err = esp_ble_gatt_set_local_mtu(mtu);
    return err;
}

uint64_t hal_ble_get_time(bool in_ms){
    uint64_t time = esp_timer_get_time();
    if(in_ms){
        time /= 1000;
    }
    return time;
}


esp_err_t hal_ble_add_characteristic(uint16_t service_handle,esp_bt_uuid_t *characteristic_uuid,esp_gatt_perm_t permissions,esp_gatt_char_prop_t properties,esp_attr_value_t *attribute_value){
    esp_err_t err = esp_ble_gatts_add_char(service_handle,characteristic_uuid,permissions,properties,attribute_value,NULL);
    return err;
}

esp_err_t hal_ble_add_characteristic_descriptor(uint16_t service_handle,esp_bt_uuid_t *cccd_uuid,esp_gatt_perm_t permissions,uint16_t inital_value){
    esp_err_t err = esp_ble_gatts_add_char_descr(service_handle,cccd_uuid,permissions,NULL,inital_value);
    return err;
}

esp_err_t hal_ble_init_nvs(){
    esp_err_t err = nvs_flash_init();
    return err;
}

esp_err_t hal_ble_release_bt_controller_mem(esp_bt_mode_t bt_mode){
    esp_err_t err = esp_bt_controller_mem_release(bt_mode);
    return err;
}

esp_err_t hal_ble_init_bt_controller(esp_bt_controller_config_t *ble_cfg){
    esp_err_t err = esp_bt_controller_init(ble_cfg);
    return err;
}

esp_err_t hal_ble_enable_bt_controller(esp_bt_mode_t bt_mode){
    esp_err_t err = esp_bt_controller_enable(bt_mode);
    return err;
}

esp_err_t hal_ble_init_bluedroid(){
    esp_err_t err = esp_bluedroid_init();
    return err;
}

esp_err_t hal_ble_register_gatt_server_callback(esp_gatts_cb_t gatts_cb){
    esp_err_t err = esp_ble_gatts_register_callback(gatts_cb);
    return err;
}

esp_err_t hal_ble_register_gap_server_callback(esp_gap_ble_cb_t gap_cb){
    esp_err_t err = esp_ble_gap_register_callback(gap_cb);
    return err;
}

esp_err_t hal_ble_register_gatt_server_app_profile(uint16_t app_id){
    esp_err_t err = esp_ble_gatts_app_register(app_id);
    return err;
}

esp_err_t hal_ble_set_gap_server_config_adv_data(esp_ble_adv_data_t *adv_data){
    esp_err_t err = esp_ble_gap_config_adv_data(adv_data);
    return err;
}

esp_err_t hal_ble_start_gap_server_advertisement(esp_ble_adv_params_t *adv_params){
    esp_err_t err = esp_ble_gap_start_advertising(adv_params);
    return err;
}

esp_err_t hal_ble_stop_gap_server_advertisement(){
    esp_err_t err = esp_ble_gap_stop_advertising();
    return err;
}

esp_err_t hal_ble_send_notification(uint16_t gatt_if,uint16_t conn_id,uint16_t char_handle,uint16_t length,uint8_t *value){
    esp_err_t err = esp_ble_gatts_send_indicate(gatt_if,conn_id,char_handle,length,value,false);
    return err;
}

esp_err_t hal_ble_send_indication(uint16_t gatt_if,uint16_t conn_id,uint16_t char_handle,uint16_t length,uint8_t *value){
    esp_err_t err = esp_ble_gatts_send_indicate(gatt_if,conn_id,char_handle,length,value,true);
    return err;
}

esp_err_t hal_ble_update_conn_params(esp_ble_conn_update_params_t *params){
    esp_err_t err = esp_ble_gap_update_conn_params(params);
    return err;
}

esp_err_t hal_ble_send_gatt_response(uint16_t gatt_if,uint16_t conn_id,uint32_t trans_id,esp_gatt_status_t status,esp_gatt_rsp_t *rsp){
    esp_err_t err = esp_ble_gatts_send_response(gatt_if,conn_id,trans_id,status,rsp);
    return err;
}

esp_err_t hal_ble_create_service(uint16_t gatt_if,esp_gatt_srvc_id_t* service_uuid,uint16_t num_handles){
    esp_err_t err = esp_ble_gatts_create_service(gatt_if,service_uuid,num_handles);
    return err;
}

esp_gatt_srvc_id_t hal_ble_create_service_id(uint16_t service_id){
    // Need to create the service for the profile
    esp_gatt_srvc_id_t service_uuid = {
        // Need to give it an id and show if it is primary service for that profile or not
        .is_primary = true,
        .id = {
            .uuid = {
                .len = ESP_UUID_LEN_128, // 128/8 = 16 bytes
                .uuid = {
                    .uuid16 = service_id
                }
            },
            .inst_id = 0
        }
    };
    return service_uuid;
}

esp_ble_conn_update_params_t hal_ble_create_conn_params(uint16_t min_interval,uint16_t max_interval,uint16_t latency,uint16_t timeout){
    esp_ble_conn_update_params_t conn_params = {
        .min_int = min_interval,
        .max_int = max_interval,
        .latency = latency,
        .timeout = timeout
    };
    return conn_params;
}

esp_bt_uuid_t hal_ble_create_uuid(uint16_t uuid,uint8_t len){
    esp_bt_uuid_t bt_uuid = {
        .len = len,
        .uuid = {
            .uuid16 = uuid
        }
    };

    return bt_uuid;
}

esp_gatt_rsp_t hal_ble_create_gatt_response(uint16_t handle,uint16_t length,uint8_t *value){
    esp_gatt_rsp_t rsp = {
        .attr_value = {
            .handle = handle,
            .len = length
        }
    };

    memcpy(rsp.attr_value.value,value,length);

    return rsp;
}

esp_err_t hal_ble_add_char_descriptor(uint16_t service_handle,esp_bt_uuid_t* cccd_uuid,esp_gatt_perm_t permissions,uint16_t initial_value){
    esp_err_t err = esp_ble_gatts_add_char_descr(service_handle,cccd_uuid,permissions,initial_value,NULL);
    return err;
}

esp_gatt_perm_t hal_ble_create_permissions(bool read,bool write){
    esp_gatt_perm_t permissions = 0;

    if(read){
        permissions |= ESP_GATT_PERM_READ;
    }

    if(write){
        permissions |= ESP_GATT_PERM_WRITE;
    }

    return permissions;
}

esp_gatt_char_prop_t hal_ble_create_characteristic_property(bool read,bool write,bool notify,bool indicate){
    esp_gatt_char_prop_t properties = 0;

    if(read){
        properties |= ESP_GATT_CHAR_PROP_BIT_READ;
    }

    if(write){
        properties |= ESP_GATT_CHAR_PROP_BIT_WRITE;
    }

    if(notify){
        properties |= ESP_GATT_CHAR_PROP_BIT_NOTIFY;
    }

    if(indicate){
        properties |= ESP_GATT_CHAR_PROP_BIT_INDICATE;
    }

    return properties;
}

esp_err_t hal_ble_start_service(uint16_t service_handle){
    esp_err_t err = esp_ble_gatts_start_service(service_handle);
    return err;
}
