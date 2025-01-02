#pragma once

#include "hal_ble.h"

#define NUM_PROFILES 4

/*
    Profile ID's
*/
#define MUSIC_PROFILE_ID 0
#define TODO_PROFILE_ID 1
#define TIME_PROFILE_ID 2
#define MUSIC_PLAYBACK_PROFILE_ID 3

/*
    Macros For Notification Management
*/
#define MAX_NOTIFCATION_RETRIES 3
#define NOTIFICATION_INTERVAL 500 // 0.5 second interval for notifications

/*
    Macros For Power Management
*/

#define PWR_ADV_SWITCH_TIMOUT 30000 // 30 seconds for the power management task to switch between full power and low power mode

/*
    Macros For Storage Profile Storage Limits
*/

#define MUSIC_PROFILE_CHAR_LEN 32
#define TODO_PROFILE_CHAR_LEN 32
#define TIME_PROFILE_CHAR_LEN 5
#define MUSIC_PLAYBACK_CHAR_LEN 5

/*
    Macros For Debugging
*/

#define DEBUG
// #define TESTING

/*
    Macros For Logging
*/

#define GATT_INIT "GATT_INIT"
#define GATT_CALLBACK "GATT_CALLBACK"
#define GAP_INIT "GAP_INIT"
#define GAP_CALLBACK "GAP_CALLBACK"
#define MUSIC_PROFILE_CB "MUSIC_PROFILE_CB"
#define MUSIC_PLAYBACK_PROFILE_CB "MUSIC_PLAYBACK_CB"
#define TODO_PROFILE_CB "TODO_PROFILE_CB"
#define TIME_PROFILE_CB "TIME_PROFILE_CB"

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

static uint8_t profile_service_uuids[32] = {
  // Music Profile UUID LSB -> MSB
  0x4f,0xaf,0xc2,0x01,0x1f,0xb5,0x45,0x9e,0x8f,0xcc,0xc5,0xc9,0xc3,0x31,0x91,0x4b,
  // Todo list Profile UUID LSB -> MSB
  0x28,0xbd,0x3c,0x28,0x63,0x5d,0x11,0xee,0x8c,0x99,0x02,0x42,0xac,0x12,0x00,0x02,

};

/*!
    @brief Profile Structure to hold the GATT Profile Information & Storage
*/
typedef struct{
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
} profile_t;

/*
    Structures For The Server
*/

/*!
    Creating a structure to hold the power information
*/
typedef enum {
    HIGH_POWER_MODE                     = 0,
    LOW_POWER_MODE                      = 1,
    CLIENT_CONN_LOW_POWER_MODE          = 2,
} power_mode_t;

/*
    Advertisement Data Structure
*/

esp_ble_adv_data_t bsp_gap_server_adv_data = {
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

/*
    Advertisement Parameters Structure
*/

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

/*
    Connection Parameters Structure
*/

esp_ble_conn_update_params_t conn_params = {
    .latency = 4,    // Skipping 4 intervals
    .max_int = 0x50,
    .min_int = 0x30,
    .timeout = 500,  // 5 seconds
};



/*
    UUID's For The Profiles & Server
*/


//Creating an array of 16 bit UUIDs for the services that will be created based on the bluetooth specification used as standard

static uint16_t service_uuids[NUM_PROFILES] = {
    0x1840, // Music Service 
    0x1801, // Todo Service
    0x1847, // Time Service
    0x1848 // Music Playback Service
};

// Creating array of 16 bit UUIDs for the characteristics that will be created based on the bluetooth specification used as standard
static uint16_t characteristic_uuids[NUM_PROFILES] = {
    0x2B93, // Music Characteristic
    0x2A3D, // Todo Characteristic
    0x2A2B, // Time Characteristic
    0x2BA3 // Music Playback Characteristic
};

profile_t* bsp_gatt_server_application_profile_table;

/*
    State Variables
*/

// Create a flag to see if a client is connected
static bool client_connected = false;

// Creating a timer for the advertisement
static TimerHandle_t advertisement_timer; // This timer will be used to switch between full power and low power mode

// Updating the data can cause some issues so Semmaphores need to be used to prevent race conditions
// Creating mutex for each of the number of profiles so that mutual exclusions can be created for anything
// targeting the local storage and the notification queue especially when there is a writing being carried out to the notification and the local storage
static SemaphoreHandle_t bsp_profile_semaphores[NUM_PROFILES];

// Creating a timer for the server start

uint64_t server_start_timer = 0;

// Creating a timer for the client disconnection
uint64_t client_disconnet_timer = 0;

// Creating a timer for the last notification time
uint8_t current_power_mode = HIGH_POWER_MODE;


/*
    Function Definitions
*/

// Creating the GATT functions and the profile handlers
/*!
    @brief GATT Server Profile Event Handler
    @param event The event that is being handled
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
*/
static void bsp_server_gatt_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param);
/*!
    @brief GAP Server Profile Event Handler
    @param event The event that is being handled
    @param param The parameters for the event
*/
static void bsp_server_gap_profile_handler(esp_gap_ble_cb_event_t event,esp_ble_gap_cb_param_t *param);

/*!
    @brief Music Profile Event Handler
    @param event The event that is being handled
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
*/
static void bsp_gatt_server_music_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param);
/*!
    @brief Todo Profile Event Handler
    @param event The event that is being handled
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
*/
static void bsp_gatt_server_todo_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param);
/*!
    @brief Time Profile Event Handler
    @param event The event that is being handled
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
*/
static void bsp_gatt_server_time_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param);
/*!
    @brief Music Playback Profile Event Handler
    @param event The event that is being handled
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
*/
static void bsp_gatt_server_music_playback_profile_handler(esp_gatts_cb_event_t event,esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param);

//  Creating modular functions to implement certain functions in order to make the code more readable

/*!
    @brief Handle Client Characteristic Configuration Descriptor of the Server Characteristic depending on the configuration
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
    @param profile_id The profile ID
*/

static void bsp_handle_client_characteristic_configuration_descriptor(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id);
/*!
    @brief Send Notification Data to the Client
    @param profile_id The profile ID
*/
void bsp_send_notification_data(int profile_id);
/*!
    @brief Write Characteristic Data to the Client
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
    @param profile_id The profile ID
*/
void bsp_write_characteristic_data(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id);
/*!
    @brief Handle Read Request from the Client
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
    @param profile_id The profile ID
*/
void bsp_handle_read_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id);
/*!
    @brief Handle Create Service Request
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
    @param profile_id The profile ID
    @param requires_notifications If the profile requires notifications
*/
void bsp_handle_create_service_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id,bool requires_notifications);
/*!
    @brief Handle Add Characteristic Request
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
    @param profile_id The profile ID
    @param requires_notifications If the profile requires notifications
*/
void bsp_handle_add_characteristic_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id,bool requires_notifications);
/*!
    @brief Handle Add Characteristic Descriptor Request
    @param gatt_interface The GATT Interface
    @param param The parameters for the event
    @param profile_id The profile ID
*/
void bsp_handle_add_characteristic_descriptor_request(esp_gatt_if_t gatt_interface,esp_ble_gatts_cb_param_t *param,int profile_id);
/*!
    @brief Update Characteristic Data
    @param profile_id The profile ID
*/
void bsp_update_characteristic_data(int profile_id);// Notification Management Functions

/*!
    @brief Check if the notifications are enabled
    @param cccd_status The CCCD Status
    @return True if the notifications are enabled, false otherwise
*/
bool bsp_is_notification_enabled(uint16_t cccd_status);

/*!
    @brief Enable the notifications
    @param cccd_status The CCCD Status
*/
void bsp_enable_notifications(uint16_t* cccd_status);

/*!
    @brief Disable the notifications
    @param cccd_status The CCCD Status
*/
void bsp_disable_notifications(uint16_t* cccd_status);
/*!
    @brief Check if the data has changed
    @param new_data The new data
    @param old_data The old data
    @param length The length of the data
    @return True if the data has changed, false otherwise
*/
bool bsp_has_data_changed(const uint8_t* new_data,const uint8_t* old_data,uint16_t length);// Check if the data has changed
/*!
    @brief Notify the client of the data change
    @param param The parameters for the event
*/
void bsp_start_notification_task(int profile_id);
/*!
    @brief Notify the client of the data change
    @param param The parameters for the event
*/
void bsp_notify_task(void *param);
/*!
    @brief Initialize the semaphores for the profiles
    @param num_profiles The number of profiles
*/
void bsp_init_semaphores(uint8_t num_profiles);
/*!
    @brief Handle the advertisement timer
    @param param The parameters for the event
*/
void bsp_push_data_to_notification_queue(int profile_id,uint8_t * data,uint16_t length);

// Power Management Functions

/*!
    @brief Power Management Task
    @param param The parameters for the event
*/
void bsp_power_management_task(void *param);
/*!
    @brief Start the power management task
*/
void bsp_start_power_management_task();
/*!
    @brief Initialize the sleep configuration
*/
void bsp_initialize_sleep_configuration();
// Functions To Initialize the server and its profiles

/*!
    @brief Initialize the server
    @param device_name The device name
*/
void bsp_initialize_server(char* device_name);
/*!
    @brief Create the storage for the profile
    @param max_length The maximum length of the storage
    @return The storage for the profile
*/
uint8_t* bsp_create_profile_storage(uint8_t max_length);

/*!
    @brief Create The Server Profile Table
    @param number_of_profiles The number of profiles
    @return The server profile table
*/
profile_t* bsp_create_server_profile_table(uint8_t number_of_profiles);
/*!
    @brief Create a profile
    @param profile_id The profile ID
    @param profile_event_handler The profile event handler
    @param storage The storage for the profile
    @param max_length The maximum length of the storage
    @param notification_queue_buffer The notification queue buffer
    @return The profile
*/
profile_t* bsp_create_profile(uint8_t profile_id,esp_gatts_cb_t profile_event_handler,uint8_t* storage,uint8_t max_length,uint8_t* notification_queue_buffer);
/*!
    @brief Free the server profile table
    @param server_table The server table
    @param number_of_profiles The number of profiles
*/
void bsp_free_server_profile_table(profile_t* server_table,uint8_t number_of_profiles);

/*!
    @brief Stop the server
*/
void bsp_stop_server();

#ifdef TESTING
    /*
        Testing Functions
    */

    void test_music_metadata_notification(); // Test the music metadata notification
    void music_metadata_notification_task(void *param); // Music Metadata Notification Task
    void start_music_notification_task(); // Start the music notification task

#endif

// Disconnect Profile
/*!
    @brief Disconnect a profile
    @param profile_id The profile ID
*/
void bsp_disconnect_profile(int profile_id);










