/**
 * @file main_rmt.c
 * @brief ESP32 Double Pulse Test (DPT) Signal Generator
 * 
 * WiFi-controlled high-precision double pulse test signal generator
 * for power electronics testing using ESP32 RMT peripheral.
 * 
 * @author Yang Xie (yang.xie.2@stonybrook.edu)
 * @date 2024
 * @version 1.0
 * 
 * Features:
 * - WiFi access point with web interface
 * - Complementary signal outputs (GPIO 7/8)
 * - Nanosecond-precision timing
 * - Real-time parameter configuration
 * - Hardware button trigger support
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/rmt.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"

#define TAG "DPT_SYSTEM"


// ---------------------- WiFi Configuration ----------------------
#define WIFI_SSID "dpt_test"
#define WIFI_PASS "12345678"
#define WIFI_CHANNEL 1
#define MAX_STA_CONN 4

// ---------------------- RMT Configuration ----------------------
// Define two RMT channels
#define RMT_TX_CHANNEL_P    RMT_CHANNEL_0    // Positive signal channel
#define RMT_TX_CHANNEL_N    RMT_CHANNEL_1    // Negative signal channel
#define RMT_TX_GPIO_P       7                // Positive signal GPIO
#define RMT_TX_GPIO_N       8                // Negative signal GPIO
#define RMT_CLK_DIV         1               // 80MHz / 80 = 1μs, 1=12.5ns

// Double Pulse parameters (default values)
static uint32_t pulse1_high = 5;
static uint32_t pulse1_low = 1;
static uint32_t pulse2_high = 3;
static uint32_t pulse2_low = 10000;

// Function declarations
void send_double_pulse(void);
static void setup_rmt(void);

// ---------------------- Button Interrupt ----------------------
#define BUTTON_GPIO       0  // Boot button

static QueueHandle_t button_evt_queue = NULL;

static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)arg;
    gpio_intr_disable(gpio_num);  // Disable interrupt
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(button_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ---------------------- Task: Handle Button Events ----------------------
void button_event_task(void *arg) {
    uint32_t io_num;
    while (1) {
        if (xQueueReceive(button_evt_queue, &io_num, portMAX_DELAY)) {
            ESP_LOGI(TAG, "Button pressed! Triggering DPT...");
            vTaskDelay(pdMS_TO_TICKS(1000));  // Wait 1 second
            send_double_pulse();
            vTaskDelay(pdMS_TO_TICKS(200));  // Prevent bouncing
            gpio_intr_enable(io_num);  // Re-enable interrupt
        }
    }
}

// ---------------------- WiFi AP Configuration ----------------------
void wifi_init_softap(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = { .ssid = WIFI_SSID, .ssid_len = strlen(WIFI_SSID), .password = WIFI_PASS, .channel = WIFI_CHANNEL,
                .max_connection = MAX_STA_CONN, .authmode = WIFI_AUTH_WPA_WPA2_PSK }
    };
    if (strlen(WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s", WIFI_SSID, WIFI_PASS);
}

// ---------------------- HTTP Server ----------------------
// static esp_err_t get_handler(httpd_req_t *req) {
//     char response[1024];
//     int len = snprintf(response, sizeof(response),
//         "<html><body>"
//         "<h2>Set Double Pulse Parameters</h2>"
//         "<form action='/set' method='post'>"
//         "Pulse 1 High (us): <input type='number' name='p1h' value='%lu'><br>"
//         "Pulse 1 Low (us): <input type='number' name='p1l' value='%lu'><br>"
//         "Pulse 2 High (us): <input type='number' name='p2h' value='%lu'><br>"
//         "Pulse 2 Low (us): <input type='number' name='p2l' value='%lu'><br>"
//         "<input type='submit' value='Set'>"
//         "</form>"
//         "<form action='/trigger' method='get'>"
//         "<input type='submit' value='Trigger DPT'>"
//         "</form></body></html>",
//         (unsigned long)pulse1_high, (unsigned long)pulse1_low, 
//         (unsigned long)pulse2_high, (unsigned long)pulse2_low
//     );

//     // Check if `snprintf()` exceeds buffer
//     if (len < 0 || len >= sizeof(response)) {
//         ESP_LOGE(TAG, "Response buffer overflow! Length required: %d, Buffer size: %d", len, sizeof(response));
//         httpd_resp_send(req, "Error: Response too long!", HTTPD_RESP_USE_STRLEN);
//         return ESP_FAIL;
//     }
//     httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
//     return ESP_OK;
// }

static esp_err_t get_handler(httpd_req_t *req) {
    char response[4096];  // Increase buffer to 4096 bytes
    int len = snprintf(response, sizeof(response),
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Double Pulse Test</title>"
        "<style>"
        "  body {"
        "    font-family: Arial, sans-serif;"
        "    margin: 0;"
        "    padding: 20px;"
        "    background-color: #f4f4f4;"
        "  }"
        "  .container {"
        "    max-width: 400px;"
        "    margin: 0 auto;"
        "    background: #fff;"
        "    padding: 20px;"
        "    border-radius: 8px;"
        "    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);"
        "  }"
        "  h2 {"
        "    text-align: center;"
        "    color: #333;"
        "  }"
        "  .form-group {"
        "    margin-bottom: 15px;"
        "  }"
        "  .form-group label {"
        "    display: block;"
        "    margin-bottom: 5px;"
        "    font-weight: bold;"
        "  }"
        "  .form-group input {"
        "    width: 100%%;"
        "    padding: 8px;"
        "    box-sizing: border-box;"
        "    border: 1px solid #ccc;"
        "    border-radius: 4px;"
        "  }"
        "  .form-group input[type='submit'] {"
        "    background-color: #007bff;"
        "    color: white;"
        "    border: none;"
        "    cursor: pointer;"
        "  }"
        "  .form-group input[type='submit']:hover {"
        "    background-color: #0056b3;"
        "  }"
        "  .message {"
        "    margin-top: 20px;"
        "    padding: 10px;"
        "    background-color: #d4edda;"
        "    color: #155724;"
        "    border: 1px solid #c3e6cb;"
        "    border-radius: 4px;"
        "    display: none;"
        "  }"
        "  .error {"
        "    background-color: #f8d7da;"
        "    color: #721c24;"
        "    border: 1px solid #f5c6cb;"
        "  }"
        "</style>"
        "<script>"
        "  async function submitForm(event) {"
        "    event.preventDefault();"  // 阻止表单默认提交行为
        "    const form = event.target;"
        "    const formData = new FormData(form);"
        "    const response = await fetch('/set', {"
        "      method: 'POST',"
        "      body: new URLSearchParams(formData)"
        "    });"
        "    const message = document.getElementById('message');"
        "    if (response.ok) {"
        "      message.textContent = 'Parameters set successfully!';"
        "      message.className = 'message';"
        "      message.style.display = 'block';"
        "      setTimeout(() => {"
        "        message.style.display = 'none';"
        "      }, 3000);"  // 3秒后隐藏提示
        "    } else {"
        "      message.textContent = 'Failed to set parameters!';"
        "      message.className = 'message error';"
        "      message.style.display = 'block';"
        "    }"
        "  }"
        "  async function triggerDPT(event) {"
        "    event.preventDefault();"  // 阻止表单默认提交行为
        "    const response = await fetch('/trigger', {"
        "      method: 'GET'"
        "    });"
        "    const message = document.getElementById('message');"
        "    if (response.ok) {"
        "      message.textContent = 'DPT triggered successfully!';"
        "      message.className = 'message';"
        "      message.style.display = 'block';"
        "      setTimeout(() => {"
        "        message.style.display = 'none';"
        "      }, 3000);"  // 3秒后隐藏提示
        "    } else {"
        "      message.textContent = 'Failed to trigger DPT!';"
        "      message.className = 'message error';"
        "      message.style.display = 'block';"
        "    }"
        "  }"
        "</script>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<h2>Double Pulse Test</h2>"
        "<div id='message' class='message'></div>"
        "<form onsubmit='submitForm(event)'>"
        "<div class='form-group'>"
        "<label for='p1h'>Pulse 1 High (us):</label>"
        "<input type='number' id='p1h' name='p1h' value='%lu'>"
        "</div>"
        "<div class='form-group'>"
        "<label for='p1l'>Pulse 1 Low (us):</label>"
        "<input type='number' id='p1l' name='p1l' value='%lu'>"
        "</div>"
        "<div class='form-group'>"
        "<label for='p2h'>Pulse 2 High (us):</label>"
        "<input type='number' id='p2h' name='p2h' value='%lu'>"
        "</div>"
        "<div class='form-group'>"
        "<label for='p2l'>Pulse 2 Low (us):</label>"
        "<input type='number' id='p2l' name='p2l' value='%lu'>"
        "</div>"
        "<div class='form-group'>"
        "<input type='submit' value='Set'>"
        "</div>"
        "</form>"
        "<form onsubmit='triggerDPT(event)'>"
        "<div class='form-group'>"
        "<input type='submit' value='Trigger DPT'>"
        "</div>"
        "<div>Designed by Yang</div>"
        "</form>"
        "</div>"
        "</body>"
        "</html>",
        (unsigned long)pulse1_high, (unsigned long)pulse1_low, 
        (unsigned long)pulse2_high, (unsigned long)pulse2_low
    );

    // Check if `snprintf()` exceeds buffer
    if (len < 0 || len >= sizeof(response)) {
        ESP_LOGE(TAG, "Response buffer overflow! Length required: %d, Buffer size: %d", len, sizeof(response));
        httpd_resp_send(req, "Error: Response too long!", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t set_params_handler(httpd_req_t *req) {
    char content[2048];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        ESP_LOGW(TAG, "Failed to receive request body");
        return ESP_FAIL;
    }
    content[ret] = '\0';  // Ensure string termination
    ESP_LOGI(TAG, "Received POST data: %s", content);

    char param_val[10];
    if (httpd_query_key_value(content, "p1h", param_val, sizeof(param_val)) == ESP_OK) {
        pulse1_high = atoi(param_val);
    }
    if (httpd_query_key_value(content, "p1l", param_val, sizeof(param_val)) == ESP_OK) {
        pulse1_low = atoi(param_val);
    }
    if (httpd_query_key_value(content, "p2h", param_val, sizeof(param_val)) == ESP_OK) {
        pulse2_high = atoi(param_val);
    }
    if (httpd_query_key_value(content, "p2l", param_val, sizeof(param_val)) == ESP_OK) {
        pulse2_low = atoi(param_val);
    }

    ESP_LOGI(TAG, "Updated parameters: p1h=%lu, p1l=%lu, p2h=%lu, p2l=%lu", 
         (unsigned long) pulse1_high, 
         (unsigned long) pulse1_low, 
         (unsigned long) pulse2_high, 
         (unsigned long) pulse2_low);
    httpd_resp_send(req, "Parameters Set!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t trigger_handler(httpd_req_t *req) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    send_double_pulse();
    httpd_resp_send(req, "Triggered!", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req) {
    httpd_resp_send_404(req);
    return ESP_OK;
}

httpd_uri_t uri_favicon = {
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = favicon_handler
};

httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = get_handler };
httpd_uri_t uri_set = { .uri = "/set", .method = HTTP_POST, .handler = set_params_handler };
httpd_uri_t uri_trigger = { .uri = "/trigger", .method = HTTP_GET, .handler = trigger_handler };

httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;   // Maximum number of URI handlers
    config.stack_size = 10240;      // Task stack size
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_set);
        httpd_register_uri_handler(server, &uri_trigger);
        httpd_register_uri_handler(server, &uri_favicon);
    }
    return server;
}

// ✅ Add setup_rmt function definition
static void setup_rmt(void) {
    // Configure positive channel
    rmt_config_t rmt_tx_config_p = {
        .rmt_mode = RMT_MODE_TX,
        .channel = RMT_TX_CHANNEL_P,
        .gpio_num = RMT_TX_GPIO_P,
        .clk_div = RMT_CLK_DIV,
        .mem_block_num = 1,
        .tx_config = {
            .loop_en = false,
            .carrier_en = false,
            .idle_output_en = true,
            .idle_level = RMT_IDLE_LEVEL_LOW,
        }
    };

    // Configure negative channel
    rmt_config_t rmt_tx_config_n = {
        .rmt_mode = RMT_MODE_TX,
        .channel = RMT_TX_CHANNEL_N,
        .gpio_num = RMT_TX_GPIO_N,
        .clk_div = RMT_CLK_DIV,
        .mem_block_num = 1,
        .tx_config = {
            .loop_en = false,
            .carrier_en = false,
            .idle_output_en = true,
            .idle_level = RMT_IDLE_LEVEL_HIGH,  // Set idle level to high for negative channel
        }
    };

    // Configure both channels
    ESP_ERROR_CHECK(rmt_config(&rmt_tx_config_p));
    ESP_ERROR_CHECK(rmt_config(&rmt_tx_config_n));

    // Install RMT driver
    ESP_ERROR_CHECK(rmt_driver_install(RMT_TX_CHANNEL_P, 0, 0));
    ESP_ERROR_CHECK(rmt_driver_install(RMT_TX_CHANNEL_N, 0, 0));

    ESP_LOGI(TAG, "RMT TX channels configured successfully");
}

// Modified send function with optimized timing control
void send_double_pulse(void) {
    // Convert time units to count values under new clock division
    uint32_t p1h = pulse1_high * 80;  // Convert to new clock division count value
    uint32_t p1l = pulse1_low * 80;
    uint32_t p2h = pulse2_high * 80;
    uint32_t p2l = pulse2_low * 80;

    // Positive signal items
    rmt_item32_t double_pulse_items_p[] = {
        { .duration0 = p1h, .level0 = 1, .duration1 = p1l, .level1 = 0 },
        { .duration0 = p2h, .level0 = 1, .duration1 = p2l, .level1 = 0 }
    };

    // Negative signal items (inverted levels)
    rmt_item32_t double_pulse_items_n[] = {
        { .duration0 = p1h, .level0 = 0, .duration1 = p1l, .level1 = 1 },  
        { .duration0 = p2h, .level0 = 0, .duration1 = p2l, .level1 = 1 }   
    };

    // Clear previous data
    rmt_tx_stop(RMT_TX_CHANNEL_P);
    rmt_tx_stop(RMT_TX_CHANNEL_N);

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    
    // Prepare data but do not send immediately
    ESP_ERROR_CHECK(rmt_write_items(RMT_TX_CHANNEL_P, double_pulse_items_p, 2, false)); 
    ESP_ERROR_CHECK(rmt_write_items(RMT_TX_CHANNEL_N, double_pulse_items_n, 2, false)); 
    
    // Ensure both channels are ready
    vTaskDelay(50);
    // Synchronously start both channels
    portENTER_CRITICAL(&mux);
    rmt_tx_start(RMT_TX_CHANNEL_P, true);  // Start positive channel second
    rmt_tx_start(RMT_TX_CHANNEL_N, true);  // Start negative channel first
    portEXIT_CRITICAL(&mux);

    // Wait for transmission completion
    rmt_wait_tx_done(RMT_TX_CHANNEL_P, portMAX_DELAY);
    rmt_wait_tx_done(RMT_TX_CHANNEL_N, portMAX_DELAY);

    ESP_LOGI(TAG, "Complementary double pulse sent");
}
// ---------------------- Button Interrupt Configuration ----------------------
void setup_button_interrupt(void)
{
    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    io_conf.intr_type = GPIO_INTR_NEGEDGE;      // Falling edge trigger
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUTTON_GPIO);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // Create queue for passing button interrupt events
    button_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    // Install GPIO interrupt service
    gpio_install_isr_service(0);
    // Add interrupt handler for GPIO0
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, (void *)BUTTON_GPIO);

    ESP_LOGI(TAG, "Button configured on GPIO%d (interrupt mode)", BUTTON_GPIO);
}

// ---------------------- Main Task ----------------------
void app_main(void) {
    ESP_LOGI(TAG, "Starting DPT System...");

    wifi_init_softap();
    start_webserver();

    // Configure RMT TX channels
    setup_rmt();

    // Configure button interrupt
    setup_button_interrupt();

    xTaskCreate(button_event_task, "button_event_task", 4096, NULL, 10, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}