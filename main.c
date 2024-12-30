#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

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

#define GATT_INIT "GATT_INIT"
#define GATT_CALLBACK "GATT_CALLBACK"
#define GAP_INIT "GAP_INIT"
#define GAP_CALLBACK "GAP_CALLBACK"
#define MUSIC_PROFILE_CB "MUSIC_PROFILE_CB"
#define MUSIC_PLAYBACK_PROFILE_CB "MUSIC_PLAYBACK_CB"
#define TODO_PROFILE_CB "TODO_PROFILE_CB"
#define TIME_PROFILE_CB "TIME_PROFILE_CB"

#define NUM_PROFILES 4
#define MUSIC_PROFILE_ID 0
#define TODO_PROFILE_ID 1
#define TIME_PROFILE_ID 2
#define MUSIC_PLAYBACK_PROFILE_ID 3

#define MAX_NOTIFCATION_RETRIES 3
#define NOTIFICATION_INTERVAL 500 // 0.5 second interval for notifications

#define PWR_ADV_SWITCH_TIMOUT 30000 // 30 seconds for the power management task to switch between full power and low power mode

#define DEBUG
// #define TESTING

/*
    Creating the Characteristic strorage containers for the profiles
*/

#define MUSIC_PROFILE_CHAR_LEN 32
#define TODO_PROFILE_CHAR_LEN 32
#define TIME_PROFILE_CHAR_LEN 5
#define MUSIC_PLAYBACK_CHAR_LEN 5

// static uint8_t music_profile_characteristic_storage[MUSIC_PROFILE_CHAR_LEN] = {0}; // 100 bytes for the music profile
// static uint8_t todo_profile_characteristic_storage[TODO_PROFILE_CHAR_LEN] = {0}; // 100 bytes for the todo profile
// static uint8_t time_profile_characteristic_storage[TIME_PROFILE_CHAR_LEN] = {0}; // 32 bytes for the time profile

// // Creating a message queue for the notifications
// static uint8_t music_notification_queue_buffer[MUSIC_PROFILE_CHAR_LEN] = {0};
// static uint8_t todo_notification_queue_buffer[TODO_PROFILE_CHAR_LEN] = {0};
// static uint8_t time_notification_queue_buffer[TIME_PROFILE_CHAR_LEN] = {0};

// Create a flag to see if a client is connected
static bool client_connected = false;

// Creating a timer for the advertisement
static TimerHandle_t advertisement_timer; // This timer will be used to switch between full power and low power mode

// Updating the data can cause some issues so Semmaphores need to be used to prevent race conditions
// Creating mutex for each of the number of profiles so that mutual exclusions can be created for anything
// targeting the local storage and the notification queue especially when there is a writing being carried out to the notification and the local storage
static SemaphoreHandle_t profile_semaphores[NUM_PROFILES];


/*
    Creating an array of 16 bit UUIDs for the services that will be created based on the bluetooth specification used as standard
*/

static uint16_t service_uuids[NUM_PROFILES] = {
    0x1840, // Music Service 
    0x1801, // Todo Service
    0x1847, // Time Service
    0x1848 // Music Playback Service
};

/*
    Creating array of 16 bit UUIDs for the characteristics that will be created based on the bluetooth specification used as standard
*/
static uint16_t characteristic_uuids[NUM_PROFILES] = {
    0x2B93, // Music Characteristic
    0x2A3D, // Todo Characteristic
    0x2A2B, // Time Characteristic
    0x2BA3 // Music Playback Characteristic
};


static uint8_t profile_service_uuids[32] = {
  // Music Profile UUID LSB -> MSB
  0x4f,0xaf,0xc2,0x01,0x1f,0xb5,0x45,0x9e,0x8f,0xcc,0xc5,0xc9,0xc3,0x31,0x91,0x4b,
  // Todo list Profile UUID LSB -> MSB
  0x28,0xbd,0x3c,0x28,0x63,0x5d,0x11,0xee,0x8c,0x99,0x02,0x42,0xac,0x12,0x00,0x02,

};

/*
    Creating an array of log tags so that I can easily identify the logs in the console
    0-3 are reserved for GATT Server, 4-6 are reserved for the profiles in the profile id order
*/

static char* log_tags[] = {
    "GATT_INIT",
    "GATT_CALLBACK",
    "GAP_INIT",
    "GAP_CALLBACK",
    "MUSIC_PROFILE_CB",
    "TODO_PROFILE_CB",
    "TIME_PROFILE_CB",
    "MUSIC_PLAYBACK_PROFILE_CB"
};

uint8_t music_profile_attr_value[] = {0x11,0x22,0x33};

/*
    Creating a enum structure for power mode
*/

typedef enum {
    HIGH_POWER_MODE                     = 0,
    LOW_POWER_MODE                      = 1,
    CLIENT_CONN_LOW_POWER_MODE          = 2,
} power_mode_t;

/*
    Creating a structure to hold the profile information
*/

struct gatt_application_profile_t {
    esp_gatt_if_t profile_interface;
    uint16_t application_id;
    uint16_t connection_id;
    uint16_t service_handle;
    uint16_t service_id;
    uint16_t characteristic_handle;
    esp_attr_value_t attribute_value;
    esp_bt_uuid_t characteristic_uuid;
    esp_gatt_perm_t attribute_permissions;
    esp_gatt_perm_t characteristic_properties;
    uint16_t characteristic_descriptor_handle;
    esp_bt_uuid_t characteristic_descriptor_uuid;
    esp_gatts_cb_t profile_event_handler;
    uint16_t cccd_status;
    uint8_t *local_storage;
    uint8_t local_storage_limit;
    uint8_t local_storage_len;
    uint64_t last_notification_time;
    uint8_t *notification_queue_buffer;
    uint8_t notification_queue_len;
};

typedef struct gatt_application_profile_t profile_t;

/*
    Power management variables
*/

uint64_t server_start_timer = 0;
uint64_t client_disconnet_timer = 0;
uint8_t current_power_mode = HIGH_POWER_MODE;

/*
    Declaring the Umbrella GATT Server Profile Event Handler
*/

static void gatt_server_music_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param);
static void gatt_server_todo_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param);
static void gatt_server_time_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param);
static void gatt_server_music_playback_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param);

/*
    Creating modular functions to implememnt certain functions in order to make the code more readable
 */

static void handle_client_characteristic_configuration_descriptor(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id);
void send_notification_data(int profile_id);
void write_characteristic_data(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id);
void handle_read_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id);
void handle_create_service_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id,bool requires_notifications);
void handle_add_characteristic_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id,bool requires_notification);
void handle_add_characteristic_descriptor_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id);

/*
    Creating functions to handle the notifications of the profiles
*/

bool is_notification_enabled(uint16_t cccd_status); // Check if the notifications are enabled
void enable_notifications(uint16_t* cccd_status); // Enable the notifications
void disable_notifications(uint16_t* cccd_status); // Disable the notifications
bool has_data_changed(const uint8_t* new_data,const uint8_t* old_data,uint16_t length); // Check if the data has changed
void notify_task(void *param); // Notify the client of the data change
void init_semaphores(uint8_t num_profiles); // Initialize the semaphores for the profiles

/*
    Declaring the Umbrella GATT & GAP Event Handler
*/

static void server_gap_profile_handler(esp_gap_ble_cb_event_t event,esp_ble_gap_cb_param_t *param);
static void server_gatt_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param);

/*
    Power Management Functions
*/

void power_management_task(void *param); // Power Management Task
void start_power_management_task(); // Start the power management task
void initialize_sleep_configuration(); // Initialize the sleep configuration

/*
    Functions To Initialize the server and its profiles
*/

void initialize_server(); // Initialize the server
uint8_t* create_profile_storage(uint8_t max_length) ; // Create the storage for the profile
profile_t* create_server_profile_table(uint8_t number_of_profiles); // Create the server profile table
profile_t* create_profile(uint8_t profile_id,esp_gatts_cb_t profile_event_handler,uint8_t* storage,uint8_t max_length,uint8_t* notification_queue_buffer); // Create a profile
void free_server_profile_table(profile_t* server_table,uint8_t number_of_profiles); // Free the server profile table

/*
    Testing Functions
*/

void test_music_metadata_notification(); // Test the music metadata notification
void music_metadata_notification_task(void *param); // Music Metadata Notification Task
void start_music_notification_task(); // Start the music notification task

/* 
    Creating the application profile tables for the GATT Server for easy retreival of the profiles
    Table is made up of an array of application profiles:
        1. Server can have multiple profiles
        2. Easy retreival and search for profiles
        3. It is static to make it allocated to the stack and keep the scope global
*/

// static profile_t gatt_server_application_profile_table[NUM_PROFILES] = {
//     {
//         .profile_interface = ESP_GATT_IF_NONE,
//         .application_id = MUSIC_PROFILE_ID,
//         .profile_event_handler = gatt_server_music_profile_handler,
//         .attribute_value = {
//             .attr_len = sizeof(music_profile_characteristic_storage),
//             .attr_max_len = MUSIC_PROFILE_CHAR_LEN,
//             .attr_value = music_profile_characteristic_storage,
//         },
//         .local_storage = music_profile_characteristic_storage,
//         .local_storage_limit = MUSIC_PROFILE_CHAR_LEN,
//         .local_storage_len = 0,
//         .notification_queue_buffer = music_notification_queue_buffer,
//         .notification_queue_len = 0,
//         .last_notification_time = 0,
//         .cccd_status = 0x0000,
//     },
//     {
//         .profile_interface = ESP_GATT_IF_NONE,
//         .application_id = TODO_PROFILE_ID,
//         .profile_event_handler = gatt_server_todo_profile_handler,
//         .attribute_value = {
//             .attr_len = sizeof(todo_profile_characteristic_storage),
//             .attr_max_len = TODO_PROFILE_CHAR_LEN,
//             .attr_value = todo_profile_characteristic_storage,
//         },
//         .local_storage = todo_profile_characteristic_storage,
//         .local_storage_limit = TODO_PROFILE_CHAR_LEN,
//         .local_storage_len = 0,
//         .notification_queue_buffer = todo_notification_queue_buffer,
//         .notification_queue_len = 0,
//         .last_notification_time = 0,
//         .cccd_status = 0x0000,
//     },
//     {
//         .profile_interface = ESP_GATT_IF_NONE,
//         .application_id = TIME_PROFILE_ID,
//         .profile_event_handler = gatt_server_time_profile_handler,
//         .attribute_value = {
//             .attr_len = sizeof(time_profile_characteristic_storage),
//             .attr_max_len = TIME_PROFILE_CHAR_LEN,
//             .attr_value = time_profile_characteristic_storage,
//         },
//         .local_storage = time_profile_characteristic_storage,
//         .local_storage_limit = TIME_PROFILE_CHAR_LEN,
//         .local_storage_len = 0,
//         .notification_queue_buffer = time_notification_queue_buffer,
//         .notification_queue_len = 0,
//         .last_notification_time = 0,
//         .cccd_status = 0x0000,

//     }
// };

profile_t* gatt_server_application_profile_table;

//TODO: Add Checks in reading from storage before writing to it
//TODO: Add Checks for the notification CCCD reading operation
//TODO: Test Notifications (Completed)
//TODO: Implement Indication Extra Feature Later


esp_ble_adv_data_t gap_server_adv_data = {
  .set_scan_rsp = false,
  .include_name = true,
  .include_txpower = false,
  .min_interval = 0x0006,
  .max_interval = 0x0010,
  .appearance = 0x00,
  .manufacturer_len = 0,
  .p_manufacturer_data = NULL,
  .service_data_len = 0,
  .p_service_data = NULL,
  .service_uuid_len = sizeof(profile_service_uuids),
  .p_service_uuid = profile_service_uuids,
  .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT), // General Discoverable Mode & BLE Mode Only

};

esp_ble_adv_params_t gap_server_adv_params = {
  .adv_int_min = 0x20,
  .adv_int_max = 0x40,
  .adv_type = ADV_TYPE_IND,
  .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
  .peer_addr = {0},
  .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
  .channel_map = ADV_CHNL_ALL,
  .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

esp_ble_conn_update_params_t conn_params = {
    .latency = 4,    // Skipping 4 intervals
    .max_int = 0x50,
    .min_int = 0x30,
    .timeout = 500,  // 5 seconds
};



esp_err_t err;

void app_main(void)
{
//     /*
//         Initialize the NVS Flash
//     */
//     err = nvs_flash_init();
//     if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Initializing NVS Flash: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GATT_INIT,"NVS Flash Initialized");

//     /*
//         Release the Classic Bluetooth Memory
//     */

//    err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT); // The memory need to be released for the classic BT stack so that only the BLE stack is kept and initialized
//    if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Releasing Classic BT Memory: %s",esp_err_to_name(err));
//         return;
//    }
//     ESP_LOGI(GATT_INIT,"Classic BT Memory Released");

//     /*
//         Initialize the Bluetooth Controller
//     */

//     esp_bt_controller_config_t ble_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
//     err = esp_bt_controller_init(&ble_cfg);
//     if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Initializing Bluetooth Controller: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GATT_INIT,"Bluetooth Controller Initialized");

//     /*
//         Enable the Bluetooth Controller
//     */

//     err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
//     if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Enabling Bluetooth Controller: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GATT_INIT,"Bluetooth Controller Enabled");

//     /*
//         Initialize the Bluedroid Stack
//     */

//     err = esp_bluedroid_init();
//     if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Initializing Bluedroid Stack: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GATT_INIT,"Bluedroid Stack Initialized");

//     /*
//         Enable the Bluedroid Stack
//     */

//     err = esp_bluedroid_enable();
//     if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Enabling Bluedroid Stack: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GATT_INIT,"Bluedroid Stack Enabled");

//     /*
//         Register GATT & GAP Callback
//     */

//     err = esp_ble_gatts_register_callback(server_gatt_profile_handler);
//     if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Registering GATT Callback: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GATT_INIT,"GATT Callback Registered");

//     err = esp_ble_gap_register_callback(server_gap_profile_handler);
//     if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Registering GAP Callback: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GATT_INIT,"GAP Callback Registered");

//     /*
//         Register the GATT Server Application Profiles
//     */

//     err = esp_ble_gatts_app_register(MUSIC_PROFILE_ID); // Triggers the registration event
//     if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Registering Music Profile: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GATT_INIT,"Music Profile Registered");

//     err = esp_ble_gatts_app_register(TODO_PROFILE_ID);
//     if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Registering Todo Profile: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GATT_INIT,"Todo Profile Registered");

//     err = esp_ble_gatts_app_register(TIME_PROFILE_ID); 
//     if(err != ESP_OK){
//         ESP_LOGE(GATT_INIT,"Error Registering Time Profile: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GATT_INIT,"Time Profile Registered");

//     /*
//         Set the GAP Server Advertisement Data
//     */

//     err = esp_ble_gap_set_device_name("ESP32_xsSERVER");
//     if(err != ESP_OK){
//         ESP_LOGE(GAP_INIT,"Error Setting Device Name: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GAP_INIT,"Device Name Set");

//     err = esp_ble_gap_config_adv_data(&gap_server_adv_data);
//     if(err != ESP_OK){
//         ESP_LOGE(GAP_INIT,"Error Configuring Advertisement Data: %s",esp_err_to_name(err));
//         return;
//     }
//     ESP_LOGI(GAP_INIT,"Advertisement Data Configured");

//     err = esp_ble_gap_start_advertising(&gap_server_adv_params);
//     if(err != ESP_OK){
//         ESP_LOGE(GAP_INIT,"Error Starting Advertisement: %s",esp_err_to_name(err));
//         return;
//     }

//     ESP_LOGI(GAP_INIT,"Advertisement Parameters Configured");

//     err = esp_ble_gatt_set_local_mtu(517);
//     if(err != ESP_OK){
//         ESP_LOGE(GAP_INIT,"Error Setting Local MTU: %s",esp_err_to_name(err));
//         return;
//     }

//     ESP_LOGI(GAP_INIT,"Local MTU Set");

//     init_semaphores(NUM_PROFILES);
//     start_power_management_task();

//     server_start_timer = esp_timer_get_time() / 1000; // Get the current time in milliseconds

//     initialize_sleep_configuration();

    initialize_server();

    #ifdef TESTING
        // Simulate metadata notification after initialization
        vTaskDelay(pdMS_TO_TICKS(30000)); // Delay to ensure initialization is complete
        test_music_metadata_notification();
    #endif

    return;
}

/*
    Server GATT Profile Event Handler Implementation
*/

static void gatt_server_music_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param){
    switch(event){
        case ESP_GATTS_REG_EVT:
            // This event is done when the GATT Server is created and profiles need to be registered

            break;
        case ESP_GATTS_CREATE_EVT:
            // This event is done service is created
            ESP_LOGI(MUSIC_PROFILE_CB,"GATT Server Create Event status: %d",param->create.status);
            handle_create_service_request(gatt_interface,param,MUSIC_PROFILE_ID,true);
            break;
        case ESP_GATTS_START_EVT:
            // The service has started so now the characteristic for each of the profiles must be created
            esp_gatt_status_t start_status = param->start.status;
            if(start_status == ESP_OK){
                ESP_LOGI(MUSIC_PROFILE_CB,"Music Service Started Successfully with status %d",param->start.status);
            }else{
                ESP_LOGE(MUSIC_PROFILE_CB,"Music Service Failed to Start with status %d",param->start.status);
            }
            break;
        case ESP_GATTS_ADD_CHAR_EVT:
            // This event is done when a characteristic is added
            handle_add_characteristic_request(gatt_interface,param,MUSIC_PROFILE_ID,true);
            break;

        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            // This event is done when a characteristic descriptor is added
            handle_add_characteristic_descriptor_request(gatt_interface,param,MUSIC_PROFILE_ID);
            break;
        break;
        case ESP_GATTS_READ_EVT:
            // This event is when the client wants to execute a read operation
            handle_read_request(gatt_interface,param,MUSIC_PROFILE_ID);
            break;
        case ESP_GATTS_WRITE_EVT:
            ESP_LOGI(MUSIC_PROFILE_CB, "GATT Server Write Event handle: %d", param->write.handle);

            // // Check CCCD value
            if(param->write.handle == gatt_server_application_profile_table[MUSIC_PROFILE_ID].characteristic_descriptor_handle){
                ESP_LOGI(MUSIC_PROFILE_CB, "Write Value (Length: %d):", param->write.len);
                for (int i = 0; i < param->write.len; i++) {
                    ESP_LOGI(MUSIC_PROFILE_CB, "Byte[%d]: 0x%02X", i, param->write.value[i]);
                }
                // CCCD value has been written
                if(param->write.len == 2){
                    handle_client_characteristic_configuration_descriptor(gatt_interface,param,MUSIC_PROFILE_ID);
                }else{
                    ESP_LOGE(MUSIC_PROFILE_CB,"Invalid CCCD Value Length");
                }
                
            }else{
                // Check if the write is under characteristic length
                ESP_LOGW(MUSIC_PROFILE_CB,"Write Event - Storage Value: %s, Storage Length: %d",gatt_server_application_profile_table[MUSIC_PROFILE_ID].local_storage,gatt_server_application_profile_table[MUSIC_PROFILE_ID].local_storage_len);
                write_characteristic_data(gatt_interface,param,MUSIC_PROFILE_ID);
            }
            break;
        case ESP_GATTS_SET_ATTR_VAL_EVT:
            // This event is done when the attribute value is set
            ESP_LOGI(MUSIC_PROFILE_CB,"GATT Server Set Attribute Value Event status: %d",param->set_attr_val.status);
            // Get the attribute value
            uint16_t attribute_length = 0;
            uint8_t* attribute_value = NULL;

            err = esp_ble_gatts_get_attr_value(param->set_attr_val.attr_handle,&attribute_length,&attribute_value);
            if(err != ESP_OK){
                ESP_LOGE(MUSIC_PROFILE_CB,"Error Getting Attribute Value");
            }else{
                ESP_LOGI(MUSIC_PROFILE_CB,"Attribute Length: %d",attribute_length);
                ESP_LOGI(MUSIC_PROFILE_CB,"Attribute Value: %s",attribute_value);
            }
            break;
        case ESP_GATTS_EXEC_WRITE_EVT:
            ESP_LOGI(MUSIC_PROFILE_CB,"GATT Server Execute Write Event handle: %d",param->exec_write.conn_id);
            ESP_LOGI(MUSIC_PROFILE_CB, "Write Event - Handle: %d, Offset: %d, Length: %d",param->write.handle, param->write.offset, param->write.len);

            // TODO: Implement Buffering & Long writes and change the MTU value to be smaller.
            if(param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC){
                ESP_LOGI(MUSIC_PROFILE_CB,"Execute Write Flag: Execute Write");
            }
            break;
        case ESP_GATTS_MTU_EVT:
            // This event is when the MTU is set
            ESP_LOGI(MUSIC_PROFILE_CB,"GATT Server MTU Event MTU: %d",param->mtu.mtu);
            break;
        case ESP_GATTS_CONNECT_EVT:
            // This evnet is when the client connects to the server
            ESP_LOGI(MUSIC_PROFILE_CB,"GATT Server Connect Event conn_id: %d",param->connect.conn_id);
            // Need to implement the connection parameters based on the ESP-IDF documentation
            // According to the the documentation the connection parameters must be initialized only on one profile
            gatt_server_application_profile_table[MUSIC_PROFILE_ID].connection_id = param->connect.conn_id; // Saving the connection id for the profile
            gatt_server_application_profile_table[MUSIC_PROFILE_ID].cccd_status = 0x0000; //Initializing it so that the notifications reset.
            client_connected = true;
            client_disconnet_timer = 0;
            // Update the connection parameters

                // err = esp_ble_gap_update_conn_params(&conn_params); // Update the connection parameters

                // #ifdef DEBUG
                //     if(err != ESP_OK){
                //         ESP_LOGE("Power Management","Error Updating Connection Parameters: %s",esp_err_to_name(err));
                //     }else{
                //         ESP_LOGI("Power Management","Connection Parameters Updated");
                //     }
                // #endif
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            // This event is when the client disconnects from the server
            ESP_LOGI(MUSIC_PROFILE_CB,"GATT Server Disconnect Event conn_id: %d",param->disconnect.conn_id);
            
            // Reset the attributes for the profile
            gatt_server_application_profile_table[MUSIC_PROFILE_ID].connection_id = 0;
            gatt_server_application_profile_table[MUSIC_PROFILE_ID].cccd_status = 0x0000;
            gatt_server_application_profile_table[MUSIC_PROFILE_ID].characteristic_handle = 0;
            gatt_server_application_profile_table[MUSIC_PROFILE_ID].characteristic_descriptor_handle = 0;

            esp_ble_gap_start_advertising(&gap_server_adv_params); // Restart the advertising
            client_connected = false;
            client_disconnet_timer = esp_timer_get_time() / 1000; // Get the current time in milliseconds

            break;
        case ESP_GATTS_RESPONSE_EVT:
            // This event is when the server sends a response to the client
            ESP_LOGI(MUSIC_PROFILE_CB,"GATT Server Response Event conn_id: %d",param->rsp.status);
            if(param->rsp.status == ESP_GATT_OK){
                ESP_LOGI(MUSIC_PROFILE_CB,"GATT Server Response Event Success");
            }else{
                ESP_LOGE(MUSIC_PROFILE_CB,"GATT Server Response Event Failed");
            }
            break;
        case ESP_GATTS_CONF_EVT:
            // This event is when the server sends a confirmation to the client
            ESP_LOGI(MUSIC_PROFILE_CB,"GATT Server Confirmation Event conn_id: %d",param->conf.status);
            if(param->conf.status == ESP_GATT_OK){
                ESP_LOGI(MUSIC_PROFILE_CB,"GATT Server Confirmation Event Success");
            }else{
                ESP_LOGE(MUSIC_PROFILE_CB,"GATT Server Confirmation Event Failed");
            }
            break;
        default:
            ESP_LOGE(MUSIC_PROFILE_CB,"Unknown GATT Server Event: %d",event);
            break;
    }
}

static void gatt_server_todo_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param){
     switch(event){
        case ESP_GATTS_REG_EVT:
            // This event is done when the GATT Server is created and profiles need to be registered
            break;
        case ESP_GATTS_CREATE_EVT:
            // This event is done service is created
            ESP_LOGI(TODO_PROFILE_CB,"GATT Server Create Event status: %d",param->create.status);
            handle_create_service_request(gatt_interface,param,TODO_PROFILE_ID,false);
            break;
        case ESP_GATTS_START_EVT:
            // The service has started so now the characteristic for each of the profiles must be created
            esp_gatt_status_t start_status = param->start.status;
            if(start_status == ESP_OK){
                ESP_LOGI(TODO_PROFILE_CB,"Todo Service Started Successfully with status %d",param->start.status);
            }else{
               ESP_LOGE(TODO_PROFILE_CB,"Todo Service Failed to Start with status %d",param->start.status);
            }
            break;
        case ESP_GATTS_ADD_CHAR_EVT:
            // This event is done when a characteristic is added
            ESP_LOGI(TODO_PROFILE_CB,"GATT Server Add Characteristic Event status: %d",param->add_char.status);

            handle_add_characteristic_request(gatt_interface,param,TODO_PROFILE_ID,false);
            break;

        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            // This event is done when a characteristic descriptor is added
            handle_add_characteristic_descriptor_request(gatt_interface,param,TODO_PROFILE_ID);
            break;
        case ESP_GATTS_READ_EVT:
            // This event is when the client wants to execute a read operation
            ESP_LOGI(TODO_PROFILE_CB,"GATT Server Read Event handle: %d",param->read.handle);
            handle_read_request(gatt_interface,param,TODO_PROFILE_ID);
            break;
        case ESP_GATTS_WRITE_EVT:
            // This event is when the client wants to execute a write operation
            ESP_LOGI(TODO_PROFILE_CB,"GATT Server Write Event handle: %d",param->write.handle);
            // // Check CCCD value
            if(param->write.handle == gatt_server_application_profile_table[TODO_PROFILE_ID].characteristic_descriptor_handle){
                ESP_LOGI(MUSIC_PROFILE_CB, "Write Value (Length: %d):", param->write.len);
                for (int i = 0; i < param->write.len; i++) {
                    // ESP_LOGI(TODO_PROFILE_ID, "Byte[%d]: 0x%02X", i, param->write.value[i]);
                }
                // CCCD value has been written
                if(param->write.len == 2){
                    handle_client_characteristic_configuration_descriptor(gatt_interface,param,TODO_PROFILE_ID);
                }else{
                    ESP_LOGE(MUSIC_PROFILE_CB,"Invalid CCCD Value Length");
                }
                
            }else{
                // Check if the write is under characteristic length
                write_characteristic_data(gatt_interface,param,TODO_PROFILE_ID);
            }
            break;
        case ESP_GATTS_EXEC_WRITE_EVT:
            ESP_LOGI(TODO_PROFILE_CB,"GATT Server Execute Write Event handle: %d",param->exec_write.conn_id);
            break;
        case ESP_GATTS_MTU_EVT:
            // This event is when the MTU is set
            ESP_LOGI(TODO_PROFILE_CB,"GATT Server MTU Event MTU: %d",param->mtu.mtu);
            break;
        case ESP_GATTS_CONNECT_EVT:
            // This evnet is when the client connects to the server
            ESP_LOGI(TODO_PROFILE_CB,"GATT Server Connect Event conn_id: %d",param->connect.conn_id);
            // Need to implement the connection parameters based on the ESP-IDF documentation
            esp_ble_conn_update_params_t client_connection_parameters = {
                .latency = 0,
                .max_int = 0x30,
                .min_int = 0x10,
                .timeout = 500,
            };
            memcpy(client_connection_parameters.bda,param->connect.remote_bda,sizeof(esp_bd_addr_t)); // Copying the client address to the connection parameters
            gatt_server_application_profile_table[TODO_PROFILE_ID].connection_id = param->connect.conn_id; // Saving the connection id for the profile
            err = esp_ble_gap_update_conn_params(&client_connection_parameters);
            if(err != ESP_OK){
                ESP_LOGE(TODO_PROFILE_CB,"Error Updating Connection Parameters: %s",esp_err_to_name(err));
            }else{
                ESP_LOGI(TODO_PROFILE_CB,"Connection Parameters Updated");
            }
            break;
        case ESP_GATTS_RESPONSE_EVT:
            ESP_LOGI(TODO_PROFILE_CB,"GATT Server Response Event conn_id: %d",param->rsp.status);
            if(param->rsp.status == ESP_GATT_OK){
                ESP_LOGI(TODO_PROFILE_CB,"GATT Server Response Event Success");
            }else{
                ESP_LOGE(TODO_PROFILE_CB,"GATT Server Response Event Failed");
            }
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            // This event is when the client disconnects from the server
            ESP_LOGI(TODO_PROFILE_CB,"GATT Server Disconnect Event conn_id: %d",param->disconnect.conn_id);
            
             // Reset the attributes for the profile
            gatt_server_application_profile_table[TODO_PROFILE_ID].connection_id = 0;
            gatt_server_application_profile_table[TODO_PROFILE_ID].cccd_status = 0x0000;
            gatt_server_application_profile_table[TODO_PROFILE_ID].characteristic_handle = 0;
            gatt_server_application_profile_table[TODO_PROFILE_ID].characteristic_descriptor_handle = 0;

            esp_ble_gap_start_advertising(&gap_server_adv_params); // Restart the advertising
            break;
        case ESP_GATTS_SET_ATTR_VAL_EVT:
            ESP_LOGI(TODO_PROFILE_CB,"GATT Server Set Attribute Value Event status: %d",param->set_attr_val.status);
            break;
        default:
            ESP_LOGE(TODO_PROFILE_CB,"Unknown GATT Server Event: %d",event);
            break;
    }

}

static void gatt_server_time_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param){
     switch(event){
        case ESP_GATTS_REG_EVT:
            // This event is done when the GATT Server is created and profiles need to be registered
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server Registration Event status: %d",param->reg.status);

            break;
        case ESP_GATTS_CREATE_EVT:
            // This event is done service is created
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server Create Event status: %d",param->create.status);
            handle_create_service_request(gatt_interface,param,TIME_PROFILE_ID,false);
            break;
        case ESP_GATTS_START_EVT:
            // The service has started so now the characteristic for each of the profiles must be created
            esp_gatt_status_t start_status = param->start.status;
            if(start_status == ESP_OK){
               ESP_LOGI(TIME_PROFILE_CB,"Time Service Started Successfully with status %d",param->start.status);
            }else{
                ESP_LOGE(TIME_PROFILE_CB,"Time Service Failed to Start with status %d",param->start.status);
            }
            break;
        case ESP_GATTS_ADD_CHAR_EVT:
            // This event is done when a characteristic is added
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server Add Characteristic Event status: %d",param->add_char.status);
            handle_add_characteristic_request(gatt_interface,param,TIME_PROFILE_ID,false);
            break;
        case ESP_GATTS_READ_EVT:
            // This event is when the client wants to execute a read operation
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server Read Event handle: %d",param->read.handle);
            handle_read_request(gatt_interface,param,TIME_PROFILE_ID);
            break;
        case ESP_GATTS_WRITE_EVT:
            // This event is when the client wants to execute a write operation
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server Write Event handle: %d",param->write.handle);
            write_characteristic_data(gatt_interface,param,TIME_PROFILE_ID);
            break;
        case ESP_GATTS_EXEC_WRITE_EVT:
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server Execute Write Event handle: %d",param->exec_write.conn_id);
            break;
        case ESP_GATTS_MTU_EVT:
            // This event is when the MTU is set
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server MTU Event MTU: %d",param->mtu.mtu);
            break;
        case ESP_GATTS_CONNECT_EVT:
            // This evnet is when the client connects to the server
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server Connect Event conn_id: %d",param->connect.conn_id);
            // Need to implement the connection parameters based on the ESP-IDF documentation
            esp_ble_conn_update_params_t client_connection_parameters = {
                .latency = 0,
                .max_int = 0x30,
                .min_int = 0x10,
                .timeout = 500,
            };
            memcpy(client_connection_parameters.bda,param->connect.remote_bda,sizeof(esp_bd_addr_t)); // Copying the client address to the connection parameters
            gatt_server_application_profile_table[TIME_PROFILE_ID].connection_id = param->connect.conn_id; // Saving the connection id for the profile
            err = esp_ble_gap_update_conn_params(&client_connection_parameters);
            if(err != ESP_OK){
                ESP_LOGE(TIME_PROFILE_CB,"Error Updating Connection Parameters: %s",esp_err_to_name(err));
            }else{
                ESP_LOGI(TIME_PROFILE_CB,"Connection Parameters Updated");
            }
            break;
        case ESP_GATTS_RESPONSE_EVT:
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server Response Event conn_id: %d",param->rsp.status);
            if(param->rsp.status == ESP_GATT_OK){
                ESP_LOGI(TIME_PROFILE_CB,"GATT Server Response Event Success");
            }else{
                ESP_LOGE(TIME_PROFILE_CB,"GATT Server Response Event Failed");
            }
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            // This event is when the client disconnects from the server
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server Disconnect Event conn_id: %d",param->disconnect.conn_id);

             // Reset the attributes for the profile
            gatt_server_application_profile_table[TIME_PROFILE_ID].connection_id = 0;
            gatt_server_application_profile_table[TIME_PROFILE_ID].cccd_status = 0x0000;
            gatt_server_application_profile_table[TIME_PROFILE_ID].characteristic_handle = 0;
            gatt_server_application_profile_table[TIME_PROFILE_ID].characteristic_descriptor_handle = 0;

            esp_ble_gap_start_advertising(&gap_server_adv_params); // Restart the advertising
            break;
        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            // This event is done when a characteristic descriptor is added
            handle_add_characteristic_descriptor_request(gatt_interface,param,TIME_PROFILE_ID);
            break;

        case ESP_GATTS_SET_ATTR_VAL_EVT:
            ESP_LOGI(TIME_PROFILE_CB,"GATT Server Set Attribute Value Event status: %d",param->set_attr_val.status);
            break;
        default:
            ESP_LOGE(TIME_PROFILE_CB,"Unknown GATT Server Event: %d",event);
            break;
    }

}

static void gatt_server_music_playback_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param){
    switch(event){
        case ESP_GATTS_REG_EVT:
            // This event is done when the GATT Server is created and profiles need to be registered

            break;
        case ESP_GATTS_CREATE_EVT:
            // This event is done service is created
            ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Create Event status: %d",param->create.status);
            handle_create_service_request(gatt_interface,param,MUSIC_PLAYBACK_PROFILE_ID,true);
            break;
        case ESP_GATTS_START_EVT:
            // The service has started so now the characteristic for each of the profiles must be created
            esp_gatt_status_t start_status = param->start.status;
            if(start_status == ESP_OK){
                ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"Music Service Started Successfully with status %d",param->start.status);
            }else{
                ESP_LOGE(MUSIC_PLAYBACK_PROFILE_CB,"Music Service Failed to Start with status %d",param->start.status);
            }
            break;
        case ESP_GATTS_ADD_CHAR_EVT:
            // This event is done when a characteristic is added
            handle_add_characteristic_request(gatt_interface,param,MUSIC_PLAYBACK_PROFILE_ID,true);
            break;

        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            // This event is done when a characteristic descriptor is added
            handle_add_characteristic_descriptor_request(gatt_interface,param,MUSIC_PLAYBACK_PROFILE_ID);
            break;
        break;
        case ESP_GATTS_READ_EVT:
            // This event is when the client wants to execute a read operation
            handle_read_request(gatt_interface,param,MUSIC_PLAYBACK_PROFILE_ID);
            break;
        case ESP_GATTS_WRITE_EVT:
            ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB, "GATT Server Write Event handle: %d", param->write.handle);

            // // Check CCCD value
            if(param->write.handle == gatt_server_application_profile_table[MUSIC_PLAYBACK_PROFILE_ID].characteristic_descriptor_handle){
                ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB, "Write Value (Length: %d):", param->write.len);
                for (int i = 0; i < param->write.len; i++) {
                    ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB, "Byte[%d]: 0x%02X", i, param->write.value[i]);
                }
                // CCCD value has been written
                if(param->write.len == 2){
                    handle_client_characteristic_configuration_descriptor(gatt_interface,param,MUSIC_PLAYBACK_PROFILE_ID);
                }else{
                    ESP_LOGE(MUSIC_PLAYBACK_PROFILE_CB,"Invalid CCCD Value Length");
                }
                
            }else{
                // Check if the write is under characteristic length
                ESP_LOGW(MUSIC_PLAYBACK_PROFILE_CB,"Write Event - Storage Value: %s, Storage Length: %d",gatt_server_application_profile_table[MUSIC_PLAYBACK_PROFILE_ID].local_storage,gatt_server_application_profile_table[MUSIC_PLAYBACK_PROFILE_ID].local_storage_len);
                write_characteristic_data(gatt_interface,param,MUSIC_PLAYBACK_PROFILE_ID);
            }
            break;
        case ESP_GATTS_SET_ATTR_VAL_EVT:
            // This event is done when the attribute value is set
            ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Set Attribute Value Event status: %d",param->set_attr_val.status);
            // Get the attribute value
            uint16_t attribute_length = 0;
            uint8_t* attribute_value = NULL;

            err = esp_ble_gatts_get_attr_value(param->set_attr_val.attr_handle,&attribute_length,&attribute_value);
            if(err != ESP_OK){
                ESP_LOGE(MUSIC_PLAYBACK_PROFILE_CB,"Error Getting Attribute Value");
            }else{
                ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"Attribute Length: %d",attribute_length);
                ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"Attribute Value: %s",attribute_value);
            }
            break;
        case ESP_GATTS_EXEC_WRITE_EVT:
            ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Execute Write Event handle: %d",param->exec_write.conn_id);
            ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB, "Write Event - Handle: %d, Offset: %d, Length: %d",param->write.handle, param->write.offset, param->write.len);

            // TODO: Implement Buffering & Long writes and change the MTU value to be smaller.
            if(param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC){
                ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"Execute Write Flag: Execute Write");
            }
            break;
        case ESP_GATTS_MTU_EVT:
            // This event is when the MTU is set
            ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server MTU Event MTU: %d",param->mtu.mtu);
            break;
        case ESP_GATTS_CONNECT_EVT:
            // This evnet is when the client connects to the server
            ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Connect Event conn_id: %d",param->connect.conn_id);
            // Need to implement the connection parameters based on the ESP-IDF documentation
            // According to the the documentation the connection parameters must be initialized only on one profile
            gatt_server_application_profile_table[MUSIC_PLAYBACK_PROFILE_ID].connection_id = param->connect.conn_id; // Saving the connection id for the profile
            gatt_server_application_profile_table[MUSIC_PLAYBACK_PROFILE_ID].cccd_status = 0x0000; //Initializing it so that the notifications reset.
            client_connected = true;
            client_disconnet_timer = 0;
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            // This event is when the client disconnects from the server
            ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Disconnect Event conn_id: %d",param->disconnect.conn_id);
            
            // Reset the attributes for the profile
            gatt_server_application_profile_table[MUSIC_PLAYBACK_PROFILE_ID].connection_id = 0;
            gatt_server_application_profile_table[MUSIC_PLAYBACK_PROFILE_ID].cccd_status = 0x0000;
            gatt_server_application_profile_table[MUSIC_PLAYBACK_PROFILE_ID].characteristic_handle = 0;
            gatt_server_application_profile_table[MUSIC_PLAYBACK_PROFILE_ID].characteristic_descriptor_handle = 0;

            esp_ble_gap_start_advertising(&gap_server_adv_params); // Restart the advertising
            client_connected = false;
            client_disconnet_timer = esp_timer_get_time() / 1000; // Get the current time in milliseconds

            break;
        case ESP_GATTS_RESPONSE_EVT:
            // This event is when the server sends a response to the client
            ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Response Event conn_id: %d",param->rsp.status);
            if(param->rsp.status == ESP_GATT_OK){
                ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Response Event Success");
            }else{
                ESP_LOGE(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Response Event Failed");
            }
            break;
        case ESP_GATTS_CONF_EVT:
            // This event is when the server sends a confirmation to the client
            ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Confirmation Event conn_id: %d",param->conf.status);
            if(param->conf.status == ESP_GATT_OK){
                ESP_LOGI(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Confirmation Event Success");
            }else{
                ESP_LOGE(MUSIC_PLAYBACK_PROFILE_CB,"GATT Server Confirmation Event Failed");
            }
            break;
        default:
            ESP_LOGE(MUSIC_PLAYBACK_PROFILE_CB,"Unknown GATT Server Event: %d",event);
            break;
    }
}
/*
    Server GATT & GAP Profile Handler Implementation
 */

static void server_gap_profile_handler(esp_gap_ble_cb_event_t event,esp_ble_gap_cb_param_t *param){
    
}

static void server_gatt_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param){
    if(event == ESP_GATTS_REG_EVT){
                    // This event is done when the GATT Server is created and profiles need to be registered
            ESP_LOGI(GATT_CALLBACK,"GATT Server Registration Event status: %d",param->reg.status);
            // need to make sure that the status of the registration is successful
            esp_gatt_status_t reg_status =  param->reg.status;
            if(reg_status == ESP_OK){
                ESP_LOGI(GATT_CALLBACK,"GATT Server Registration Successful");
                // Now we need to get the application profile and set the interface
                ESP_LOGI(GATT_CALLBACK,"Setting registration for profile: %d",param->reg.app_id);

                // Need to create the service for the profile
                esp_gatt_srvc_id_t service_id = {
                    // Need to give it an id and show if it is primary service for that profile or not
                    .is_primary = true,
                    .id = {
                        .uuid = {
                            .len = ESP_UUID_LEN_128, // 128/8 = 16 bytes
                            .uuid = {
                                .uuid16 = service_uuids[param->reg.app_id],
                            }
                        },
                        .inst_id = 0,
                    },
                };

                // Set the specific profile interface
                gatt_server_application_profile_table[param->reg.app_id].profile_interface = gatt_interface;
                ESP_LOGI(GATT_CALLBACK,"Assigned GATT Interface for profile: %d",param->reg.app_id);
                // Create the service for the profile
                err = esp_ble_gatts_create_service(gatt_interface,&service_id,10);
                if(err != ESP_OK){
                    ESP_LOGE(GATT_CALLBACK,"Error Creating Service for profile: %d",param->reg.app_id);
                    return;
                }
                ESP_LOGI(GATT_CALLBACK,"Profile Create Service for profile: %d",param->reg.app_id);
                // Set the specific profile interface
                gatt_server_application_profile_table[param->reg.app_id].profile_interface = gatt_interface;

                // Set the service id for the profile
                gatt_server_application_profile_table[param->reg.app_id].service_id = service_id.id.uuid.uuid.uuid16;

                ESP_LOGI(GATT_CALLBACK,"Created GATT Service Sucessfully for profile: %d",param->reg.app_id);
            }else{
                ESP_LOGE(GATT_CALLBACK,"GATT Server Registration Failed for profile: %d",param->reg.app_id);
            }
    }else{
        // If it is not registartion event then it is a profile event
        // Need to get the profile interface and call the profile event handler
        ESP_LOGI(GATT_CALLBACK,"Calling Profile Event Handler");
        for(int profile_no = 0; profile_no < NUM_PROFILES; profile_no++){
            if(gatt_server_application_profile_table[profile_no].profile_interface == gatt_interface|| gatt_interface == ESP_GATT_IF_NONE){
                // Call the profile event handler
                ESP_LOGI(GATT_CALLBACK,"Calling Profile Event Handler for profile: %d",profile_no);
                gatt_server_application_profile_table[profile_no].profile_event_handler(event,gatt_interface,param);
            }
        }
        
    }

}

static void handle_client_characteristic_configuration_descriptor(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id){
    // Implement the CCCD handling logic
    
    uint16_t cccd_write_value = param->write.value[1] << 8 | param->write.value[0];// Take the first byte and shift it 8 bits to the left and then OR it with the second byte
    // Now checking for the states of the CCCD
    if(cccd_write_value == 0x0001){
        ESP_LOGI(GATT_CALLBACK,"Notification Enabled");
        // Send a response to the client stating that the CCCD value has been set
        esp_gatt_rsp_t rsp = {
            .attr_value = {
                .handle = param->write.handle,
                .len = param->write.len,
            }
        };

        memcpy(rsp.attr_value.value,param->write.value,param->write.len);

        esp_err_t err = esp_ble_gatts_send_response(gatt_interface,param->write.conn_id,param->write.trans_id,ESP_GATT_OK,&rsp);
        if (err != ESP_OK){
           ESP_LOGE(GATT_CALLBACK,"Error Sending Response");
           ESP_LOGE(GATT_CALLBACK,"Error Code: %s",esp_err_to_name(err));
        }

        // Set the value of the CCCD state in the profile table
        gatt_server_application_profile_table[profile_id].cccd_status = 0x0001;
    }else if(cccd_write_value == 0x0002){
        ESP_LOGI(GATT_CALLBACK,"Indication Enabled");
        // Send a response to the client stating that the CCCD value has been set
        esp_gatt_rsp_t rsp = {
            .attr_value = {
                .handle = param->write.handle,
                .len = param->write.len,
            }
        };

        memcpy(rsp.attr_value.value,param->write.value,param->write.len);

        esp_err_t err = esp_ble_gatts_send_response(gatt_interface,param->write.conn_id,param->write.trans_id,ESP_GATT_OK,&rsp);
        if (err != ESP_OK){
           ESP_LOGE(GATT_CALLBACK,"Error Sending Response");
           ESP_LOGE(GATT_CALLBACK,"Error Code: %s",esp_err_to_name(err));
        }
        // Set the value of the CCCD state in the profile table
        gatt_server_application_profile_table[profile_id].cccd_status = 0x0002;
    }else if(cccd_write_value == 0x0000){
        ESP_LOGI(GATT_CALLBACK,"Notification/Indication Disabled");
        // Send a response to the client stating that the CCCD value has been set
        esp_gatt_rsp_t rsp = {
            .attr_value = {
                .handle = param->write.handle,
                .len = param->write.len,
            }
        };

        memcpy(rsp.attr_value.value,param->write.value,param->write.len);

        esp_err_t err = esp_ble_gatts_send_response(gatt_interface,param->write.conn_id,param->write.trans_id,ESP_GATT_OK,&rsp);
        if (err != ESP_OK){
           ESP_LOGE(GATT_CALLBACK,"Error Sending Response");
           ESP_LOGE(GATT_CALLBACK,"Error Code: %s",esp_err_to_name(err));
        }

        // Set the value of the CCCD state in the profile table
        gatt_server_application_profile_table[profile_id].cccd_status = 0x0000;
        
    }else{
        ESP_LOGE(GATT_CALLBACK,"Unknown CCCD Value: %d",cccd_write_value);
    }
}

void send_notification_data(int profile_id){
    // Send the data to the client if notifications are enabled
    if(gatt_server_application_profile_table[profile_id].cccd_status == 0x0001){
        // Notifications are enabled
        // ESP_LOGI(GATT_CALLBACK,"Sending Notification Data");
        esp_err_t err = ESP_FAIL;
        for(int counter = 0; counter< MAX_NOTIFCATION_RETRIES; counter++){
            // Check if enough time has passed between last notification
            uint64_t current_time = esp_timer_get_time();
            ESP_LOGI(GATT_CALLBACK,"Try No: %d",counter);
            if(gatt_server_application_profile_table[profile_id].last_notification_time != 0){
                uint64_t time_difference = current_time - gatt_server_application_profile_table[profile_id].last_notification_time;
                ESP_LOGI(GATT_CALLBACK,"Time Difference: %llu",time_difference);
                ESP_LOGI(GATT_CALLBACK,"Checking Time Difference");
                if(time_difference < NOTIFICATION_INTERVAL){
                    ESP_LOGE(GATT_CALLBACK,"Not Enough Time has Passed since last notification");
                    break;
                }else{
                    // Enough time has passed
                    ESP_LOGI(GATT_CALLBACK,"Enough Time has Passed since last notification");
                    ESP_LOGI(GATT_CALLBACK,"Sending Notification Data");
                    #ifdef DEBUG
                        ESP_LOGW(GATT_CALLBACK,"DEBUG Notification Data: %s",gatt_server_application_profile_table[profile_id].notification_queue_buffer);
                        ESP_LOGW(GATT_CALLBACK,"DEBUG Notification Data Length: %d",gatt_server_application_profile_table[profile_id].notification_queue_len);
                        ESP_LOGW(GATT_CALLBACK,"DEBUG Local Storage Value: %s",gatt_server_application_profile_table[profile_id].local_storage);
                        ESP_LOGW(GATT_CALLBACK,"DEBUG Local Storage Length: %d",gatt_server_application_profile_table[profile_id].local_storage_len);
                    #endif
                    err = esp_ble_gatts_send_indicate(
                            gatt_server_application_profile_table[profile_id].profile_interface,
                            gatt_server_application_profile_table[profile_id].connection_id,
                            gatt_server_application_profile_table[profile_id].characteristic_handle,
                            gatt_server_application_profile_table[profile_id].notification_queue_len,
                            gatt_server_application_profile_table[profile_id].notification_queue_buffer,
                            false);
                    if(err != ESP_OK){
                        ESP_LOGE(GATT_CALLBACK,"Error Sending Notification Data retrying...");
                        ESP_LOGE(GATT_CALLBACK,"Error Code: %s",esp_err_to_name(err));
                        vTaskDelay(pdMS_TO_TICKS((counter+1)*50)); // Adding a delay before retrying and increasing it as per the counter
                    }else{
                        ESP_LOGI(GATT_CALLBACK,"Notification Data Sent");

                        // Write the value to the local storage
                        memset(gatt_server_application_profile_table[profile_id].local_storage,0,gatt_server_application_profile_table[profile_id].local_storage_limit); // Clear the memory
                        memcpy(gatt_server_application_profile_table[profile_id].local_storage,gatt_server_application_profile_table[profile_id].notification_queue_buffer,gatt_server_application_profile_table[profile_id].notification_queue_len); // Copy the new value to the storage
                        gatt_server_application_profile_table[profile_id].local_storage_len = gatt_server_application_profile_table[profile_id].notification_queue_len; // Update the value length

                        // Clear the notification queue
                        memset(gatt_server_application_profile_table[profile_id].notification_queue_buffer,0,gatt_server_application_profile_table[profile_id].local_storage_limit);
                        gatt_server_application_profile_table[profile_id].notification_queue_len = 0;
                        gatt_server_application_profile_table[profile_id].last_notification_time = current_time;

                        #ifdef DEBUG
                            ESP_LOGW(GATT_CALLBACK,"DEBUG Notification Data: %s",gatt_server_application_profile_table[profile_id].notification_queue_buffer);
                            ESP_LOGW(GATT_CALLBACK,"DEBUG Notification Data Length: %d",gatt_server_application_profile_table[profile_id].notification_queue_len);
                            ESP_LOGW(GATT_CALLBACK,"DEBUG Local Storage Value: %s",gatt_server_application_profile_table[profile_id].local_storage);
                            ESP_LOGW(GATT_CALLBACK,"DEBUG Local Storage Length: %d",gatt_server_application_profile_table[profile_id].local_storage_len);
                        #endif

                        break;
                    }
                }
            }else{
                // Last Notification time is 0 so it is the first notification
                 // Enough time has passed
                    ESP_LOGI(GATT_CALLBACK,"Enough Time has Passed since last notification");
                    ESP_LOGI(GATT_CALLBACK,"Sending Notification Data");
                    #ifdef DEBUG
                        ESP_LOGW(GATT_CALLBACK,"DEBUG Notification Data: %s",gatt_server_application_profile_table[profile_id].notification_queue_buffer);
                        ESP_LOGW(GATT_CALLBACK,"DEBUG Notification Data Length: %d",gatt_server_application_profile_table[profile_id].notification_queue_len);
                        ESP_LOGW(GATT_CALLBACK,"DEBUG Local Storage Value: %s",gatt_server_application_profile_table[profile_id].local_storage);
                        ESP_LOGW(GATT_CALLBACK,"DEBUG Local Storage Length: %d",gatt_server_application_profile_table[profile_id].local_storage_len);
                    #endif
                 err = esp_ble_gatts_send_indicate(
                            gatt_server_application_profile_table[profile_id].profile_interface,
                            gatt_server_application_profile_table[profile_id].connection_id,
                            gatt_server_application_profile_table[profile_id].characteristic_handle,
                            gatt_server_application_profile_table[profile_id].notification_queue_len,
                            gatt_server_application_profile_table[profile_id].notification_queue_buffer,
                            false);
                    if(err != ESP_OK){
                        ESP_LOGE(GATT_CALLBACK,"Error Sending Notification Data retrying...");
                        ESP_LOGE(GATT_CALLBACK,"Error Code: %s",esp_err_to_name(err));
                    }else{
                        ESP_LOGI(GATT_CALLBACK,"Notification Data Sent");

                        // Write the value to the local storage
                        memset(gatt_server_application_profile_table[profile_id].local_storage,0,gatt_server_application_profile_table[profile_id].local_storage_limit); // Clear the memory
                        memcpy(gatt_server_application_profile_table[profile_id].local_storage,gatt_server_application_profile_table[profile_id].notification_queue_buffer,gatt_server_application_profile_table[profile_id].notification_queue_len); // Copy the new value to the storage
                        gatt_server_application_profile_table[profile_id].local_storage_len = gatt_server_application_profile_table[profile_id].notification_queue_len; // Update the value length

                        // Clear the notification queue
                        memset(gatt_server_application_profile_table[profile_id].notification_queue_buffer,0,gatt_server_application_profile_table[profile_id].local_storage_limit);
                        gatt_server_application_profile_table[profile_id].last_notification_time = current_time; // Update the last notification time
                        gatt_server_application_profile_table[profile_id].notification_queue_len = 0;

                        #ifdef DEBUG
                            ESP_LOGW(GATT_CALLBACK,"DEBUG Notification Data: %s",gatt_server_application_profile_table[profile_id].notification_queue_buffer);
                            ESP_LOGW(GATT_CALLBACK,"DEBUG Notification Data Length: %d",gatt_server_application_profile_table[profile_id].notification_queue_len);
                            ESP_LOGW(GATT_CALLBACK,"DEBUG Local Storage Value: %s",gatt_server_application_profile_table[profile_id].local_storage);
                            ESP_LOGW(GATT_CALLBACK,"DEBUG Local Storage Length: %d",gatt_server_application_profile_table[profile_id].local_storage_len);
                        #endif

                        break;
                    }
                    
            }
        }
        if(err != ESP_OK){
            ESP_LOGE(GATT_CALLBACK,"Error Sending Notification Data");
        }
    }else if(gatt_server_application_profile_table[profile_id].cccd_status == 0x0002){
        // Indications are enabled
        ESP_LOGI(GATT_CALLBACK,"Sending Indication Data");
        // esp_ble_gatts_send_indicate(gatt_server_application_profile_table[profile_id].profile_interface,gatt_server_application_profile_table[profile_id].connection_id,gatt_server_application_profile_table[profile_id].characteristic_handle,sizeof(data),&data,true);
    }else{
       // Display the cccd value
        ESP_LOGI(GATT_CALLBACK,"CCCD Value: %d",gatt_server_application_profile_table[profile_id].cccd_status);
    }
}

void write_characteristic_data(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id){
    // Write the data to the characteristic
    // Check if the write is under characteristic length

    if(param->write.len <= gatt_server_application_profile_table[profile_id].local_storage_limit){
        // It is under the size that is allowed so it can be written without any buffering
        if(xSemaphoreTake(profile_semaphores[profile_id],portMAX_DELAY) == pdTRUE){
            err = esp_ble_gatts_set_attr_value(param->write.handle,param->write.len,param->write.value);
            if(err != ESP_OK){
                ESP_LOGE(log_tags[4+profile_id],"Error Setting Attribute Value");
            }else{
                ESP_LOGI(log_tags[4+profile_id],"Attribute Value Set");
            }

            #ifdef DEBUG
                ESP_LOGW(log_tags[4+profile_id],"DEBUG Before Storage Value: %s",gatt_server_application_profile_table[profile_id].local_storage);
                ESP_LOGW(log_tags[4+profile_id],"DEBUG Before Storage Length: %d",gatt_server_application_profile_table[profile_id].local_storage_len);
            #endif
            // Copy the value to the characteristic storage
            memset(gatt_server_application_profile_table[profile_id].local_storage,0,gatt_server_application_profile_table[profile_id].local_storage_limit); // Clear the memory
            memcpy(gatt_server_application_profile_table[profile_id].local_storage,param->write.value,param->write.len); // Copy the new value to the storage

            gatt_server_application_profile_table[profile_id].local_storage_len = param->write.len; // Update the value length

            ESP_LOGI(log_tags[4+profile_id],"Characteristic Storage Updated");

            // This is the write operation that is commpleted so the semaphore can be given out here
            xSemaphoreGive(profile_semaphores[profile_id]);
            ESP_LOGI(log_tags[4+profile_id],"Semaphore Given");

            #ifdef DEBUG
                ESP_LOGW(log_tags[4+profile_id],"DEBUG After Storage Value: %s",gatt_server_application_profile_table[profile_id].local_storage);
                ESP_LOGW(log_tags[4+profile_id],"DEBUG After Storage Length: %d",gatt_server_application_profile_table[profile_id].local_storage_len);
            #endif

            #ifdef DEBUG
                ESP_LOGW(log_tags[4+profile_id],"DEBUG Characteristic Value: %s",param->write.value);
                ESP_LOGW(log_tags[4+profile_id],"DEBUG Characteristic Length: %d",param->write.len);
                ESP_LOGW(log_tags[4+profile_id],"DEBUG Storage Value: %s",gatt_server_application_profile_table[profile_id].local_storage);
                ESP_LOGW(log_tags[4+profile_id],"DEBUG Storage Length: %d",gatt_server_application_profile_table[profile_id].local_storage_len);
            #endif

            // Send a response to the client
            esp_gatt_rsp_t rsp = {0};
            rsp.attr_value.handle = param->write.handle;
            rsp.attr_value.len = param->write.len;
            memcpy(rsp.attr_value.value, param->write.value, param->write.len);

            esp_err_t err = esp_ble_gatts_send_response(
                gatt_interface,
                param->write.conn_id,
                param->write.trans_id,
                ESP_GATT_OK,
                &rsp
            );

            if (err != ESP_OK) {
                ESP_LOGE(log_tags[4+profile_id], "Failed to send write response: %s", esp_err_to_name(err));
            }
        }else{
            ESP_LOGE(log_tags[4+profile_id],"Error Taking Semaphore");
        }
    }
}

void handle_read_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id){
    // This event is when the client wants to execute a read operation
    ESP_LOGI(log_tags[4+profile_id],"GATT Server Read Event handle: %d",param->read.handle);
    // Create response to send to the client
    esp_gatt_rsp_t resp = {
        .attr_value = {
            .handle = param->read.handle,
            .len = gatt_server_application_profile_table[profile_id].local_storage_len,
        }
    };

    memcpy(resp.attr_value.value,gatt_server_application_profile_table[profile_id].local_storage,gatt_server_application_profile_table[profile_id].local_storage_len);

    err = esp_ble_gatts_send_response(gatt_interface,param->read.conn_id,param->read.trans_id,ESP_GATT_OK,&resp);
    if(err != ESP_OK){
        ESP_LOGE(log_tags[4+profile_id],"Error Sending Response");
    }

    #ifdef DEBUG
        // Display the current value in the attribute for debugging purposes
        uint16_t attribute_length = 0;
        uint8_t* attribute_value = NULL;

        esp_ble_gatts_get_attr_value(param->read.handle,&attribute_length,&attribute_value);
        // Display them as characters
        ESP_LOGI(log_tags[4+profile_id],"DEBUG Attribute Value: %s",attribute_value);
        ESP_LOGI(log_tags[4+profile_id],"DEBUG Attribute Length: %d",attribute_length);
        ESP_LOGI(log_tags[4+profile_id],"DEBUG Storage Value: %s",gatt_server_application_profile_table[profile_id].local_storage);
        ESP_LOGI(log_tags[4+profile_id],"DEBUG Storage Length: %d",gatt_server_application_profile_table[profile_id].local_storage_len);
    #endif

}

void handle_create_service_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id,bool requires_notifications){

    esp_gatt_status_t create_status = param->create.status;
    if(create_status == ESP_OK){
        // The service has been created in the event ESP_GATTS_REG_EVT, now the service handle must be created and service must be started
        // Create the service handle
        esp_gatt_srvc_id_t service_id = param->create.service_id;
        gatt_server_application_profile_table[profile_id].service_handle = param->create.service_handle;
        gatt_server_application_profile_table[profile_id].service_id = service_id.id.uuid.uuid.uuid16;
        ESP_LOGI(log_tags[4+profile_id],"Profile Service Handle: %d",param->create.service_handle);

        // Since the service is being created, the characteristics for the service must be created
        // Create the characteristic for the service
        esp_bt_uuid_t characteristic_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = {
                .uuid16 = characteristic_uuids[profile_id]
            }
        };

        gatt_server_application_profile_table[profile_id].characteristic_uuid = characteristic_uuid;

        ESP_LOGI(log_tags[4+profile_id],"Attempting To Start Service: 0x%X",service_id.id.uuid.uuid.uuid16);
        esp_err_t err = esp_ble_gatts_start_service(param->create.service_handle);
        if(err != ESP_OK){
            ESP_LOGE(log_tags[4+profile_id],"Error In Starting Service for service id: 0x%X with handle: %d",param->create.service_id.id.uuid.uuid.uuid16,param->create.service_handle);
        }

        esp_gatt_perm_t perm;
        esp_gatt_char_prop_t prop;

        if(requires_notifications){
            perm = ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE;
            prop = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_WRITE|ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        }else{
            perm = ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE;
            prop = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_WRITE;
        }

        // Adding the characteristic to the service
        err = esp_ble_gatts_add_char(param->create.service_handle,
                                                &characteristic_uuid,
                                                perm,
                                                prop,
                                                &gatt_server_application_profile_table[profile_id].attribute_value
                                                ,NULL);
        if (err != ESP_OK){
            ESP_LOGE(log_tags[4+profile_id],"Error Adding Characteristic for Music Profile");
        }else{
            ESP_LOGI(log_tags[4+profile_id],"Added Characteristic for Music Profile");
        }

        ESP_LOGI(log_tags[4+profile_id],"Sucessfully Started Service for service id: 0x%X with handle: %d",param->create.service_id.id.uuid.uuid.uuid16,param->create.service_handle);
        }else{
            ESP_LOGE(log_tags[4+profile_id],"Failed To Start Service For ID: 0x%X",param->create.service_id.id.uuid.uuid.uuid16);
        }
}

void handle_add_characteristic_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id,bool requires_notifications){
    ESP_LOGI(log_tags[4+profile_id],"GATT Server Add Characteristic Event status: %d",param->add_char.status);
    uint16_t attribute_length = 0;
    const uint8_t *attribute_value = NULL;

    #ifdef DEBUG
        ESP_LOGI(log_tags[4+profile_id],"GATT Server Add Characteristic Event status: %d",param->add_char.status);
        ESP_LOGI(log_tags[4+profile_id],"GATT Server Add Characteristic Event Attribute Handle: %d",param->add_char.attr_handle);
        ESP_LOGI(log_tags[4+profile_id],"GATT Server Add Characteristic Event Service Handle: %d",param->add_char.service_handle);
    #endif

    gatt_server_application_profile_table[profile_id].characteristic_handle = param->add_char.attr_handle;

    #ifdef DEBUG

        // Getting the attribute value to see if it was set correctly
        esp_err_t err = esp_ble_gatts_get_attr_value(param->add_char.attr_handle,&attribute_length,&attribute_value);
        if(err != ESP_OK){
            ESP_LOGE(log_tags[4+profile_id],"Error Getting Attribute Value");
        }

        ESP_LOGI(log_tags[4+profile_id],"Attribute Length: %d",attribute_length);
        ESP_LOGI(log_tags[4+profile_id],"Attribute Value: %s",attribute_value);
        ESP_LOGI(log_tags[4+profile_id],"Attribute Handle: %d",param->add_char.attr_handle);
    #endif

    if(requires_notifications){
        // Adding a characteristic descriptor UUID
        esp_bt_uuid_t cccd_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid = {
                .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG // Standard CCCD UUID
            }
        };

        err = esp_ble_gatts_add_char_descr(
            param->add_char.service_handle,
            &cccd_uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, // Permissions for client configuration
            false,                                    // Initial value
            NULL                                     // No additional control block needed
        );

        gatt_server_application_profile_table[profile_id].characteristic_descriptor_uuid = cccd_uuid;
        gatt_server_application_profile_table[profile_id].characteristic_handle = param->add_char.attr_handle;

        if(err != ESP_OK){
            ESP_LOGE(log_tags[4+profile_id],"Error Adding Characteristic Descriptor");
        }
    }else{
        ESP_LOGI(log_tags[4+profile_id],"Notifications Not Required");
    }
    

}

void handle_add_characteristic_descriptor_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id){
    ESP_LOGI(log_tags[4+profile_id],"GATT Server Add Characteristic Descriptor Event status: %d",param->add_char_descr.status);
    ESP_LOGI(log_tags[4+profile_id],"GATT Server Add Characteristic Descriptor Event Attribute Handle: %d",param->add_char_descr.attr_handle);
    ESP_LOGI(log_tags[4+profile_id],"GATT Server Add Characteristic Descriptor Event Service Handle: %d",param->add_char_descr.service_handle);

    // Get the CCCD Value
    uint16_t cccd_value = 0;
    uint16_t cccd_len = sizeof(cccd_value);
    esp_err_t err = esp_ble_gatts_get_attr_value(param->add_char_descr.attr_handle, &cccd_len, (const uint8_t **)&cccd_value);

    gatt_server_application_profile_table[profile_id].characteristic_descriptor_handle = param->add_char_descr.attr_handle;
    gatt_server_application_profile_table[profile_id].cccd_status = cccd_value;

    if(err != ESP_OK){
        ESP_LOGE(log_tags[4+profile_id],"Error Getting CCCD Value");
    }else{
        ESP_LOGI(log_tags[4+profile_id],"CCCD Value: %d",cccd_value);
    }
}

// Notifications handling logic

bool is_notification_enabled(uint16_t cccd_status){
    return (cccd_status & 0x0001);
}// Check if the notifications are enabled
void enable_notifications(uint16_t* cccd_status){
    *cccd_status = 0x0001;
}
void disable_notifications(uint16_t* cccd_status){
    *cccd_status = 0x0000;
}
void enable_indications(uint16_t* cccd_status){
    *cccd_status = 0x0002;
}
bool has_data_changed(const uint8_t* new_data,const uint8_t* old_data,uint16_t length){
    return memcmp(new_data,old_data,length) != 0;
}

void start_notification_task(int profile_id){
    // Start the task to send notifications
    int task_parameter = malloc(sizeof(int));
    task_parameter = profile_id;
    xTaskCreatePinnedToCore(
        notify_task,
        "Notify Task",
        1024,
        (void*)task_parameter,
        5,
        NULL,
        1
    );
    ESP_LOGI(log_tags[4+profile_id],"Notification Task Started");
}

void update_characteristic_data(int profile_id){
    // Characteristic data needs to be updated and the notifications need to be sent
    
    // Updating the local storage for the characteristic from the notification queue buffer
    memset(gatt_server_application_profile_table[profile_id].local_storage,0,gatt_server_application_profile_table[profile_id].local_storage_limit);
    memcpy(gatt_server_application_profile_table[profile_id].local_storage,gatt_server_application_profile_table[profile_id].notification_queue_buffer,gatt_server_application_profile_table[profile_id].notification_queue_len);

    gatt_server_application_profile_table[profile_id].local_storage_len = gatt_server_application_profile_table[profile_id].notification_queue_len;

    // Need to change the characteristic value
    esp_err_t err = esp_ble_gatts_set_attr_value(gatt_server_application_profile_table[profile_id].characteristic_handle,
                                                gatt_server_application_profile_table[profile_id].local_storage_len,
                                                gatt_server_application_profile_table[profile_id].local_storage);
    if(err != ESP_OK){
        ESP_LOGE(log_tags[4+profile_id],"Error Setting Attribute Value");
    }else{
        ESP_LOGI(log_tags[4+profile_id],"Attribute Value Set");
    }

    // Send the notification to the client
    send_notification_data(profile_id);
}

void notify_task(void *param){

    int profile_id = (int)param;
    
    while(1){
        // Check if the data is changed
        // Send a notification if the data has changed

        // I need to check if the profiles semaphore is available
        if(xSemaphoreTake(profile_semaphores[profile_id],portMAX_DELAY) == pdTRUE){
            // The semaphore is available
            ESP_LOGI(log_tags[4+profile_id],"Semaphore Taken for Profile: %d",profile_id);
            if(has_data_changed(gatt_server_application_profile_table[profile_id].notification_queue_buffer,gatt_server_application_profile_table[profile_id].local_storage,gatt_server_application_profile_table[profile_id].local_storage_limit) && is_notification_enabled(gatt_server_application_profile_table[profile_id].cccd_status)){
                // Data has changed and notifications are enabled
                update_characteristic_data(profile_id);
            }

            // Release the semaphore
            xSemaphoreGive(profile_semaphores[profile_id]);
            ESP_LOGI(log_tags[4+profile_id],"Semaphore Released for Profile: %d",profile_id);
        }else{
            ESP_LOGE(log_tags[4+profile_id],"Error Taking Semaphore for Profile: %d",profile_id);
        }       
            // Delay the task
        vTaskDelay(pdMS_TO_TICKS(100)); // Checking every 100ms to see if the data has changed
    }

}

void push_data_to_notification_queue(int profile_id,uint8_t * data,uint16_t length){
    // Push the data to the notification queue

    // Need to take the semaphore
    if(xSemaphoreTake(profile_semaphores[profile_id],portMAX_DELAY) == pdTRUE){
        ESP_LOGI(log_tags[4+profile_id],"Semaphore Taken for Profile: %d",profile_id);
        if(has_data_changed(data,gatt_server_application_profile_table[profile_id].notification_queue_buffer,gatt_server_application_profile_table[profile_id].local_storage_limit)){
            // Data has changed
            memset(gatt_server_application_profile_table[profile_id].notification_queue_buffer,0,gatt_server_application_profile_table[profile_id].local_storage_limit);
            memcpy(gatt_server_application_profile_table[profile_id].notification_queue_buffer,data,length);
            gatt_server_application_profile_table[profile_id].notification_queue_len = length;
        }   

        // The semaphore needs to be released
        xSemaphoreGive(profile_semaphores[profile_id]);
        ESP_LOGI(log_tags[4+profile_id],"Semaphore Released for Profile: %d",profile_id);
    }else{
        ESP_LOGE(log_tags[4+profile_id],"Error Taking Semaphore for Profile: %d",profile_id);
    }
}
    

void init_semaphores(uint8_t num_profiles){
    // Initialize the semaphores
    for(int profile_no = 0; profile_no < num_profiles; profile_no++){
       profile_semaphores[profile_no] = xSemaphoreCreateMutex();
       ESP_LOGI(log_tags[4+profile_no],"Semaphore Created for Profile: %d",profile_no);
    }
}

/*
    Power Management Task
*/

void power_management_task(void *param){
    esp_err_t pwr_err;
    while(1){
        // Check if the device is connected
        // If the client is connected then keep the device in high power mode
        // If the client is not connected then put the device in low power mode
        if(client_connected){
            // The client is connected so the power needs to be managed
            if(current_power_mode != CLIENT_CONN_LOW_POWER_MODE){
                pwr_err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12); // client is connected so advertisement is not required
                #ifdef DEBUG
                    if(pwr_err != ESP_OK){
                        ESP_LOGE("Power Management","Error Setting Power Level: %s",esp_err_to_name(pwr_err));
                    }else{
                        ESP_LOGI("Power Management","Advertisement Power Set to -12dBm");
                    }
                #endif
                pwr_err = esp_ble_gap_stop_advertising(); // Stop advertising

                #ifdef DEBUG
                    if(pwr_err != ESP_OK){
                        ESP_LOGE("Power Management","Error Stopping Advertisement: %s",esp_err_to_name(pwr_err));
                    }else{
                        ESP_LOGI("Power Management","Advertisement Stopped");
                    }
                #endif

                current_power_mode = CLIENT_CONN_LOW_POWER_MODE;
            }
        }else{
            // The client is not connected so the device needs to be in low power mode
            uint64_t elapsed_time = (client_disconnet_timer > 0)? (esp_timer_get_time()/1000) - client_disconnet_timer : (esp_timer_get_time()/1000) - server_start_timer;

            if(elapsed_time >= PWR_ADV_SWITCH_TIMOUT){
                #ifdef DEBUG
                    ESP_LOGW("Power Management","Elapsed Time: %llu",elapsed_time);
                    ESP_LOGI("Power Management","Switched to Low Power Mode");
                #endif
                if(current_power_mode != LOW_POWER_MODE){
                    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12); // Low power advertising
                    current_power_mode = LOW_POWER_MODE;
                }
                esp_light_sleep_start();
            }else{
                #ifdef DEBUG
                    ESP_LOGI("Power Management"," Waiting to Switch to Low Power Mode");
                #endif
            }
            
        }

        #ifdef DEBUG
            ESP_LOGI("Power Management","Current Power Mode: %d",current_power_mode);
            ESP_LOGI("Power Management", "Stack high-water mark: %d bytes", uxTaskGetStackHighWaterMark(NULL));

        #endif
        // Delay the task
        vTaskDelay(pdMS_TO_TICKS(1000)); // Checking every 1s to see if the device is connected
    }

}

void start_power_management_task(){
    ESP_LOGI("DEBUG", "Free heap size before task: %d bytes", xPortGetFreeHeapSize());

    if(xTaskCreatePinnedToCore(
        power_management_task,
        "Power Management Task",
        4096,
        NULL,
        1,
        NULL,
        tskNO_AFFINITY
    ) == pdPASS){
        ESP_LOGI("DEBUG", "Free heap size after task: %d bytes", xPortGetFreeHeapSize());
        ESP_LOGI("Power Management","Power Management Task Started");
    }else{
        ESP_LOGE("Power Management","Error Starting Power Management Task");
        ESP_LOGE("Power Management","Error Code: %s",esp_err_to_name(err));
    }
}

void initialize_sleep_configuration(){
    // Sleep Configuration

    esp_sleep_enable_timer_wakeup(500000); // 500ms

    err = esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,ESP_PD_OPTION_ON);
    if(err != ESP_OK){
        ESP_LOGE(GAP_INIT,"Error Configuring RTC Peripherals: %s",esp_err_to_name(err));
        return;
    }

    // err = esp_sleep_pd_config(ESP_PD_DOMAIN_RC_FAST,ESP_PD_OPTION_ON);
    // if(err != ESP_OK){
    //     ESP_LOGE(GAP_INIT,"Error Configuring RTC Fast Memory: %s",esp_err_to_name(err));
    //     return;
    // }
}

/*
    Initialize Functions For BLE Server Profile & Server Table
*/

void initialize_server(){

    // Initialize the server table
    gatt_server_application_profile_table = create_server_profile_table(NUM_PROFILES);

    // Initialize the semaphores
    init_semaphores(NUM_PROFILES);

    // Start the power management task
    start_power_management_task();

    // Initialize the sleep configuration
    initialize_sleep_configuration();

    // Initialize the server mode and functions for the ESP32
        /*
        Initialize the NVS Flash
    */

    err = nvs_flash_init();
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Initializing NVS Flash: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"NVS Flash Initialized");

    /*
        Release the Classic Bluetooth Memory
    */

   err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT); // The memory need to be released for the classic BT stack so that only the BLE stack is kept and initialized
   if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Releasing Classic BT Memory: %s",esp_err_to_name(err));
        return;
   }
    ESP_LOGI(GATT_INIT,"Classic BT Memory Released");

    /*
        Initialize the Bluetooth Controller
    */

    esp_bt_controller_config_t ble_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&ble_cfg);
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Initializing Bluetooth Controller: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"Bluetooth Controller Initialized");

    /*
        Enable the Bluetooth Controller
    */

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Enabling Bluetooth Controller: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"Bluetooth Controller Enabled");

    /*
        Initialize the Bluedroid Stack
    */

    err = esp_bluedroid_init();
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Initializing Bluedroid Stack: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"Bluedroid Stack Initialized");

    /*
        Enable the Bluedroid Stack
    */

    err = esp_bluedroid_enable();
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Enabling Bluedroid Stack: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"Bluedroid Stack Enabled");

    /*
        Register GATT & GAP Callback
    */

    err = esp_ble_gatts_register_callback(server_gatt_profile_handler);
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Registering GATT Callback: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"GATT Callback Registered");

    err = esp_ble_gap_register_callback(server_gap_profile_handler);
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Registering GAP Callback: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"GAP Callback Registered");

    /*
        Register the GATT Server Application Profiles
    */

    err = esp_ble_gatts_app_register(MUSIC_PROFILE_ID); // Triggers the registration event
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Registering Music Profile: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"Music Profile Registered");

    err = esp_ble_gatts_app_register(TODO_PROFILE_ID);
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Registering Todo Profile: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"Todo Profile Registered");

    err = esp_ble_gatts_app_register(TIME_PROFILE_ID); 
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Registering Time Profile: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"Time Profile Registered");

    err = esp_ble_gatts_app_register(MUSIC_PLAYBACK_PROFILE_ID);
    if(err != ESP_OK){
        ESP_LOGE(GATT_INIT,"Error Registering Music Playback Profile: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GATT_INIT,"Music Playback Profile Registered");

    /*
        Set the GAP Server Advertisement Data
    */

    err = esp_ble_gap_set_device_name("ESP32_xsSERVER");
    if(err != ESP_OK){
        ESP_LOGE(GAP_INIT,"Error Setting Device Name: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GAP_INIT,"Device Name Set");

    err = esp_ble_gap_config_adv_data(&gap_server_adv_data);
    if(err != ESP_OK){
        ESP_LOGE(GAP_INIT,"Error Configuring Advertisement Data: %s",esp_err_to_name(err));
        return;
    }
    ESP_LOGI(GAP_INIT,"Advertisement Data Configured");

    err = esp_ble_gap_start_advertising(&gap_server_adv_params);
    if(err != ESP_OK){
        ESP_LOGE(GAP_INIT,"Error Starting Advertisement: %s",esp_err_to_name(err));
        return;
    }

    ESP_LOGI(GAP_INIT,"Advertisement Parameters Configured");

    err = esp_ble_gatt_set_local_mtu(517);
    if(err != ESP_OK){
        ESP_LOGE(GAP_INIT,"Error Setting Local MTU: %s",esp_err_to_name(err));
        return;
    }

    ESP_LOGI(GAP_INIT,"Local MTU Set");


} // Initialize the server
profile_t* create_server_profile_table(uint8_t number_of_profiles){
    uint8_t* music_storage = create_profile_storage(MUSIC_PROFILE_CHAR_LEN);
    uint8_t* notification_music_storage = create_profile_storage(MUSIC_PROFILE_CHAR_LEN);
    uint8_t* todo_storage = create_profile_storage(TODO_PROFILE_CHAR_LEN);
    uint8_t* notification_todo_storage = create_profile_storage(TODO_PROFILE_CHAR_LEN);
    uint8_t* time_storage = create_profile_storage(TIME_PROFILE_CHAR_LEN);
    uint8_t* notification_time_storage = create_profile_storage(TIME_PROFILE_CHAR_LEN);
    uint8_t* music_playback_storage = create_profile_storage(MUSIC_PLAYBACK_CHAR_LEN);
    uint8_t* notification_music_playback_storage = create_profile_storage(MUSIC_PLAYBACK_CHAR_LEN);

    // create a GATT Server Profile Table
    profile_t* server_table = (profile_t*) malloc(number_of_profiles*sizeof(profile_t));

    // Add the profiles to the server table
    server_table[MUSIC_PROFILE_ID] = *create_profile(MUSIC_PROFILE_ID,gatt_server_music_profile_handler,music_storage,MUSIC_PROFILE_CHAR_LEN,notification_music_storage);
    server_table[TODO_PROFILE_ID] = *create_profile(TODO_PROFILE_ID,gatt_server_todo_profile_handler,todo_storage,TODO_PROFILE_CHAR_LEN,notification_todo_storage);
    server_table[TIME_PROFILE_ID] = *create_profile(TIME_PROFILE_ID,gatt_server_time_profile_handler,time_storage,TIME_PROFILE_CHAR_LEN,notification_time_storage);
    server_table[MUSIC_PLAYBACK_PROFILE_ID] = *create_profile(MUSIC_PLAYBACK_PROFILE_ID,gatt_server_music_playback_profile_handler,music_playback_storage,MUSIC_PLAYBACK_CHAR_LEN,notification_music_playback_storage);

    return server_table;

} // Create the server profile table

uint8_t* create_profile_storage(uint8_t max_length){
    // Create the storage for the profile
    uint8_t* storage = (uint8_t*)malloc(max_length*sizeof(uint8_t));
    if(storage == NULL){
        ESP_LOGE("Profile Storage","Error Creating Profile Storage");
    }else{
        ESP_LOGI("Profile Storage","Profile Storage Created");
        // Initializing the storage
        memset(storage,0,max_length);
    }

    return storage;

}
profile_t* create_profile(uint8_t profile_id,esp_gatts_cb_t profile_event_handler,uint8_t* storage,uint8_t max_length,uint8_t* notification_queue_buffer){
    profile_t* profile = (profile_t*)malloc(sizeof(profile_t)); // Created the profile

    // Initialize the profile
    profile->profile_interface = ESP_GATT_IF_NONE;
    profile->application_id = profile_id;
    profile->profile_event_handler = profile_event_handler;
    profile->attribute_value.attr_len = max_length;
    profile->attribute_value.attr_max_len = max_length;
    profile->attribute_value.attr_value = storage;
    profile->local_storage = storage;
    profile->local_storage_limit = max_length;
    profile->local_storage_len = 0;
    profile->notification_queue_buffer = notification_queue_buffer;
    profile->notification_queue_len = 0;
    profile->last_notification_time = 0;
    profile->cccd_status = 0x0000;

    return profile;
}

void free_server_profile_table(profile_t* server_table,uint8_t number_of_profiles){
    for(int profile_no = 0; profile_no < number_of_profiles; profile_no++){
        free(server_table[profile_no].local_storage);
        free(server_table[profile_no].notification_queue_buffer);
    }
    free(server_table);
    ESP_LOGI("Server Profile Table","Server Profile Table Freed");
}

void testing_music_metadata_notification(){
    ESP_LOGI(MUSIC_PROFILE_CB, "Testing Music Metadata Notification...");

    int profile_id = MUSIC_PROFILE_ID;

    // Sample new metadata for testing
    const char* new_metadata = "Sample Song Metadata";
    uint16_t metadata_length = strlen(new_metadata);

    // Push the new metadata to the notification queue
    push_data_to_notification_queue(profile_id, (uint8_t*)new_metadata, metadata_length);

    #ifdef DEBUG
        ESP_LOGW(GATT_CALLBACK,"DEBUG Notification Data: %s",gatt_server_application_profile_table[profile_id].notification_queue_buffer);
        ESP_LOGW(GATT_CALLBACK,"DEBUG Notification Data Length: %d",gatt_server_application_profile_table[profile_id].notification_queue_len);
        ESP_LOGW(GATT_CALLBACK,"DEBUG Local Storage Value: %s",gatt_server_application_profile_table[profile_id].local_storage);
        ESP_LOGW(GATT_CALLBACK,"DEBUG Local Storage Length: %d",gatt_server_application_profile_table[profile_id].local_storage_len);
    #endif

    // Notify the client
    send_notification_data(profile_id);
}