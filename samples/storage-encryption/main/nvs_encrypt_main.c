#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/efuse_reg.h"
#include "esp_efuse.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_flash_encrypt.h"
#include "esp_efuse_table.h"
#include "nvs.h"
#include "nvs_flash.h"

static void print_chip_info(void);
static void print_flash_encryption_status(void);

static const char *TAG = "storage-encryption";

#if CONFIG_IDF_TARGET_ESP32
#define TARGET_CRYPT_CNT_EFUSE ESP_EFUSE_FLASH_CRYPT_CNT
#define TARGET_CRYPT_CNT_WIDTH 7
#else
#define TARGET_CRYPT_CNT_EFUSE ESP_EFUSE_SPI_BOOT_CRYPT_CNT
#define TARGET_CRYPT_CNT_WIDTH 3
#endif

static void print_chip_info(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);

    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;

    printf("silicon revision v%d.%d, ", major_rev, minor_rev);

    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
    {
        printf("Get flash size failed");
        return;
    }

    printf("%" PRIu32 "MB %s flash\n", flash_size / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
}

static void print_flash_encryption_status(void)
{
    printf("\nChecking Flash Encryption Status\n");

    uint32_t flash_crypt_cnt = 0;
    esp_efuse_read_field_blob(TARGET_CRYPT_CNT_EFUSE, &flash_crypt_cnt, TARGET_CRYPT_CNT_WIDTH);
    printf("FLASH_CRYPT_CNT eFuse value is %" PRIu32 "\n", flash_crypt_cnt);

    esp_flash_enc_mode_t mode = esp_get_flash_encryption_mode();
    if (mode == ESP_FLASH_ENC_MODE_DISABLED)
    {
        printf("Flash encryption feature is disabled\n");
    }
    else
    {
        printf("Flash encryption feature is enabled in %s mode\n",
               mode == ESP_FLASH_ENC_MODE_DEVELOPMENT ? "DEVELOPMENT" : "RELEASE");
    }
}

/**
 * Helper function that loads a value from NVS.
 * It returns NULL when the value doesn't exist.
 */
char *nvs_load_value_if_exist(nvs_handle handle, const char *key)
{
    // Try to get the size of the item
    size_t value_size;
    if (nvs_get_str(handle, key, NULL, &value_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get size of key: %s", key);
        return NULL;
    }

    char *value = malloc(value_size);
    if (nvs_get_str(handle, key, value, &value_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load key: %s", key);
        return NULL;
    }

    return value;
}

esp_err_t nvs_secure_initialize()
{
    static const char *NVS_TAG = "nvs";
    esp_err_t err = ESP_OK;

    // 1. find partition with nvs_keys
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS,
                                                                "nvs_key");
    if (partition == NULL)
    {
        ESP_LOGE(NVS_TAG, "Could not locate nvs_key partition. Aborting.");
        return ESP_FAIL;
    }

    // 2. read nvs_keys from key partition
    nvs_sec_cfg_t cfg;
    if (ESP_OK != (err = nvs_flash_read_security_cfg(partition, &cfg)))
    {
        ESP_LOGE(NVS_TAG, "Failed to read nvs keys (rc=0x%x)", err);
        return err;
    }

    // 3. initialize nvs partition
    if (ESP_OK != (err = nvs_flash_secure_init(&cfg)))
    {
        ESP_LOGE(NVS_TAG, "failed to initialize nvs partition (err=0x%x). Aborting.", err);
        return err;
    };

    return err;
}

void app_main(void)
{
    print_chip_info();
    print_flash_encryption_status();

    esp_err_t err = nvs_secure_initialize();
    if (err != ESP_OK)
    {
        ESP_LOGE("main", "Failed to initialize nvs (rc=0x%x). Halting.", err);
        while (1)
        {
            vTaskDelay(100);
        }
    }

    // Open the "secrets" namespace in read-only mode
    nvs_handle handle;
    ESP_ERROR_CHECK(nvs_open("secrets", NVS_READONLY, &handle) != ESP_OK);

    // Load the private key & certificate
    ESP_LOGI(TAG, "Loading private key & certificate");
    char *private_key = nvs_load_value_if_exist(handle, "private_key");
    char *certificate = nvs_load_value_if_exist(handle, "claim_cert");

    // We're done with NVS
    nvs_close(handle);

    // Check if both items have been correctly retrieved
    if (private_key == NULL || certificate == NULL)
    {
        ESP_LOGE(TAG, "Private key or cert could not be loaded");
        return; // You might want to handle this in a better way
    }
    else
    {
        // We're printing the keys to the console for demonstration
        // purposes. This shows that NVS Encryption is actually working.
        // Don't do this in production code, obivously!
        ESP_LOGI(TAG, "Loaded private key & certificate successfully!");
        printf("Private Key:\n %s\n", private_key);
        printf("Claim Certificate:\n %s\n", certificate);
    }

    // At this point the private_key and claim_cert have been loaded.
    // Use them to exchange them for a claim certificate and proceed to
    // communicate with the LifeOmic Platform via MQTT.
}
