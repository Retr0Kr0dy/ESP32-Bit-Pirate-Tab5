/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_hosted.h"

#include "port_esp_hosted_host_log.h"

#include "esp_private/startup_internal.h"

DEFINE_LOG_TAG(host_init);

/*
 * LOCAL PATCH (Tab5 Bit Pirate port, not upstream) — the stock constructor
 * below runs esp_hosted_init() before app_main()/the FreeRTOS scheduler even
 * exists, which crashes this board's boot outright (confirmed: the very
 * first xTaskCreatePinnedToCore() in esp_startup_start_app() fails because
 * esp_hosted's pre-scheduler allocations/queues have already disturbed
 * startup). Tab5-Orion (tab5_ref/Tab5-Orion/main/wifi.c) hit and fixed the
 * exact same problem the same way: disable the automatic constructor and
 * call esp_hosted_init() explicitly, later, the first time Wi-Fi is actually
 * requested (lazy init). This file lives under managed_components/ and will
 * be silently overwritten by a fresh `idf.py reconfigure`/component
 * re-fetch -- if Wi-Fi/AP mode starts crashing boot again after one, re-apply
 * this same patch (disable the constructor attribute below) before
 * suspecting anything else.
 */
//ESP_SYSTEM_INIT_FN(esp_hosted_host_init, BIT(0), 120)
void esp_hosted_host_init_lazy(void)
{
	ESP_LOGI(TAG, "ESP Hosted : Host chip_ip[%d]", CONFIG_IDF_FIRMWARE_CHIP_ID);
	ESP_ERROR_CHECK(esp_hosted_init());
}

static void __attribute__((destructor)) esp_hosted_host_deinit(void)
{
	ESP_LOGI(TAG, "ESP Hosted deinit");
	esp_hosted_deinit();
}
