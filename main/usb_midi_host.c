#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "usb/usb_host.h"
#include "platform.h"
#include "picoruby-esp32.h"

/* PicoRuby USB-MIDI integration */
#include "../components/picoruby-esp32/picoruby/mrbgems/picoruby-usb_midi/include/usb_midi.h"
#include "../components/picoruby-esp32/picoruby/mrbgems/picoruby-midi/include/midi.h"

static const char *TAG = "USB_HOST_SAMPLE";

#define CLIENT_NUM_EVENT_MSG        5

#define ACTION_OPEN_DEV             0x01
#define ACTION_GET_DEV_INFO         0x02
#define ACTION_GET_DEV_DESC         0x04
#define ACTION_GET_CONFIG_DESC      0x08
#define ACTION_GET_STR_DESC         0x10
#define ACTION_CHECK_MIDI           0x20
#define ACTION_SETUP_MIDI           0x40
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

// Forward declarations for MIDI transfer handling
static usb_transfer_t *g_in_transfer = NULL;
static void midi_in_transfer_callback(usb_transfer_t *transfer);
static volatile bool g_tx_pending = false;  // Track if OUT transfer is in progress
static uint32_t g_tx_pending_since = 0;  // Timestamp when g_tx_pending became true
#define TX_PENDING_TIMEOUT_MS 500  // Timeout for OUT transfer (should complete within 100ms)

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

    ESP_LOGI(TAG, "MIDI endpoint discovery complete:");
    ESP_LOGI(TAG, "  OUT endpoint: 0x%02x (for sending to device)", driver_obj->midi_out_ep);
    ESP_LOGI(TAG, "  IN endpoint: 0x%02x (for receiving from device)", driver_obj->midi_in_ep);
    if (driver_obj->midi_in_ep == 0) {
        ESP_LOGW(TAG, "No MIDI IN endpoint found - device may be output-only");
    }

    // Notify PicoRuby USB-MIDI of device connection
    {
        const usb_device_desc_t *dev_desc;
        usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc);

        usb_device_info_t dev_info;
        usb_host_device_info(driver_obj->dev_hdl, &dev_info);

        usb_midi_device_info_t midi_info = {
            .vendor_id = dev_desc->idVendor,
            .product_id = dev_desc->idProduct,
            .midi_in_ep = driver_obj->midi_in_ep,
            .midi_out_ep = driver_obj->midi_out_ep
        };

        // Copy strings safely (wData is UTF-16LE, extract ASCII)
        if (dev_info.str_desc_manufacturer) {
            size_t len = dev_info.str_desc_manufacturer->bLength > 2 ?
                        (dev_info.str_desc_manufacturer->bLength - 2) / 2 : 0;
            for (size_t i = 0; i < len && i < 63; i++) {
                midi_info.manufacturer[i] = (char)(dev_info.str_desc_manufacturer->wData[i] & 0xFF);
            }
        }
        if (dev_info.str_desc_product) {
            size_t len = dev_info.str_desc_product->bLength > 2 ?
                        (dev_info.str_desc_product->bLength - 2) / 2 : 0;
            for (size_t i = 0; i < len && i < 63; i++) {
                midi_info.product[i] = (char)(dev_info.str_desc_product->wData[i] & 0xFF);
            }
        }

        // Set up MIDI IN transfer for receiving data BEFORE notifying connection
        ESP_LOGI(TAG, "MIDI IN setup check: midi_in_ep=0x%02x, g_in_transfer=%p",
                 driver_obj->midi_in_ep, (void*)g_in_transfer);
        if (driver_obj->midi_in_ep != 0 && g_in_transfer == NULL) {
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

                ret = usb_host_transfer_submit(g_in_transfer);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "MIDI IN transfer submitted successfully");
                    // Wait for USB host to be ready to receive data
                    // This helps when reconnecting to an already-running MIDI device
                    vTaskDelay(pdMS_TO_TICKS(100));
                    ESP_LOGI(TAG, "MIDI IN transfer ready after stabilization delay");
                } else {
                    ESP_LOGE(TAG, "Failed to submit MIDI IN transfer: %s", esp_err_to_name(ret));
                    usb_host_transfer_free(g_in_transfer);
                    g_in_transfer = NULL;
                }
            } else {
                ESP_LOGE(TAG, "Failed to allocate MIDI IN transfer: %s", esp_err_to_name(ret));
            }
        }

        // Notify connection AFTER MIDI IN transfer is ready
        USB_MIDI_notify_connected(&midi_info);
    }

    driver_obj->actions &= ~ACTION_SETUP_MIDI;
    ESP_LOGI(TAG, "MIDI setup complete - ready for PicoRuby control");
}

static void midi_out_transfer_callback(usb_transfer_t *transfer)
{
    // Clear pending flag to allow next OUT transfer
    g_tx_pending = false;
    g_tx_pending_since = 0;

    // Minimal OUT transfer logging
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE(TAG, "MIDI OUT failed: %d", transfer->status);
    }

    // Free the transfer after completion
    usb_host_transfer_free(transfer);
}

static void midi_in_transfer_callback(usb_transfer_t *transfer)
{
    class_driver_t *driver_obj = (class_driver_t *)transfer->context;

    // Check for device disconnection first
    if (transfer->status == USB_TRANSFER_STATUS_CANCELED ||
        transfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
        transfer->status == USB_TRANSFER_STATUS_ERROR) {

        ESP_LOGI(TAG, "MIDI IN transfer ended (status: %d) - device disconnected", transfer->status);
        // Trigger device cleanup
        if (driver_obj != NULL && driver_obj->dev_addr != 0) {
            ESP_LOGI(TAG, "Setting ACTION_CLOSE_DEV from MIDI IN callback");
            driver_obj->actions |= ACTION_CLOSE_DEV;
        }
        usb_host_transfer_free(transfer);
        g_in_transfer = NULL;  // Clear pointer after freeing
        return; // Don't resubmit
    }


    // Handle timeout separately - timeout is normal when device has no data to send
    if (transfer->status == USB_TRANSFER_STATUS_TIMED_OUT) {
        // Timeout is normal - just resubmit
        esp_err_t ret = usb_host_transfer_submit(transfer);
        if (ret != ESP_OK) {
            if (ret == ESP_ERR_INVALID_STATE) {
                ESP_LOGI(TAG, "MIDI IN stopped after timeout - device disconnected");
                if (driver_obj != NULL && driver_obj->dev_addr != 0) {
                    driver_obj->actions |= ACTION_CLOSE_DEV;
                }
            } else {
                ESP_LOGW(TAG, "Failed to resubmit MIDI IN after timeout: %s", esp_err_to_name(ret));
            }
            usb_host_transfer_free(transfer);
            g_in_transfer = NULL;  // Clear pointer after freeing
        }
        return;
    }

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        if (transfer->actual_num_bytes > 0) {
            // Push data to PicoRuby USB-MIDI ring buffer
            USB_MIDI_push_rx_data(transfer->data_buffer, transfer->actual_num_bytes);

            // Parse USB MIDI packet (4 bytes per packet) - minimal logging
            for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
                if (i + 3 < transfer->actual_num_bytes) {
                    uint8_t midi1 = transfer->data_buffer[i + 1];
                    uint8_t midi2 = transfer->data_buffer[i + 2];
                    uint8_t midi3 = transfer->data_buffer[i + 3];

                    // Only log decoded MIDI messages
                    if ((midi1 & 0xF0) == 0x90 && midi3 > 0) {
                        ESP_LOGD(TAG, "NOTE ON: Ch%d, Note%d, Vel%d", midi1 & 0x0F, midi2, midi3);
                    } else if ((midi1 & 0xF0) == 0x80 || ((midi1 & 0xF0) == 0x90 && midi3 == 0)) {
                        ESP_LOGD(TAG, "NOTE OFF: Ch%d, Note%d", midi1 & 0x0F, midi2);
                    }
                }
            }
        }

        // Resubmit for next data
        esp_err_t ret = usb_host_transfer_submit(transfer);
        if (ret != ESP_OK) {
            if (ret == ESP_ERR_INVALID_STATE) {
                ESP_LOGI(TAG, "MIDI IN stopped - device disconnected");
                // Trigger device cleanup
                if (driver_obj != NULL && driver_obj->dev_addr != 0) {
                    ESP_LOGI(TAG, "Setting ACTION_CLOSE_DEV from MIDI IN resubmit failure");
                    driver_obj->actions |= ACTION_CLOSE_DEV;
                }
            } else {
                ESP_LOGE(TAG, "Failed to resubmit MIDI IN: %s", esp_err_to_name(ret));
            }
            usb_host_transfer_free(transfer);
            g_in_transfer = NULL;  // Clear pointer after freeing
        }
    } else {
        ESP_LOGW(TAG, "MIDI IN transfer failed with status: %d", transfer->status);
        // Trigger device cleanup for unexpected failures
        if (driver_obj != NULL && driver_obj->dev_addr != 0) {
            ESP_LOGI(TAG, "Setting ACTION_CLOSE_DEV from MIDI IN failure");
            driver_obj->actions |= ACTION_CLOSE_DEV;
        }
        usb_host_transfer_free(transfer);
        g_in_transfer = NULL;  // Clear pointer after freeing
    }
}

// Helper function to reset static variables
static void reset_midi_static_vars(void)
{
    // Reset static variables to ensure clean state for reconnection
    ESP_LOGI(TAG, "Resetting MIDI static variables for device reconnection");

    // Reset transfer state
    if (g_in_transfer != NULL) {
        ESP_LOGW(TAG, "Force cleanup: MIDI IN transfer still exists during disconnect");
        // Don't free here as callback might still be processing - just mark as null
        g_in_transfer = NULL;
    }

    // Reset TX state
    g_tx_pending = false;
    g_tx_pending_since = 0;

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
    driver_obj->num_interfaces = 0;

    // Reset static variables in action_send_note
    reset_midi_static_vars();

    // Notify PicoRuby USB-MIDI of device disconnection
    USB_MIDI_notify_disconnected();

    // Clear action and prepare for new device
    driver_obj->actions &= ~ACTION_CLOSE_DEV;

    ESP_LOGI(TAG, "Cleanup complete - monitoring for new device connections");
}

/* Helper function to process TX queue from PicoRuby */
static void process_midi_tx_queue(class_driver_t *driver_obj)
{
    if (driver_obj->dev_hdl == NULL || driver_obj->midi_out_ep == 0) {
        /* Device not available - drain and discard any pending packets */
        uint8_t discard[4];
        int discarded = 0;
        while (USB_MIDI_pop_tx_packet(discard)) {
            discarded++;
        }
        if (discarded > 0) {
            ESP_LOGD(TAG, "Discarded %d TX packets (device unavailable)", discarded);
        }
        return;
    }

    /* CRITICAL: Only submit one OUT transfer at a time to avoid blocking IN transfers.
     * The previous implementation could submit multiple OUT transfers in quick succession,
     * which seemed to prevent the USB host from processing IN transfers properly. */
    if (g_tx_pending) {
        /* Check for timeout - if callback hasn't been called within TX_PENDING_TIMEOUT_MS,
         * force reset the pending flag to recover from stuck state */
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if (g_tx_pending_since > 0 && (now - g_tx_pending_since) > TX_PENDING_TIMEOUT_MS) {
            ESP_LOGW(TAG, "TX pending timeout (%lu ms) - forcing reset", now - g_tx_pending_since);
            g_tx_pending = false;
            g_tx_pending_since = 0;
            /* Don't return - allow new transfer to be submitted */
        } else {
            return;  // Wait for current OUT transfer to complete
        }
    }

    uint8_t packet[4];
    if (USB_MIDI_pop_tx_packet(packet)) {
        usb_transfer_t *transfer;
        esp_err_t ret = usb_host_transfer_alloc(4, 0, &transfer);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to alloc TX transfer");
            return;
        }

        memcpy(transfer->data_buffer, packet, 4);
        transfer->device_handle = driver_obj->dev_hdl;
        transfer->bEndpointAddress = driver_obj->midi_out_ep;
        transfer->num_bytes = 4;
        transfer->timeout_ms = 100;
        transfer->callback = midi_out_transfer_callback;
        transfer->context = driver_obj;

        g_tx_pending = true;  // Mark OUT transfer as pending
        g_tx_pending_since = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        ret = usb_host_transfer_submit(transfer);
        if (ret != ESP_OK) {
            g_tx_pending = false;  // Clear pending flag on failure
            g_tx_pending_since = 0;
            usb_host_transfer_free(transfer);
            if (ret == ESP_ERR_INVALID_STATE) {
                /* Device disconnected - stop MIDI clock and input immediately */
                if (MIDI_Clock_is_running()) {
                    MIDI_Clock_stop();
                }
                if (MIDI_Input_is_running()) {
                    MIDI_Input_stop();
                }
                /* Drain remaining packets */
                int discarded = 1;  /* Count current packet */
                while (USB_MIDI_pop_tx_packet(packet)) {
                    discarded++;
                }
                ESP_LOGI(TAG, "Device disconnected during TX, stopped MIDI, discarded %d packets", discarded);
            } else {
                ESP_LOGW(TAG, "TX submit failed: %s", esp_err_to_name(ret));
            }
            return;
        }
    }
}

static void class_driver_task(void *arg)
{
    SemaphoreHandle_t signaling_sem = (SemaphoreHandle_t)arg;
    class_driver_t driver_obj = {0};

    /* Initialize PicoRuby USB-MIDI subsystem */
    USB_MIDI_init();

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
    while (true) {
        // Process MIDI TX queue - one packet at a time to avoid blocking IN transfers
        if (driver_obj.dev_hdl != NULL && driver_obj.is_midi_device &&
            !(driver_obj.actions & ACTION_CLOSE_DEV)) {
            process_midi_tx_queue(&driver_obj);
        }

        // Handle events - use short timeout for responsive UI
        usb_host_client_handle_events(driver_obj.client_hdl, pdMS_TO_TICKS(5));

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
        if (driver_obj.actions & ACTION_CLOSE_DEV) {
            ESP_LOGI(TAG, "ACTION_CLOSE_DEV detected in main loop");
            action_close_dev(&driver_obj);
        }


        vTaskDelay(pdMS_TO_TICKS(5));  // Shorter delay for faster event processing
    }

    ESP_LOGI(TAG, "Deregistering client");
    ESP_ERROR_CHECK(usb_host_client_deregister(driver_obj.client_hdl));

    xSemaphoreGive(signaling_sem);
    vTaskSuspend(NULL);
}

// PicoRuby task - runs the Ruby shell on a separate core
static void picoruby_task(void *arg)
{
    ESP_LOGI(TAG, "Starting PicoRuby shell...");
    picoruby_esp32();
    // picoruby_esp32() is blocking, so this won't normally be reached
    vTaskDelete(NULL);
}

void app_main(void)
{
    // Initialize platform (LCD on M5Stack, no-op on Freenove)
    ESP_ERROR_CHECK(platform_init());

    BaseType_t task_created;
    SemaphoreHandle_t signaling_sem = xSemaphoreCreateBinary();

    ESP_LOGI(TAG, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    task_created = xTaskCreatePinnedToCore(class_driver_task, "class", 4096, signaling_sem, 1, NULL, 0);
    assert(task_created == pdTRUE);

    // Start PicoRuby shell on Core 1 (separate from USB Host on Core 0)
    // Note: PicoRuby shell uses serial console, works on Freenove but not on M5Stack CoreS3 SE
    // when USB Host is active (shared USB-C port)
    // Stack 16KB for complex scripts, priority 3 (higher than USB host at 2)
    task_created = xTaskCreatePinnedToCore(picoruby_task, "picoruby", 16384, NULL, 3, NULL, 1);
    if (task_created != pdTRUE) {
        ESP_LOGW(TAG, "Failed to create PicoRuby task");
    } else {
        ESP_LOGI(TAG, "PicoRuby task started on Core 1");
    }

    xSemaphoreTake(signaling_sem, portMAX_DELAY);

    ESP_LOGI(TAG, "Waiting for USB devices to be connected");

    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(pdMS_TO_TICKS(10), &event_flags);

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No more clients");
            usb_host_device_free_all();
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "All devices freed - ready for new connections");
            // Don't break here - continue waiting for new devices
        }

        // Update platform (touch events, UI, etc.)
        platform_update();

        // Add small delay to prevent tight loop
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Uninstalling USB Host Library");
    ESP_ERROR_CHECK(usb_host_uninstall());
    vSemaphoreDelete(signaling_sem);
}
