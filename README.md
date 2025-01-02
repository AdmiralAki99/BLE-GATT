# BLE Server Component for Smartwatch

This repository showcases a BLE Server component designed specifically for a smartwatch project. It is made in a modular layered embedded architecture for easier feature addition and changing the logic or swapping MCU's.

## **Layered Architecture**
![Layered Embedded Software drawio](https://github.com/user-attachments/assets/51eff44a-411d-40f7-83f4-b358c3dd2ae1)

## **Features**

- **Layered Architecture:** Implements a clean separation between HAL (Hardware Abstraction Layer), BSP (Board Support Layer), and the Application Layer.
- **Dynamic Profile Management:** Supports dynamic creation and management of BLE profiles and characteristics.
- **Notification Support:** Efficient notification handling for real-time updates to connected clients.
- **Power Management:** Integrated power-saving features using low-power BLE advertising and light sleep mode.
- **Thread Safety:** Uses FreeRTOS semaphores for synchronized access to shared resources.
- **Modular Design:** Demonstrates integration potential with other components like I2C drivers and LVGL for UI.

## **Project Structure**

```
|-- src/
|   |-- hal_ble.c          # Hardware Abstraction Layer (HAL) implementation
|   |-- bsp_ble.c          # Board Support Layer (BSL) implementation
|   |-- app_ble.c          # Application-specific logic
|-- include/
|   |-- hal_ble.h          # HAL public header
|   |-- bsp_ble.h          # BSP public header
|   |-- app_ble.h          # Application interface
|-- README.md              # Documentation
|-- sdkconfig              # ESP-IDF configuration file
```

## **Architecture**

### **HAL (Hardware Abstraction Layer)**
The HAL encapsulates all direct interactions with ESP-IDF APIs, ensuring hardware-specific operations are abstracted.

#### Responsibilities:
- BLE service and characteristic creation.
- Power management configurations.
- Direct ESP-IDF API calls.

#### Key Functions:
- `hal_ble_create_service()`
- `hal_ble_add_characteristic()`
- `hal_ble_send_notification()`
- `hal_ble_set_adv_tx_power_low()`

### **BSP (Board Support Layer)**
The BSP manages BLE profiles, characteristics, notifications, and server logic. It builds on top of HAL.

#### Responsibilities:
- Dynamic profile creation and initialization.
- Notification management and data synchronization.
- BLE event handling.

#### Key Functions:
- `bsp_create_server_profile_table()`
- `bsp_write_characteristic_data()`
- `bsp_push_data_to_notification_queue()`
- `bsp_notify_task()`

### **Application Layer**
The Application Layer provides smartwatch-specific logic, integrating BLE with other components such as I2C drivers and LVGL.

#### Responsibilities:
- Application-specific tasks like updating profiles or testing notifications.
- Starting tasks and managing the overall system.

#### Key Functions:
- `app_ble_start()`
- `app_ble_send_notification(uint8_t profile_id, uint8_t* data, uint16_t length)`
- `app_stop_server()`

## **Usage Instructions**

### **Setup**
1. Clone the repository:
   ```bash
   git clone https://github.com/AdmiralAki99/BLE-GATT.git
   ```

2. Set up the ESP-IDF environment:
   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

3. Configure the project:
   ```bash
   idf.py menuconfig
   ```
   - Enable Bluetooth and BLE features.
   - Adjust stack sizes and memory settings as needed.

4. Build and flash the firmware:
   ```bash
   idf.py build flash monitor
   ```

### **Customizing Profiles**
- Use `bsp_create_server_profile_table()` to define BLE profiles.
- Modify `app_ble.c` to implement smartwatch-specific logic.

### **Adding New Profiles**
1. Define a new profile in `bsp_ble.h`.
2. Implement the profile handler in `bsp_ble.c`.
3. Add the profile to the `bsp_gatt_server_application_profile_table`.

## **Power Management**
- The `bsp_power_management_task()` dynamically adjusts BLE power and transitions the device into light sleep when inactive.
- Configurable using `PWR_ADV_SWITCH_TIMEOUT` and other macros.

## **Testing Notifications**
- Test notification functionality using the `app_test_notification()` function in `app_ble.c`.
- Simulate characteristic updates and observe client-side responses.

## **Integration with Other Components**
This BLE server is designed to integrate seamlessly with other smartwatch components:
- **I2C Touch Driver:** Reads touch data and updates BLE characteristics.
- **LVGL UI Library:** Displays Information based on the BLE profile on a graphical interface.

## **License**
This project is licensed under the MIT License. See the LICENSE file for details.

## **Acknowledgments**
Special thanks to the ESP-IDF community and contributors for providing robust BLE support.

