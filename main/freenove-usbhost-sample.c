#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "usb/usb_host.h"

static const char *TAG = "USB_HOST_SAMPLE";

#define CLIENT_NUM_EVENT_MSG        5

#define ACTION_OPEN_DEV             0x01
#define ACTION_GET_DEV_INFO         0x02
#define ACTION_GET_DEV_DESC         0x04
#define ACTION_GET_CONFIG_DESC      0x08
#define ACTION_GET_STR_DESC         0x10
#define ACTION_CHECK_MIDI           0x20
#define ACTION_SETUP_MIDI           0x40
#define ACTION_SEND_NOTE            0x80
#define ACTION_CLOSE_DEV            0x100
#define ACTION_EXIT                 0x200

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    uint32_t actions;
    uint8_t midi_in_ep;
    uint8_t midi_out_ep;
    bool is_midi_device;
    uint8_t note_counter;
    uint8_t num_interfaces; // Store interface count for complex device detection
} class_driver_t;

static void client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    class_driver_t *driver_obj = (class_driver_t *)arg;
    ESP_LOGI(TAG, "client_event_callback called - Event: %d", event_msg->event);
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            if (driver_obj->dev_addr == 0) {
                driver_obj->dev_addr = event_msg->new_dev.address;
                driver_obj->actions |= ACTION_OPEN_DEV;
            }
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            ESP_LOGI(TAG, "USB_HOST_CLIENT_EVENT_DEV_GONE received");
            ESP_LOGI(TAG, "Device address: %d, dev_hdl: %p", driver_obj->dev_addr, driver_obj->dev_hdl);
            // Always trigger cleanup when device is gone, regardless of dev_hdl state
            if (driver_obj->dev_addr != 0) {
                ESP_LOGI(TAG, "Setting ACTION_CLOSE_DEV flag");
                driver_obj->actions |= ACTION_CLOSE_DEV;
            } else {
                ESP_LOGW(TAG, "Device gone event but no device address recorded");
            }
            break;
        default:
            break;
    }
}

static void action_open_dev(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_addr != 0);
    ESP_LOGI(TAG, "Opening device at address %d", driver_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_open(driver_obj->client_hdl, driver_obj->dev_addr, &driver_obj->dev_hdl));
    driver_obj->actions &= ~ACTION_OPEN_DEV;
    driver_obj->actions |= ACTION_GET_DEV_INFO;
}

static void action_get_info(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device information");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    ESP_LOGI(TAG, "\t%s speed", (dev_info.speed == USB_SPEED_LOW) ? "Low" : "Full");
    ESP_LOGI(TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue);
    driver_obj->actions &= ~ACTION_GET_DEV_INFO;
    driver_obj->actions |= ACTION_GET_DEV_DESC;
}

static void action_get_dev_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device descriptor");
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);
    driver_obj->actions &= ~ACTION_GET_DEV_DESC;
    driver_obj->actions |= ACTION_GET_CONFIG_DESC;
}

static void action_get_config_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);
    driver_obj->actions &= ~ACTION_GET_CONFIG_DESC;
    driver_obj->actions |= ACTION_GET_STR_DESC;
}

static void action_get_str_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    if (dev_info.str_desc_manufacturer) {
        ESP_LOGI(TAG, "Getting manufacturer string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product) {
        ESP_LOGI(TAG, "Getting product string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num) {
        ESP_LOGI(TAG, "Getting serial number string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
    driver_obj->actions &= ~ACTION_GET_STR_DESC;
    driver_obj->actions |= ACTION_CHECK_MIDI;
}

static void action_check_midi(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Checking if device is MIDI device");

    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc));

    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));

    bool is_midi_device = false;

    // Parse interfaces to find MIDI streaming interface
    // Check both Audio class devices (0x01) and Composite devices (0x00)
    ESP_LOGI(TAG, "Scanning interfaces for MIDI streaming...");
    ESP_LOGI(TAG, "Number of interfaces: %d", config_desc->bNumInterfaces);

    for (int intf_num = 0; intf_num < config_desc->bNumInterfaces; intf_num++) {
        int offset = 0;
        const usb_intf_desc_t *intf_desc = usb_parse_interface_descriptor(config_desc, intf_num, 0, &offset);
        if (intf_desc != NULL) {
            ESP_LOGI(TAG, "Interface %d: Class=0x%02x, SubClass=0x%02x, Protocol=0x%02x",
                    intf_desc->bInterfaceNumber,
                    intf_desc->bInterfaceClass,
                    intf_desc->bInterfaceSubClass,
                    intf_desc->bInterfaceProtocol);

            if (intf_desc->bInterfaceClass == 0x01 && // Audio interface class
                intf_desc->bInterfaceSubClass == 0x03) { // MIDI streaming subclass
                ESP_LOGI(TAG, "MIDI streaming interface found!");
                is_midi_device = true;
                break;
            }
        }
    }

    if (is_midi_device) {
        ESP_LOGI(TAG, "*** MIDI DEVICE DETECTED ***");
        ESP_LOGI(TAG, "Vendor ID: 0x%04x, Product ID: 0x%04x",
                dev_desc->idVendor, dev_desc->idProduct);
        driver_obj->is_midi_device = true;
        driver_obj->actions &= ~ACTION_CHECK_MIDI;
        driver_obj->actions |= ACTION_SETUP_MIDI;
    } else {
        ESP_LOGI(TAG, "Device is not a MIDI device");
        ESP_LOGI(TAG, "Device class: 0x%02x, Subclass: 0x%02x",
                dev_desc->bDeviceClass, dev_desc->bDeviceSubClass);
        driver_obj->actions &= ~ACTION_CHECK_MIDI;
        driver_obj->actions |= ACTION_CLOSE_DEV;
    }
}

static void action_setup_midi(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Setting up MIDI endpoints");

    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));

    // First pass: Show ALL interfaces for analysis
    ESP_LOGI(TAG, "=== ANALYZING ALL INTERFACES ===");
    for (int intf_num = 0; intf_num < config_desc->bNumInterfaces; intf_num++) {
        int offset = 0;
        const usb_intf_desc_t *intf_desc = usb_parse_interface_descriptor(config_desc, intf_num, 0, &offset);
        if (intf_desc != NULL) {
            ESP_LOGI(TAG, "Interface %d: Class=0x%02x, SubClass=0x%02x, Protocol=0x%02x, AltSetting=%d, NumEP=%d",
                    intf_desc->bInterfaceNumber,
                    intf_desc->bInterfaceClass,
                    intf_desc->bInterfaceSubClass,
                    intf_desc->bInterfaceProtocol,
                    intf_desc->bAlternateSetting,
                    intf_desc->bNumEndpoints);
        }
    }

    // Second pass: Find and claim MIDI streaming interface
    ESP_LOGI(TAG, "=== SEARCHING FOR MIDI INTERFACE ===");
    for (int intf_num = 0; intf_num < config_desc->bNumInterfaces; intf_num++) {
        int offset = 0;
        const usb_intf_desc_t *intf_desc = usb_parse_interface_descriptor(config_desc, intf_num, 0, &offset);

        if (intf_desc != NULL &&
            intf_desc->bInterfaceClass == 0x01 &&
            intf_desc->bInterfaceSubClass == 0x03) {

            ESP_LOGI(TAG, "Found MIDI interface %d with %d endpoints",
                    intf_desc->bInterfaceNumber, intf_desc->bNumEndpoints);

            // Store interface count for later use
            driver_obj->num_interfaces = config_desc->bNumInterfaces;

            // For Roland J-6: Check if this is a complex Composite device
            if (config_desc->bNumInterfaces > 2) {
                ESP_LOGW(TAG, "Complex Composite device detected (%d interfaces)", config_desc->bNumInterfaces);
                ESP_LOGW(TAG, "This may cause issues with ESP32-S3 USB Host limitations");
                ESP_LOGW(TAG, "Attempting MIDI-only operation...");
            }

            // Try to claim the MIDI interface
            ESP_LOGI(TAG, "Attempting to claim MIDI interface %d (alt setting %d)",
                    intf_desc->bInterfaceNumber, intf_desc->bAlternateSetting);

            esp_err_t ret = usb_host_interface_claim(driver_obj->client_hdl, driver_obj->dev_hdl,
                                                   intf_desc->bInterfaceNumber,
                                                   intf_desc->bAlternateSetting);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to claim MIDI interface %d: %s",
                        intf_desc->bInterfaceNumber, esp_err_to_name(ret));

                // Try with different alternate setting
                if (intf_desc->bAlternateSetting == 0) {
                    ESP_LOGI(TAG, "Trying alternate setting 1 for interface %d", intf_desc->bInterfaceNumber);
                    ret = usb_host_interface_claim(driver_obj->client_hdl, driver_obj->dev_hdl,
                                                 intf_desc->bInterfaceNumber, 1);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to claim MIDI interface with alt setting 1: %s", esp_err_to_name(ret));
                        continue;
                    } else {
                        ESP_LOGI(TAG, "Successfully claimed MIDI interface %d with alt setting 1", intf_desc->bInterfaceNumber);
                    }
                } else {
                    continue;
                }
            } else {
                ESP_LOGI(TAG, "Successfully claimed MIDI interface %d with alt setting %d",
                        intf_desc->bInterfaceNumber, intf_desc->bAlternateSetting);
            }

            // Parse endpoints - use sequential parsing
            ESP_LOGI(TAG, "Parsing %d endpoints for MIDI interface", intf_desc->bNumEndpoints);

            // Get all endpoints in the interface
            const uint8_t *desc_ptr = (const uint8_t *)intf_desc;
            desc_ptr += intf_desc->bLength; // Skip interface descriptor

            for (int ep_idx = 0; ep_idx < intf_desc->bNumEndpoints; ep_idx++) {
                // Find endpoint descriptor
                while (desc_ptr[1] != USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
                    desc_ptr += desc_ptr[0]; // Move to next descriptor
                }

                const usb_ep_desc_t *ep_desc = (const usb_ep_desc_t *)desc_ptr;
                ESP_LOGI(TAG, "Endpoint %d: Address=0x%02x, Type=%d, MaxPacket=%d",
                        ep_idx, ep_desc->bEndpointAddress, ep_desc->bmAttributes & 0x03, ep_desc->wMaxPacketSize);

                if ((ep_desc->bEndpointAddress & 0x80) == 0) { // OUT endpoint
                    driver_obj->midi_out_ep = ep_desc->bEndpointAddress;
                    ESP_LOGI(TAG, "MIDI OUT endpoint: 0x%02x", driver_obj->midi_out_ep);
                } else { // IN endpoint
                    driver_obj->midi_in_ep = ep_desc->bEndpointAddress;
                    ESP_LOGI(TAG, "MIDI IN endpoint: 0x%02x", driver_obj->midi_in_ep);
                }

                desc_ptr += ep_desc->bLength; // Move to next descriptor
            }

            // Check if we found both endpoints
            if (driver_obj->midi_out_ep == 0) {
                ESP_LOGW(TAG, "No MIDI OUT endpoint found");
            }
            if (driver_obj->midi_in_ep == 0) {
                ESP_LOGW(TAG, "No MIDI IN endpoint found - checking other interfaces");
            }
            break;
        }
    }

    ESP_LOGI(TAG, "MIDI setup complete:");
    ESP_LOGI(TAG, "  OUT endpoint: 0x%02x", driver_obj->midi_out_ep);
    ESP_LOGI(TAG, "  IN endpoint: 0x%02x", driver_obj->midi_in_ep);

    driver_obj->note_counter = 60; // Start with middle C
    driver_obj->actions &= ~ACTION_SETUP_MIDI;
    driver_obj->actions |= ACTION_SEND_NOTE;
}

static void midi_out_transfer_callback(usb_transfer_t *transfer)
{
    // Minimal OUT transfer logging
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE(TAG, "MIDI OUT failed: %d", transfer->status);
    }

    // Free the transfer after completion
    usb_host_transfer_free(transfer);
}

static void midi_in_transfer_callback(usb_transfer_t *transfer)
{
    // Check for device disconnection first
    if (transfer->status == USB_TRANSFER_STATUS_CANCELED ||
        transfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
        transfer->status == USB_TRANSFER_STATUS_ERROR) {

        ESP_LOGI(TAG, "MIDI IN transfer ended (status: %d) - device disconnected", transfer->status);
        usb_host_transfer_free(transfer);
        return; // Don't resubmit
    }

    // Always log callback invocation for debugging
    static uint32_t callback_count = 0;
    callback_count++;

    // Minimal callback logging
    ESP_LOGI(TAG, "MIDI IN #%lu: %d bytes", callback_count, transfer->actual_num_bytes);

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        if (transfer->actual_num_bytes > 0) {
            // Parse USB MIDI packet (4 bytes per packet) - minimal logging
            for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
                if (i + 3 < transfer->actual_num_bytes) {
                    uint8_t midi1 = transfer->data_buffer[i + 1];
                    uint8_t midi2 = transfer->data_buffer[i + 2];
                    uint8_t midi3 = transfer->data_buffer[i + 3];

                    // Only log decoded MIDI messages
                    if ((midi1 & 0xF0) == 0x90 && midi3 > 0) {
                        ESP_LOGI(TAG, "NOTE ON: Ch%d, Note%d, Vel%d", midi1 & 0x0F, midi2, midi3);
                    } else if ((midi1 & 0xF0) == 0x80 || ((midi1 & 0xF0) == 0x90 && midi3 == 0)) {
                        ESP_LOGI(TAG, "NOTE OFF: Ch%d, Note%d", midi1 & 0x0F, midi2);
                    }
                }
            }
        }

        // Only resubmit if transfer completed successfully
        esp_err_t ret = usb_host_transfer_submit(transfer);
        if (ret != ESP_OK) {
            if (ret == ESP_ERR_INVALID_STATE) {
                ESP_LOGI(TAG, "MIDI IN stopped - device disconnected");
            } else {
                ESP_LOGE(TAG, "Failed to resubmit MIDI IN: %s", esp_err_to_name(ret));
            }
            usb_host_transfer_free(transfer);
        }
    } else {
        ESP_LOGW(TAG, "MIDI IN transfer failed with status: %d", transfer->status);
        usb_host_transfer_free(transfer);
    }
}

// Static variables for MIDI state management - moved to file scope for reset capability
static usb_transfer_t *g_in_transfer = NULL;
static bool g_transfer_cleanup_done = false;
static bool g_midi_enabled = false;
static uint8_t g_last_dev_addr = 0;

static void action_send_note(class_driver_t *driver_obj)
{
    // Skip if no device is connected
    if (driver_obj->dev_hdl == NULL) {
        return;
    }

    // Reset transfer if device was disconnected
    if (driver_obj->dev_hdl == NULL && g_in_transfer != NULL && !g_transfer_cleanup_done) {
        ESP_LOGI(TAG, "Cleaning up MIDI IN transfer after device disconnect");
        // Don't free here - let the callback handle it when it fails
        g_in_transfer = NULL;
        g_transfer_cleanup_done = true;
    }

    // Reset cleanup flag when new device connects
    if (driver_obj->dev_hdl != NULL && g_transfer_cleanup_done) {
        g_transfer_cleanup_done = false;
    }

    if (g_in_transfer == NULL) {
        if (driver_obj->midi_in_ep == 0) {
            ESP_LOGW(TAG, "MIDI IN endpoint not found (0x%02x), cannot start MIDI IN transfer", driver_obj->midi_in_ep);
        } else {
            ESP_LOGI(TAG, "Starting MIDI IN transfer on endpoint 0x%02x", driver_obj->midi_in_ep);
            esp_err_t ret = usb_host_transfer_alloc(64, 0, &g_in_transfer);
            if (ret == ESP_OK) {
                g_in_transfer->device_handle = driver_obj->dev_hdl;
                g_in_transfer->callback = midi_in_transfer_callback;
                g_in_transfer->context = driver_obj;
                g_in_transfer->bEndpointAddress = driver_obj->midi_in_ep;
                // Roland J-6 may need longer timeout due to complex audio interfaces
                g_in_transfer->timeout_ms = (driver_obj->num_interfaces > 2) ? 5000 : 1000;
                g_in_transfer->num_bytes = 64;

                ESP_LOGI(TAG, "MIDI IN transfer setup:");
                ESP_LOGI(TAG, "  Device handle: %p", g_in_transfer->device_handle);
                ESP_LOGI(TAG, "  Callback: %p", g_in_transfer->callback);
                ESP_LOGI(TAG, "  Endpoint: 0x%02x", g_in_transfer->bEndpointAddress);
                ESP_LOGI(TAG, "  Timeout: %lu ms", g_in_transfer->timeout_ms);
                ESP_LOGI(TAG, "  Buffer size: %d bytes", g_in_transfer->num_bytes);

                ret = usb_host_transfer_submit(g_in_transfer);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "MIDI IN transfer submitted successfully");
                } else {
                    ESP_LOGE(TAG, "Failed to submit MIDI IN transfer: %s", esp_err_to_name(ret));
                    usb_host_transfer_free(g_in_transfer);
                    g_in_transfer = NULL;
                }
            } else {
                ESP_LOGE(TAG, "Failed to allocate MIDI IN transfer: %s", esp_err_to_name(ret));
            }
        }
    } else {
        // MIDI IN transfer already running, check if it's still active
        static uint32_t monitor_count = 0;
        monitor_count++;
        if (monitor_count % 50 == 0) { // Every 50 calls (100 seconds)
            ESP_LOGI(TAG, "MIDI monitoring: %lu cycles", monitor_count);
        }
    }

    // Send Roland J-6 MIDI enable command first (only once)
    // Reset MIDI enabled state if device was disconnected or changed
    if (driver_obj->dev_hdl == NULL || driver_obj->dev_addr != g_last_dev_addr) {
        g_midi_enabled = false;
        g_last_dev_addr = driver_obj->dev_addr;
    }

    if (!g_midi_enabled && driver_obj->midi_out_ep != 0 && driver_obj->dev_hdl != NULL) {
        // Send All Sound Off / Reset All Controllers to ensure device is ready
        usb_transfer_t *enable_transfer;
        esp_err_t ret = usb_host_transfer_alloc(4, 0, &enable_transfer);
        if (ret == ESP_OK) {
            // All Sound Off on all channels (CC 120)
            enable_transfer->data_buffer[0] = 0x0B; // Cable 0, Control Change
            enable_transfer->data_buffer[1] = 0xB0; // Control Change, Channel 1
            enable_transfer->data_buffer[2] = 120;  // All Sound Off
            enable_transfer->data_buffer[3] = 0x00; // Value 0

            enable_transfer->device_handle = driver_obj->dev_hdl;
            enable_transfer->bEndpointAddress = driver_obj->midi_out_ep;
            enable_transfer->num_bytes = 4;
            enable_transfer->timeout_ms = 1000;
            enable_transfer->callback = midi_out_transfer_callback;
            enable_transfer->context = driver_obj;

            ret = usb_host_transfer_submit(enable_transfer);
            if (ret == ESP_OK) {
                g_midi_enabled = true;
            } else {
                ESP_LOGE(TAG, "Failed to send MIDI enable: %s", esp_err_to_name(ret));
                usb_host_transfer_free(enable_transfer);
            }
        }

        // Wait before sending notes
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Send NOTE ON only if device is connected, OUT endpoint is available and MIDI is enabled
    if (driver_obj->dev_hdl == NULL) {
        ESP_LOGW(TAG, "No device connected, skipping MIDI send");
    } else if (driver_obj->midi_out_ep == 0) {
        ESP_LOGW(TAG, "No MIDI OUT endpoint available, skipping MIDI send");
    } else if (!g_midi_enabled) {
        // Skip note send until MIDI is enabled
    } else {

        // Send NOTE ON
        usb_transfer_t *note_on_transfer;
        esp_err_t ret = usb_host_transfer_alloc(4, 0, &note_on_transfer);
        if (ret == ESP_OK) {
            // USB MIDI packet: Cable Number + Code Index Number + MIDI bytes
            note_on_transfer->data_buffer[0] = 0x09; // Cable 0, Note On
            note_on_transfer->data_buffer[1] = 0x90; // Note On, Channel 1
            note_on_transfer->data_buffer[2] = driver_obj->note_counter; // Note number
            note_on_transfer->data_buffer[3] = 0x7F; // Velocity 127

            note_on_transfer->device_handle = driver_obj->dev_hdl;
            note_on_transfer->bEndpointAddress = driver_obj->midi_out_ep;
            note_on_transfer->num_bytes = 4;
            note_on_transfer->timeout_ms = 1000;
            note_on_transfer->callback = midi_out_transfer_callback;
            note_on_transfer->context = driver_obj;

            ret = usb_host_transfer_submit(note_on_transfer);
            if (ret != ESP_OK) {
                if (ret == ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG, "NOTE ON skipped (device disconnected)");
                } else {
                    ESP_LOGE(TAG, "NOTE ON failed: %s", esp_err_to_name(ret));
                }
                usb_host_transfer_free(note_on_transfer);
            } else {
                ESP_LOGI(TAG, "NOTE ON sent: note=%d", driver_obj->note_counter);
            }
        }

        // Wait a bit then send NOTE OFF
        vTaskDelay(pdMS_TO_TICKS(500));

        // Send NOTE OFF
        usb_transfer_t *note_off_transfer;
        ret = usb_host_transfer_alloc(4, 0, &note_off_transfer);
        if (ret == ESP_OK) {
            note_off_transfer->data_buffer[0] = 0x08; // Cable 0, Note Off
            note_off_transfer->data_buffer[1] = 0x80; // Note Off, Channel 1
            note_off_transfer->data_buffer[2] = driver_obj->note_counter; // Note number
            note_off_transfer->data_buffer[3] = 0x00; // Velocity 0

            note_off_transfer->device_handle = driver_obj->dev_hdl;
            note_off_transfer->bEndpointAddress = driver_obj->midi_out_ep;
            note_off_transfer->num_bytes = 4;
            note_off_transfer->timeout_ms = 1000;
            note_off_transfer->callback = midi_out_transfer_callback;
            note_off_transfer->context = driver_obj;

            ret = usb_host_transfer_submit(note_off_transfer);
            if (ret != ESP_OK) {
                if (ret == ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG, "NOTE OFF skipped (device disconnected)");
                } else {
                    ESP_LOGE(TAG, "NOTE OFF failed: %s", esp_err_to_name(ret));
                }
                usb_host_transfer_free(note_off_transfer);
            } else {
                ESP_LOGI(TAG, "NOTE OFF sent: note=%d", driver_obj->note_counter);
            }
        }
    }

    driver_obj->note_counter++;
    if (driver_obj->note_counter > 72) { // C5
        driver_obj->note_counter = 60; // Reset to C4
    }

    driver_obj->actions &= ~ACTION_SEND_NOTE;

    // Continue sending notes every 2 seconds
    vTaskDelay(pdMS_TO_TICKS(2000));
    driver_obj->actions |= ACTION_SEND_NOTE;
}

// Helper function to reset static variables in action_send_note
static void reset_midi_static_vars(void)
{
    // Reset static variables in action_send_note to ensure clean state for reconnection
    // This function will be called from action_close_dev
    ESP_LOGI(TAG, "Resetting MIDI static variables for device reconnection");

    // Reset transfer state
    if (g_in_transfer != NULL) {
        ESP_LOGW(TAG, "Force cleanup: MIDI IN transfer still exists during disconnect");
        // Don't free here as callback might still be processing - just mark as null
        g_in_transfer = NULL;
    }
    g_transfer_cleanup_done = false;

    // Reset MIDI enable state
    g_midi_enabled = false;
    g_last_dev_addr = 0;

    ESP_LOGI(TAG, "MIDI static variables reset complete");
}

static void action_close_dev(class_driver_t *driver_obj)
{
    ESP_LOGI(TAG, "Device disconnected - cleaning up");
    ESP_LOGI(TAG, "  dev_hdl: %p, dev_addr: %d, is_midi_device: %d",
             driver_obj->dev_hdl, driver_obj->dev_addr, driver_obj->is_midi_device);

    // Clean up MIDI transfers first
    // Note: Static transfer pointers in action_send_note will be reset

    // Release MIDI interface if we claimed it
    if (driver_obj->dev_hdl != NULL && driver_obj->is_midi_device) {
        ESP_LOGI(TAG, "Releasing MIDI interface...");
        // Find and release the MIDI interface
        const usb_config_desc_t *config_desc;
        esp_err_t ret = usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc);
        if (ret == ESP_OK) {
            for (int intf_num = 0; intf_num < config_desc->bNumInterfaces; intf_num++) {
                int offset = 0;
                const usb_intf_desc_t *intf_desc = usb_parse_interface_descriptor(config_desc, intf_num, 0, &offset);
                if (intf_desc != NULL &&
                    intf_desc->bInterfaceClass == 0x01 &&
                    intf_desc->bInterfaceSubClass == 0x03) {
                    ESP_LOGI(TAG, "Releasing interface %d", intf_desc->bInterfaceNumber);
                    ret = usb_host_interface_release(driver_obj->client_hdl, driver_obj->dev_hdl,
                                                     intf_desc->bInterfaceNumber);
                    if (ret != ESP_OK) {
                        ESP_LOGW(TAG, "Interface release failed: %s", esp_err_to_name(ret));
                    } else {
                        ESP_LOGI(TAG, "Interface %d released successfully", intf_desc->bInterfaceNumber);
                    }
                    break;
                }
            }
        } else {
            ESP_LOGW(TAG, "Could not get config descriptor for interface release: %s", esp_err_to_name(ret));
        }
    }

    // Close the device
    if (driver_obj->dev_hdl != NULL) {
        ESP_LOGI(TAG, "Closing device handle...");
        esp_err_t ret = usb_host_device_close(driver_obj->client_hdl, driver_obj->dev_hdl);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Device close failed (expected during disconnect): %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Device closed successfully");
        }
        driver_obj->dev_hdl = NULL;
    }

    // Reset all device-related state
    driver_obj->dev_addr = 0;
    driver_obj->midi_in_ep = 0;
    driver_obj->midi_out_ep = 0;
    driver_obj->is_midi_device = false;
    driver_obj->note_counter = 60; // Reset to middle C
    driver_obj->num_interfaces = 0;

    // Reset static variables in action_send_note
    reset_midi_static_vars();

    // Clear action and prepare for new device
    driver_obj->actions &= ~ACTION_CLOSE_DEV;

    ESP_LOGI(TAG, "Cleanup complete - monitoring for new device connections");
}

static void class_driver_task(void *arg)
{
    SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;
    class_driver_t driver_obj = {0};

    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_callback,
            .callback_arg = &driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &driver_obj.client_hdl));

    xSemaphoreGive(signaling_sem);
    vTaskDelay(10);

    // Continuous loop for USB device hot-plug support
    uint32_t monitor_cycles = 0;
    while (true) {
        // Always handle events with longer timeout to ensure callbacks are processed
        usb_host_client_handle_events(driver_obj.client_hdl, pdMS_TO_TICKS(100));

        if (driver_obj.actions & ACTION_OPEN_DEV) {
            action_open_dev(&driver_obj);
        }
        if (driver_obj.actions & ACTION_GET_DEV_INFO) {
            action_get_info(&driver_obj);
        }
        if (driver_obj.actions & ACTION_GET_DEV_DESC) {
            action_get_dev_desc(&driver_obj);
        }
        if (driver_obj.actions & ACTION_GET_CONFIG_DESC) {
            action_get_config_desc(&driver_obj);
        }
        if (driver_obj.actions & ACTION_GET_STR_DESC) {
            action_get_str_desc(&driver_obj);
        }
        if (driver_obj.actions & ACTION_CHECK_MIDI) {
            action_check_midi(&driver_obj);
        }
        if (driver_obj.actions & ACTION_SETUP_MIDI) {
            action_setup_midi(&driver_obj);
        }
        if (driver_obj.actions & ACTION_SEND_NOTE) {
            // Only send notes if device is still connected
            if (driver_obj.dev_hdl != NULL) {
                action_send_note(&driver_obj);
            } else {
                // Clear action if device is disconnected
                driver_obj.actions &= ~ACTION_SEND_NOTE;
            }
        }
        if (driver_obj.actions & ACTION_CLOSE_DEV) {
            ESP_LOGI(TAG, "ACTION_CLOSE_DEV detected in main loop");
            action_close_dev(&driver_obj);
        }

        // Monitor for new device connections every 5 seconds
        monitor_cycles++;
        if (monitor_cycles % 500 == 0 && driver_obj.dev_hdl == NULL) { // Every 5 seconds when no device
            ESP_LOGI(TAG, "Monitoring for USB device connections...");
        }

        vTaskDelay(10);
    }

    ESP_LOGI(TAG, "Deregistering client");
    ESP_ERROR_CHECK(usb_host_client_deregister(driver_obj.client_hdl));

    xSemaphoreGive(signaling_sem);
    vTaskSuspend(NULL);
}

void app_main(void)
{
    BaseType_t task_created;
    SemaphoreHandle_t signaling_sem = xSemaphoreCreateBinary();

    ESP_LOGI(TAG, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    task_created = xTaskCreatePinnedToCore(class_driver_task, "class", 4096, signaling_sem, 2, NULL, 0);
    assert(task_created == pdTRUE);

    xSemaphoreTake(signaling_sem, portMAX_DELAY);

    ESP_LOGI(TAG, "Waiting for USB devices to be connected");

    static uint32_t lib_monitor_cycles = 0;
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(pdMS_TO_TICKS(100), &event_flags);

        // Debug event flags periodically
        lib_monitor_cycles++;
        if (lib_monitor_cycles % 100 == 0) { // Every 10 seconds
            ESP_LOGI(TAG, "USB Host Lib: event_flags=0x%08lx", event_flags);
        }

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No more clients");
            usb_host_device_free_all();
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "All devices freed - ready for new connections");
            // Don't break here - continue waiting for new devices
        }

        // Add small delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Uninstalling USB Host Library");
    ESP_ERROR_CHECK(usb_host_uninstall());
    vSemaphoreDelete(signaling_sem);
}
