/*
 * A DNS server that answers every question with the setup AP's address, so a
 * phone that joins the AP is sent to the setup page by its captive-portal
 * check instead of having to know an address.
 */
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "captive";
static TaskHandle_t s_task;
static int s_sock = -1;

#define AP_IP_BYTES 192, 168, 4, 1

static void task(void *arg)
{
    (void)arg;
    uint8_t buf[512];
    struct sockaddr_in from;
    socklen_t from_len;
    while (s_sock >= 0) {
        from_len = sizeof(from);
        int n = recvfrom(s_sock, buf, sizeof(buf) - 16, 0, (struct sockaddr *)&from, &from_len);
        if (n < 12) {
            if (n < 0 && s_sock >= 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            continue;
        }
        /* Walk the first question to find its end. */
        int i = 12;
        while (i < n && buf[i]) {
            i += buf[i] + 1;
        }
        i += 5; /* the zero label, QTYPE, QCLASS */
        if (i > n) {
            continue;
        }
        uint16_t qtype = (uint16_t)((buf[i - 4] << 8) | buf[i - 3]);
        buf[2] = 0x81; /* response, recursion desired */
        buf[3] = 0x80; /* recursion available, no error */
        buf[4] = 0;
        buf[5] = 1; /* one question */
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        int len = i;
        if (qtype == 1) { /* A */
            buf[6] = 0;
            buf[7] = 1;
            uint8_t answer[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, AP_IP_BYTES};
            memcpy(buf + len, answer, sizeof(answer));
            len += sizeof(answer);
        } else {
            buf[6] = 0;
            buf[7] = 0;
        }
        sendto(s_sock, buf, len, 0, (struct sockaddr *)&from, from_len);
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t oh_captive_dns_start(void)
{
    if (s_sock >= 0) {
        return ESP_OK;
    }
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        return ESP_FAIL;
    }
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }
    struct timeval tv = {.tv_sec = 1};
    setsockopt(s_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (xTaskCreate(task, "captive_dns", 3072, NULL, 4, &s_task) != pdPASS) {
        close(s_sock);
        s_sock = -1;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "captive DNS up");
    return ESP_OK;
}

void oh_captive_dns_stop(void)
{
    if (s_sock < 0) {
        return;
    }
    int sock = s_sock;
    s_sock = -1;
    close(sock);
}
