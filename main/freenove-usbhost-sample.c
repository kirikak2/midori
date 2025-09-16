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
} class_driver_t;

static void client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    class_driver_t *driver_obj = (class_driver_t *)arg;
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            if (driver_obj->dev_addr == 0) {
                driver_obj->dev_addr = event_msg->new_dev.address;
                driver_obj->actions |= ACTION_OPEN_DEV;
            }
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            if (driver_obj->dev_hdl != NULL) {
                driver_obj->actions |= ACTION_CLOSE_DEV;
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

    // Find MIDI streaming interface and its endpoints
    for (int intf_num = 0; intf_num < config_desc->bNumInterfaces; intf_num++) {
        int offset = 0;
        const usb_intf_desc_t *intf_desc = usb_parse_interface_descriptor(config_desc, intf_num, 0, &offset);

        if (intf_desc != NULL &&
            intf_desc->bInterfaceClass == 0x01 &&
            intf_desc->bInterfaceSubClass == 0x03) {

            ESP_LOGI(TAG, "Found MIDI interface %d with %d endpoints",
                    intf_desc->bInterfaceNumber, intf_desc->bNumEndpoints);

            // Claim the MIDI interface
            esp_err_t ret = usb_host_interface_claim(driver_obj->client_hdl, driver_obj->dev_hdl,
                                                   intf_desc->bInterfaceNumber,
                                                   intf_desc->bAlternateSetting);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to claim MIDI interface: %s", esp_err_to_name(ret));
                continue;
            }
            ESP_LOGI(TAG, "Successfully claimed MIDI interface %d", intf_desc->bInterfaceNumber);

            // Parse endpoints - reset offset for endpoint parsing
            int ep_offset = 0;
            for (int ep_idx = 0; ep_idx < intf_desc->bNumEndpoints; ep_idx++) {
                const usb_ep_desc_t *ep_desc = usb_parse_endpoint_descriptor_by_index(intf_desc, ep_idx, config_desc->wTotalLength, &ep_offset);
                if (ep_desc != NULL) {
                    ESP_LOGI(TAG, "Endpoint %d: Address=0x%02x, Type=%d, MaxPacket=%d",
                            ep_idx, ep_desc->bEndpointAddress, ep_desc->bmAttributes & 0x03, ep_desc->wMaxPacketSize);

                    if ((ep_desc->bEndpointAddress & 0x80) == 0) { // OUT endpoint
                        driver_obj->midi_out_ep = ep_desc->bEndpointAddress;
                        ESP_LOGI(TAG, "MIDI OUT endpoint: 0x%02x", driver_obj->midi_out_ep);
                    } else { // IN endpoint
                        driver_obj->midi_in_ep = ep_desc->bEndpointAddress;
                        ESP_LOGI(TAG, "MIDI IN endpoint: 0x%02x", driver_obj->midi_in_ep);
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to parse endpoint %d", ep_idx);
                }
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

    driver_obj->note_counter = 60; // Start with middle C
    driver_obj->actions &= ~ACTION_SETUP_MIDI;
    driver_obj->actions |= ACTION_SEND_NOTE;
}

static void midi_out_transfer_callback(usb_transfer_t *transfer)
{
    ESP_LOGI(TAG, "MIDI OUT transfer callback - Status: %d", transfer->status);

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGI(TAG, "MIDI OUT transfer completed successfully");
    } else {
        ESP_LOGE(TAG, "MIDI OUT transfer failed with status: %d", transfer->status);
    }

    // Free the transfer after completion
    usb_host_transfer_free(transfer);
}

static void midi_in_transfer_callback(usb_transfer_t *transfer)
{
    ESP_LOGI(TAG, "MIDI IN transfer callback - Status: %d, Length: %d",
             transfer->status, transfer->actual_num_bytes);

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0) {
        ESP_LOGI(TAG, "*** MIDI PACKET RECEIVED ***");
        ESP_LOG_BUFFER_HEX(TAG, transfer->data_buffer, transfer->actual_num_bytes);

        // Parse USB MIDI packet (4 bytes per packet)
        for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
            if (i + 3 < transfer->actual_num_bytes) {
                uint8_t cin = transfer->data_buffer[i] & 0x0F;
                uint8_t midi1 = transfer->data_buffer[i + 1];
                uint8_t midi2 = transfer->data_buffer[i + 2];
                uint8_t midi3 = transfer->data_buffer[i + 3];

                ESP_LOGI(TAG, "MIDI: CIN=0x%x, Data=[0x%02x 0x%02x 0x%02x]",
                         cin, midi1, midi2, midi3);

                // Decode MIDI message
                if ((midi1 & 0xF0) == 0x90 && midi3 > 0) {
                    ESP_LOGI(TAG, "NOTE ON: Channel=%d, Note=%d, Velocity=%d",
                             midi1 & 0x0F, midi2, midi3);
                } else if ((midi1 & 0xF0) == 0x80 || ((midi1 & 0xF0) == 0x90 && midi3 == 0)) {
                    ESP_LOGI(TAG, "NOTE OFF: Channel=%d, Note=%d",
                             midi1 & 0x0F, midi2);
                }
            }
        }
    }

    // Resubmit transfer for continuous listening
    esp_err_t ret = usb_host_transfer_submit(transfer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to resubmit MIDI IN transfer: %s", esp_err_to_name(ret));
    }
}

static void action_send_note(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);

    // Start MIDI IN transfer for receiving data
    static usb_transfer_t *in_transfer = NULL;
    if (in_transfer == NULL && driver_obj->midi_in_ep != 0) {
        esp_err_t ret = usb_host_transfer_alloc(64, 0, &in_transfer);
        if (ret == ESP_OK) {
            in_transfer->device_handle = driver_obj->dev_hdl;
            in_transfer->callback = midi_in_transfer_callback;
            in_transfer->context = driver_obj;
            in_transfer->bEndpointAddress = driver_obj->midi_in_ep;
            in_transfer->timeout_ms = 0;
            in_transfer->num_bytes = 64;

            ret = usb_host_transfer_submit(in_transfer);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "MIDI IN transfer started");
            } else {
                ESP_LOGE(TAG, "Failed to submit MIDI IN transfer: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGE(TAG, "Failed to allocate MIDI IN transfer: %s", esp_err_to_name(ret));
        }
    }

    // Send NOTE ON only if OUT endpoint is available
    if (driver_obj->midi_out_ep == 0) {
        ESP_LOGW(TAG, "No MIDI OUT endpoint available, skipping MIDI send");
    } else {
        ESP_LOGI(TAG, "Sending NOTE ON: Note=%d", driver_obj->note_counter);

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
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "NOTE ON submitted successfully");
            } else {
                ESP_LOGE(TAG, "Failed to submit NOTE ON: %s", esp_err_to_name(ret));
                usb_host_transfer_free(note_on_transfer);
            }
        }

        // Wait a bit then send NOTE OFF
        vTaskDelay(pdMS_TO_TICKS(500));

        // Send NOTE OFF
        usb_transfer_t *note_off_transfer;
        ret = usb_host_transfer_alloc(4, 0, &note_off_transfer);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Sending NOTE OFF: Note=%d", driver_obj->note_counter);
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
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "NOTE OFF submitted successfully");
            } else {
                ESP_LOGE(TAG, "Failed to submit NOTE OFF: %s", esp_err_to_name(ret));
                usb_host_transfer_free(note_off_transfer);
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

static void action_close_dev(class_driver_t *driver_obj)
{
    ESP_LOGI(TAG, "Closing device");
    ESP_ERROR_CHECK(usb_host_device_close(driver_obj->client_hdl, driver_obj->dev_hdl));
    driver_obj->dev_hdl = NULL;
    driver_obj->dev_addr = 0;
    driver_obj->actions &= ~ACTION_CLOSE_DEV;
    driver_obj->actions |= ACTION_EXIT;
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

    bool exit_loop = false;
    while (!exit_loop) {
        if (driver_obj.actions == 0) {
            usb_host_client_handle_events(driver_obj.client_hdl, portMAX_DELAY);
        }

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
            action_send_note(&driver_obj);
        }
        if (driver_obj.actions & ACTION_CLOSE_DEV) {
            action_close_dev(&driver_obj);
        }
        if (driver_obj.actions & ACTION_EXIT) {
            exit_loop = true;
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

    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No more clients");
            usb_host_device_free_all();
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "All devices freed");
            break;
        }
    }

    ESP_LOGI(TAG, "Uninstalling USB Host Library");
    ESP_ERROR_CHECK(usb_host_uninstall());
    vSemaphoreDelete(signaling_sem);
}
