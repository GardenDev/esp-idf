// Copyright 2015-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <string.h>
#include "sdkconfig.h"
#include "esp_rom_efuse.h"
#include "esp_system.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"

/* esp_system.h APIs relating to MAC addresses */

static const char* TAG = "system_api";

static uint8_t base_mac_addr[6] = { 0 };

esp_err_t esp_base_mac_addr_set(const uint8_t *mac)
{
    if (mac == NULL) {
        ESP_LOGE(TAG, "Base MAC address is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (mac[0] & 0x01) {
        ESP_LOGE(TAG, "Base MAC must be a unicast MAC");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(base_mac_addr, mac, 6);

    return ESP_OK;
}

esp_err_t esp_base_mac_addr_get(uint8_t *mac)
{
    uint8_t null_mac[6] = {0};

    if (memcmp(base_mac_addr, null_mac, 6) == 0) {
        ESP_LOGI(TAG, "Base MAC address is not set");
        return ESP_ERR_INVALID_MAC;
    }

    memcpy(mac, base_mac_addr, 6);

    return ESP_OK;
}

esp_err_t esp_efuse_mac_get_custom(uint8_t *mac)
{
#if !CONFIG_IDF_TARGET_ESP32
    size_t size_bits = esp_efuse_get_field_size(ESP_EFUSE_USER_DATA_MAC_CUSTOM);
    assert((size_bits % 8) == 0);
    esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA_MAC_CUSTOM, mac, size_bits);
    if (err != ESP_OK) {
        return err;
    }
    size_t size = size_bits / 8;
    if (mac[0] == 0 && memcmp(mac, &mac[1], size - 1) == 0) {
        ESP_LOGE(TAG, "eFuse MAC_CUSTOM is empty");
        return ESP_ERR_INVALID_MAC;
    }
#if (ESP_MAC_ADDRESS_LEN == 8)
    err = esp_efuse_read_field_blob(ESP_EFUSE_MAC_EXT, &mac[6], ESP_MAC_ADDRESS_LEN - size);
    if (err != ESP_OK) {
        return err;
    }
#endif
    return ESP_OK;
#else
    uint8_t version;
    esp_efuse_read_field_blob(ESP_EFUSE_MAC_CUSTOM_VER, &version, 8);
    if (version != 1) {
        ESP_LOGE(TAG, "Base MAC address from BLK3 of EFUSE version error, version = %d", version);
        return ESP_ERR_INVALID_VERSION;
    }

    uint8_t efuse_crc;
    esp_efuse_read_field_blob(ESP_EFUSE_MAC_CUSTOM, mac, 48);
    esp_efuse_read_field_blob(ESP_EFUSE_MAC_CUSTOM_CRC, &efuse_crc, 8);
    uint8_t calc_crc = esp_rom_efuse_mac_address_crc8(mac, 6);

    if (efuse_crc != calc_crc) {
        ESP_LOGE(TAG, "Base MAC address from BLK3 of EFUSE CRC error, efuse_crc = 0x%02x; calc_crc = 0x%02x", efuse_crc, calc_crc);
#ifdef CONFIG_ESP_MAC_IGNORE_MAC_CRC_ERROR
        ESP_LOGW(TAG, "Ignore MAC CRC error");
#else
        return ESP_ERR_INVALID_CRC;
#endif
    }
    return ESP_OK;
#endif
}

esp_err_t esp_efuse_mac_get_default(uint8_t* mac)
{
    esp_err_t err = esp_efuse_read_field_blob(ESP_EFUSE_MAC_FACTORY, mac, 48);
    if (err != ESP_OK) {
        return err;
    }
#ifdef CONFIG_IDF_TARGET_ESP32
// Only ESP32 has MAC CRC in efuse
    uint8_t efuse_crc;
    esp_efuse_read_field_blob(ESP_EFUSE_MAC_FACTORY_CRC, &efuse_crc, 8);
    uint8_t calc_crc = esp_rom_efuse_mac_address_crc8(mac, 6);

    if (efuse_crc != calc_crc) {
         // Small range of MAC addresses are accepted even if CRC is invalid.
         // These addresses are reserved for Espressif internal use.
        uint32_t mac_high = ((uint32_t)mac[0] << 8) | mac[1];
        uint32_t mac_low = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
        if (((mac_high & 0xFFFF) == 0x18fe) && (mac_low >= 0x346a85c7) && (mac_low <= 0x346a85f8)) {
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Base MAC address from BLK0 of EFUSE CRC error, efuse_crc = 0x%02x; calc_crc = 0x%02x", efuse_crc, calc_crc);
#ifdef CONFIG_ESP_MAC_IGNORE_MAC_CRC_ERROR
            ESP_LOGW(TAG, "Ignore MAC CRC error");
#else
            return ESP_ERR_INVALID_CRC;
#endif
        }
    }
#endif // CONFIG_IDF_TARGET_ESP32
    return ESP_OK;
}

esp_err_t esp_derive_local_mac(uint8_t* local_mac, const uint8_t* universal_mac)
{
    uint8_t idx;

    if (local_mac == NULL || universal_mac == NULL) {
        ESP_LOGE(TAG, "mac address param is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(local_mac, universal_mac, 6);
    for (idx = 0; idx < 64; idx++) {
        local_mac[0] = universal_mac[0] | 0x02;
        local_mac[0] ^= idx << 2;

        if (memcmp(local_mac, universal_mac, 6)) {
            break;
        }
    }

    return ESP_OK;
}

esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t type)
{
    uint8_t efuse_mac[6];

    if (mac == NULL) {
        ESP_LOGE(TAG, "mac address param is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (type < ESP_MAC_WIFI_STA || type > ESP_MAC_ETH) {
        ESP_LOGE(TAG, "mac type is incorrect");
        return ESP_ERR_INVALID_ARG;
    }

    // if base mac address is not set, read one from EFUSE and then write back
    if (esp_base_mac_addr_get(efuse_mac) != ESP_OK) {
        ESP_LOGI(TAG, "read default base MAC address from EFUSE");
        esp_efuse_mac_get_default(efuse_mac);
        esp_base_mac_addr_set(efuse_mac);
    }

    switch (type) {
    case ESP_MAC_WIFI_STA:
        memcpy(mac, efuse_mac, 6);
        break;
    case ESP_MAC_WIFI_SOFTAP:
#if CONFIG_ESP_MAC_ADDR_UNIVERSE_WIFI_AP
        memcpy(mac, efuse_mac, 6);
    // as a result of some esp32s2 chips burned with one MAC address by mistake,
    // there are some MAC address are reserved for this bug fix.
    // related mistake MAC address is 0x7cdfa1003000~0x7cdfa1005fff,
    // reserved MAC address is 0x7cdfa1020000~0x7cdfa1022fff (MAC address + 0x1d000).
#ifdef CONFIG_IDF_TARGET_ESP32S2
        uint8_t mac_begin[6] = { 0x7c, 0xdf, 0xa1, 0x00, 0x30, 0x00 };
        uint8_t mac_end[6]   = { 0x7c, 0xdf, 0xa1, 0x00, 0x5f, 0xff };
        if(memcmp(mac,mac_begin,6) >= 0 && memcmp(mac_end,mac,6) >=0 ){
            mac[3] += 0x02; // contain carry bit
            mac[4] += 0xd0;
        } else {
            mac[5] += 1;
        }
#else
        mac[5] += 1;
#endif // IDF_TARGET_ESP32S2
#else
        esp_derive_local_mac(mac, efuse_mac);
#endif
        break;
    case ESP_MAC_BT:
#if CONFIG_ESP_MAC_ADDR_UNIVERSE_BT
        memcpy(mac, efuse_mac, 6);
        mac[5] += CONFIG_ESP_MAC_ADDR_UNIVERSE_BT_OFFSET;
#endif
        break;
    case ESP_MAC_ETH:
#if CONFIG_ESP_MAC_ADDR_UNIVERSE_ETH
        memcpy(mac, efuse_mac, 6);
        mac[5] += 3;
#else
        efuse_mac[5] += 1;
        esp_derive_local_mac(mac, efuse_mac);
#endif
        break;
    default:
        ESP_LOGE(TAG, "unsupported mac type");
        break;
    }

    return ESP_OK;
}
