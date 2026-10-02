#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ble_print.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "led_strip.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "usb/usb_host.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define WIFI_READY BIT0
#define JOB_QUEUE_DEPTH 12
#define USB_CHUNK 4096

typedef struct {
    uint8_t *data;
    size_t len;
} print_job_t;

typedef struct {
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t out_endpoint;
    volatile bool transfer_done;
    volatile usb_transfer_status_t transfer_status;
    volatile bool disconnected;
} printer_t;

static const char *TAG = "zp450";
static EventGroupHandle_t wifi_events;
static QueueHandle_t print_queue;
static volatile bool printer_online;
static led_strip_handle_t status_led;
static SemaphoreHandle_t led_mutex;

typedef enum {
    LED_WIFI_CONNECTING,
    LED_PRINTER_OFFLINE,
    LED_PRINTER_READY,
    LED_PRINTING,
    LED_ERROR,
} led_state_t;

static void led_set(led_state_t state)
{
    if (!status_led || xSemaphoreTake(led_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    const uint8_t b = CONFIG_ZP_RGB_BRIGHTNESS;
    uint8_t r = 0, g = 0, blue = 0;
    switch (state) {
        case LED_WIFI_CONNECTING: r = b; blue = b; break; // purple
        case LED_PRINTER_OFFLINE: blue = b; break;
        case LED_PRINTER_READY:   g = b; break;
        case LED_PRINTING:        g = b; blue = b; break; // cyan
        case LED_ERROR:           r = b; break;
    }
    led_strip_set_pixel(status_led, 0, r, g, blue);
    led_strip_refresh(status_led);
    xSemaphoreGive(led_mutex);
}

static void led_start(void)
{
#if CONFIG_ZP_RGB_LED_GPIO >= 0
    led_mutex = xSemaphoreCreateMutex();
    led_strip_config_t strip = {
        .strip_gpio_num = CONFIG_ZP_RGB_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };
    if (led_strip_new_rmt_device(&strip, &rmt, &status_led) == ESP_OK) {
        led_set(LED_WIFI_CONNECTING);
    } else {
        ESP_LOGW(TAG, "Unable to initialize RGB status LED");
    }
#endif
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        led_set(LED_WIFI_CONNECTING);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_READY);
        led_set(LED_WIFI_CONNECTING);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_events, WIFI_READY);
        led_set(printer_online ? LED_PRINTER_READY : LED_PRINTER_OFFLINE);
    }
}

static void wifi_start(void)
{
    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, CONFIG_ZP_HOSTNAME);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, CONFIG_ZP_WIFI_SSID, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, CONFIG_ZP_WIFI_PASSWORD, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

bool enqueue_copy(const uint8_t *data, size_t len)
{
    print_job_t job = { .data = malloc(len), .len = len };
    if (!job.data) return false;
    memcpy(job.data, data, len);
    if (xQueueSend(print_queue, &job, pdMS_TO_TICKS(5000)) != pdTRUE) {
        free(job.data);
        return false;
    }
    return true;
}

static esp_err_t health_get(httpd_req_t *req)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"printer\":\"%s\",\"queued_chunks\":%u}\n",
             printer_online ? "online" : "offline",
             (unsigned)uxQueueMessagesWaiting(print_queue));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t print_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > CONFIG_ZP_MAX_HTTP_JOB) {
        return httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Invalid job size");
    }
    uint8_t *data = malloc(req->content_len);
    if (!data) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, (char *)data + received, req->content_len - received);
        if (n <= 0) {
            free(data);
            return ESP_FAIL;
        }
        received += n;
    }
    print_job_t job = { .data = data, .len = received };
    if (xQueueSend(print_queue, &job, pdMS_TO_TICKS(5000)) != pdTRUE) {
        free(data);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "Print queue full\n");
    }
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_sendstr(req, "queued\n");
}

static void http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));
    httpd_uri_t health = { .uri = "/health", .method = HTTP_GET, .handler = health_get };
    httpd_uri_t print = { .uri = "/print", .method = HTTP_POST, .handler = print_post };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &health));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &print));
}

static void raw_server(void *arg)
{
    xEventGroupWaitBits(wifi_events, WIFI_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    uint8_t *buffer = malloc(USB_CHUNK);
    if (!buffer) {
        ESP_LOGE(TAG, "Unable to allocate raw TCP receive buffer");
        vTaskDelete(NULL);
        return;
    }
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_ZP_RAW_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    ESP_ERROR_CHECK(bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0 ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(listen(listener, 2) == 0 ? ESP_OK : ESP_FAIL);

    while (true) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) continue;
        int n;
        while ((n = recv(client, buffer, USB_CHUNK, 0)) > 0) {
            if (!enqueue_copy(buffer, n)) {
                ESP_LOGE(TAG, "Raw print queue full");
                break;
            }
        }
        shutdown(client, SHUT_RDWR);
        close(client);
    }
}

static void transfer_cb(usb_transfer_t *transfer)
{
    printer_t *p = transfer->context;
    p->transfer_status = transfer->status;
    p->transfer_done = true;
}

static bool find_printer_interface(const usb_config_desc_t *config, printer_t *p)
{
    const uint8_t *raw = (const uint8_t *)config;
    const uint16_t total = raw[2] | ((uint16_t)raw[3] << 8);
    bool in_printer_interface = false;
    for (uint16_t off = 0; off + 2 <= total && raw[off] >= 2; off += raw[off]) {
        uint8_t len = raw[off], type = raw[off + 1];
        if (off + len > total) break;
        if (type == USB_B_DESCRIPTOR_TYPE_INTERFACE && len >= 9) {
            in_printer_interface = (raw[off + 5] == 7); // USB Printer class
            if (in_printer_interface) {
                p->interface_number = raw[off + 2];
                p->alternate_setting = raw[off + 3];
                p->out_endpoint = 0;
            }
        } else if (in_printer_interface && type == USB_B_DESCRIPTOR_TYPE_ENDPOINT && len >= 7) {
            uint8_t ep = raw[off + 2], attrs = raw[off + 3] & 0x03;
            if (!(ep & 0x80) && attrs == 0x02) {
                p->out_endpoint = ep;
                return true;
            }
        }
    }
    return false;
}

static void client_event(const usb_host_client_event_msg_t *msg, void *arg)
{
    printer_t *p = arg;
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV && !p->device) {
        if (usb_host_device_open(p->client, msg->new_dev.address, &p->device) != ESP_OK) return;
        const usb_config_desc_t *config = NULL;
        if (usb_host_get_active_config_descriptor(p->device, &config) != ESP_OK ||
            !find_printer_interface(config, p) ||
            usb_host_interface_claim(p->client, p->device, p->interface_number,
                                     p->alternate_setting) != ESP_OK) {
            usb_host_device_close(p->client, p->device);
            p->device = NULL;
            return;
        }
        printer_online = true;
        led_set(LED_PRINTER_READY);
        ESP_LOGI(TAG, "USB printer ready: interface %u, endpoint 0x%02x",
                 p->interface_number, p->out_endpoint);
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE && msg->dev_gone.dev_hdl == p->device) {
        p->disconnected = true;
        printer_online = false;
        led_set(LED_PRINTER_OFFLINE);
    }
}

static esp_err_t usb_write(printer_t *p, const uint8_t *data, size_t len)
{
    usb_transfer_t *transfer = NULL;
    ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(len, 0, &transfer), TAG, "transfer alloc");
    memcpy(transfer->data_buffer, data, len);
    transfer->num_bytes = len;
    transfer->device_handle = p->device;
    transfer->bEndpointAddress = p->out_endpoint;
    transfer->callback = transfer_cb;
    transfer->context = p;
    p->transfer_done = false;
    esp_err_t err = usb_host_transfer_submit(transfer);
    while (err == ESP_OK && !p->transfer_done && !p->disconnected) {
        usb_host_client_handle_events(p->client, pdMS_TO_TICKS(100));
    }
    if (err == ESP_OK && (p->disconnected || p->transfer_status != USB_TRANSFER_STATUS_COMPLETED)) {
        err = ESP_FAIL;
    }
    usb_host_transfer_free(transfer);
    return err;
}

static void usb_daemon(void *arg)
{
    while (true) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

static void usb_printer(void *arg)
{
    printer_t p = { 0 };
    usb_host_client_config_t cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = { .client_event_callback = client_event, .callback_arg = &p },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&cfg, &p.client));

    while (true) {
        usb_host_client_handle_events(p.client, pdMS_TO_TICKS(20));
        if (p.disconnected && p.device) {
            usb_host_interface_release(p.client, p.device, p.interface_number);
            usb_host_device_close(p.client, p.device);
            p.device = NULL;
            p.disconnected = false;
        }
        print_job_t job;
        if (xQueueReceive(print_queue, &job, pdMS_TO_TICKS(20)) == pdTRUE) {
            led_set(LED_PRINTING);
            esp_err_t result = p.device ? ESP_OK : ESP_ERR_INVALID_STATE;
            for (size_t offset = 0; result == ESP_OK && offset < job.len; offset += USB_CHUNK) {
                size_t chunk = job.len - offset;
                if (chunk > USB_CHUNK) chunk = USB_CHUNK;
                result = usb_write(&p, job.data + offset, chunk);
            }
            if (result != ESP_OK) {
                ESP_LOGE(TAG, "Print job dropped: printer unavailable or USB error");
                led_set(LED_ERROR);
            } else {
                led_set(LED_PRINTER_READY);
            }
            free(job.data);
        }
    }
}

void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    print_queue = xQueueCreate(JOB_QUEUE_DEPTH, sizeof(print_job_t));
    led_start();
		ble_print_init();
    wifi_start();

    usb_host_config_t usb_cfg = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    ESP_ERROR_CHECK(usb_host_install(&usb_cfg));
    xTaskCreatePinnedToCore(usb_daemon, "usb_daemon", 4096, NULL, 20, NULL, 0);
    xTaskCreatePinnedToCore(usb_printer, "usb_printer", 6144, NULL, 19, NULL, 0);

    xEventGroupWaitBits(wifi_events, WIFI_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(CONFIG_ZP_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Zebra print server"));
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_pdl-datastream", "_tcp", CONFIG_ZP_RAW_PORT, NULL, 0));
    http_start();
    xTaskCreate(raw_server, "raw_9100", 6144, NULL, 8, NULL);
    ESP_LOGI(TAG, "Ready: http://%s.local/health and TCP %d", CONFIG_ZP_HOSTNAME, CONFIG_ZP_RAW_PORT);
}
