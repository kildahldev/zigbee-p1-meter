#include "zigbee_device.h"
#include "ota.h"
#include "led.h"
#include "esp_zigbee_core.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "zigbee";
static volatile bool s_connected = false;

static void bdb_start_steering(uint8_t param)
{
    (void)param;
    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
}

bool zigbee_is_connected(void)
{
    return s_connected;
}

/* ---------- Attribute update helpers ---------- */

static void set_attr_and_report(uint8_t endpoint, uint16_t cluster_id,
                                uint16_t attr_id, void *value)
{
    esp_zb_zcl_set_attribute_val(endpoint, cluster_id,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  attr_id, value, false);

    esp_zb_zcl_report_attr_cmd_t cmd = {
        .zcl_basic_cmd.src_endpoint = endpoint,
        .address_mode = ESP_ZB_APS_ADDR_MODE_DST_ADDR_ENDP_NOT_PRESENT,
        .clusterID = cluster_id,
        .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
        .attributeID = attr_id,
    };
    esp_zb_zcl_report_attr_cmd_req(&cmd);
}

void zigbee_update_attributes(const zigbee_sensor_data_t *data)
{
    esp_zb_lock_acquire(portMAX_DELAY);

    uint16_t cluster = ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT;

    // Voltage L1/L2/L3
    uint16_t v;
    v = data->voltage_l1;
    set_attr_and_report(ZB_ENDPOINT, cluster,
                        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSVOLTAGE_ID, &v);
    v = data->voltage_l2;
    set_attr_and_report(ZB_ENDPOINT, cluster,
                        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSVOLTAGE_PHB_ID, &v);
    v = data->voltage_l3;
    set_attr_and_report(ZB_ENDPOINT, cluster,
                        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSVOLTAGE_PHC_ID, &v);

    // Current L1/L2/L3
    v = data->current_l1;
    set_attr_and_report(ZB_ENDPOINT, cluster,
                        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSCURRENT_ID, &v);
    v = data->current_l2;
    set_attr_and_report(ZB_ENDPOINT, cluster,
                        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSCURRENT_PHB_ID, &v);
    v = data->current_l3;
    set_attr_and_report(ZB_ENDPOINT, cluster,
                        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSCURRENT_PHC_ID, &v);

    // Active Power L1/L2/L3
    int16_t p;
    p = data->power_l1;
    set_attr_and_report(ZB_ENDPOINT, cluster,
                        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_ID, &p);
    p = data->power_l2;
    set_attr_and_report(ZB_ENDPOINT, cluster,
                        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_PHB_ID, &p);
    p = data->power_l3;
    set_attr_and_report(ZB_ENDPOINT, cluster,
                        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_PHC_ID, &p);

    // Total active power
    int32_t total = data->power_total;
    set_attr_and_report(ZB_ENDPOINT, cluster,
                        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_TOTAL_ACTIVE_POWER_ID, &total);

    // Metering: energy delivered/received (uint48 = 6 bytes, low 48 bits of uint64)
    uint16_t met_cluster = ESP_ZB_ZCL_CLUSTER_ID_METERING;

    esp_zb_uint48_t delivered = {
        .low = (uint32_t)(data->energy_delivered & 0xFFFFFFFF),
        .high = (uint16_t)((data->energy_delivered >> 32) & 0xFFFF),
    };
    set_attr_and_report(ZB_ENDPOINT, met_cluster,
                        ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID,
                        &delivered);

    esp_zb_uint48_t received = {
        .low = (uint32_t)(data->energy_received & 0xFFFFFFFF),
        .high = (uint16_t)((data->energy_received >> 32) & 0xFFFF),
    };
    set_attr_and_report(ZB_ENDPOINT, met_cluster,
                        ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_RECEIVED_ID,
                        &received);

    esp_zb_lock_release();
}

/* ---------- Signal handler ---------- */

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Factory new device, starting network steering");
                led_set_state(LED_STATE_PAIRING);
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted, already in network");
                s_connected = true;
                led_set_state(LED_STATE_CONNECTED);
                ota_mark_valid();
            }
        } else {
            ESP_LOGW(TAG, "Device start failed (0x%x), steering", err_status);
            led_set_state(LED_STATE_PAIRING);
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t pan_id;
            esp_zb_get_extended_pan_id(pan_id);
            ESP_LOGI(TAG, "Joined network successfully (PAN: %02x%02x%02x%02x%02x%02x%02x%02x)",
                     pan_id[7], pan_id[6], pan_id[5], pan_id[4],
                     pan_id[3], pan_id[2], pan_id[1], pan_id[0]);
            s_connected = true;
            led_set_state(LED_STATE_CONNECTED);
        } else {
            ESP_LOGW(TAG, "Network steering failed (0x%x), retrying in 1s", err_status);
            esp_zb_scheduler_alarm(bdb_start_steering, 0, 1000);
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        s_connected = false;
        led_set_state(LED_STATE_PAIRING);
        ESP_LOGI(TAG, "Left network, restarting steering");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        break;

    default:
        ESP_LOGD(TAG, "ZDO signal: 0x%x, status: 0x%x", sig_type, err_status);
        break;
    }
}

/* ---------- Action handler ---------- */

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id,
                                    const void *message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_OTA_UPGRADE_VALUE_CB_ID:
        return ota_upgrade_status_handler(
            (const esp_zb_zcl_ota_upgrade_value_message_t *)message);
    case ESP_ZB_CORE_OTA_UPGRADE_QUERY_IMAGE_RESP_CB_ID: {
        esp_zb_zcl_ota_upgrade_query_image_resp_message_t *resp =
            (esp_zb_zcl_ota_upgrade_query_image_resp_message_t *)message;
        ESP_LOGI(TAG, "OTA query image response: status=%d, version=0x%lx, size=%lu",
                 resp->query_status, (unsigned long)resp->file_version,
                 (unsigned long)resp->image_size);
        break;
    }
    default:
        ESP_LOGD(TAG, "Unhandled action callback: 0x%x", callback_id);
        break;
    }
    return ESP_OK;
}

/* ---------- Cluster/endpoint creation ---------- */

static esp_zb_cluster_list_t *create_cluster_list(void)
{
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    /* Basic cluster */
    esp_zb_attribute_list_t *basic = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BASIC);
    uint8_t zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE;
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID, &zcl_version);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                   MANUFACTURER_NAME);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                   MODEL_IDENTIFIER);
    uint8_t power_source = 0x01; // Mains single phase
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_POWER_SOURCE_ID, &power_source);

    // Software build string for firmware version tracking (ZCL string: length prefix + data)
    char sw_build[1 + sizeof(FW_VERSION_STRING)];
    sw_build[0] = sizeof(FW_VERSION_STRING) - 1;
    memcpy(sw_build + 1, FW_VERSION_STRING, sizeof(FW_VERSION_STRING) - 1);
    esp_zb_basic_cluster_add_attr(basic, ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID, sw_build);

    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic,
                                           ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Identify cluster */
    esp_zb_attribute_list_t *identify = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY);
    uint16_t identify_time = 0;
    esp_zb_identify_cluster_add_attr(identify, ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID,
                                      &identify_time);
    esp_zb_cluster_list_add_identify_cluster(cluster_list, identify,
                                              ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Electrical Measurement cluster (0x0B04) */
    esp_zb_attribute_list_t *elec =
        esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT);

    uint32_t meas_type = 0x00000008; // AC (active) measurement
    esp_zb_electrical_meas_cluster_add_attr(elec,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_MEASUREMENT_TYPE_ID, &meas_type);

    // Multiplier/Divisor for voltage: report in 0.1V, Z2M divides by 10
    uint16_t ac_voltage_mult = 1;
    uint16_t ac_voltage_div = 10;
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACVOLTAGE_MULTIPLIER_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &ac_voltage_mult);
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACVOLTAGE_DIVISOR_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &ac_voltage_div);

    // Multiplier/Divisor for current: report in mA, Z2M divides by 1000
    uint16_t ac_current_mult = 1;
    uint16_t ac_current_div = 1000;
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACCURRENT_MULTIPLIER_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &ac_current_mult);
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACCURRENT_DIVISOR_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &ac_current_div);

    // Multiplier/Divisor for power: report in W
    uint16_t ac_power_mult = 1;
    uint16_t ac_power_div = 1;
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACPOWER_MULTIPLIER_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &ac_power_mult);
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACPOWER_DIVISOR_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &ac_power_div);

    // Phase A attributes (reportable)
    uint16_t zero16 = 0;
    int16_t zero16s = 0;
    uint8_t rpt = ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING;

    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSVOLTAGE_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, rpt, &zero16);
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSCURRENT_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, rpt, &zero16);
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_ID,
        ESP_ZB_ZCL_ATTR_TYPE_S16, rpt, &zero16s);

    // Phase B attributes (reportable)
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSVOLTAGE_PHB_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, rpt, &zero16);
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSCURRENT_PHB_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, rpt, &zero16);
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_PHB_ID,
        ESP_ZB_ZCL_ATTR_TYPE_S16, rpt, &zero16s);

    // Phase C attributes (reportable)
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSVOLTAGE_PHC_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, rpt, &zero16);
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_RMSCURRENT_PHC_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U16, rpt, &zero16);
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_ACTIVE_POWER_PHC_ID,
        ESP_ZB_ZCL_ATTR_TYPE_S16, rpt, &zero16s);

    // Total active power (reportable)
    int32_t zero32s = 0;
    esp_zb_cluster_add_attr(elec, ESP_ZB_ZCL_CLUSTER_ID_ELECTRICAL_MEASUREMENT,
        ESP_ZB_ZCL_ATTR_ELECTRICAL_MEASUREMENT_TOTAL_ACTIVE_POWER_ID,
        ESP_ZB_ZCL_ATTR_TYPE_S32, rpt, &zero32s);

    esp_zb_cluster_list_add_electrical_meas_cluster(cluster_list, elec,
                                                     ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Metering cluster (0x0702) — build manually for reportable attributes */
    esp_zb_attribute_list_t *metering =
        esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_METERING);

    esp_zb_uint48_t zero48 = {0};
    // CurrentSummationDelivered — must be reportable
    esp_zb_cluster_add_attr(metering, ESP_ZB_ZCL_CLUSTER_ID_METERING,
        ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_DELIVERED_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U48,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
        &zero48);
    // CurrentSummationReceived — must be reportable
    esp_zb_cluster_add_attr(metering, ESP_ZB_ZCL_CLUSTER_ID_METERING,
        ESP_ZB_ZCL_ATTR_METERING_CURRENT_SUMMATION_RECEIVED_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U48,
        ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING,
        &zero48);

    // Status
    uint8_t metering_status = 0x00;
    esp_zb_cluster_add_attr(metering, ESP_ZB_ZCL_CLUSTER_ID_METERING,
        ESP_ZB_ZCL_ATTR_METERING_STATUS_ID,
        ESP_ZB_ZCL_ATTR_TYPE_8BITMAP, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
        &metering_status);
    // Unit of measure: kWh
    uint8_t unit_of_measure = 0x00;
    esp_zb_cluster_add_attr(metering, ESP_ZB_ZCL_CLUSTER_ID_METERING,
        ESP_ZB_ZCL_ATTR_METERING_UNIT_OF_MEASURE_ID,
        ESP_ZB_ZCL_ATTR_TYPE_8BIT_ENUM, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
        &unit_of_measure);
    // Summation formatting
    uint8_t summ_fmt = ESP_ZB_ZCL_METERING_FORMATTING_SET(true, 3, 5);
    esp_zb_cluster_add_attr(metering, ESP_ZB_ZCL_CLUSTER_ID_METERING,
        ESP_ZB_ZCL_ATTR_METERING_SUMMATION_FORMATTING_ID,
        ESP_ZB_ZCL_ATTR_TYPE_8BITMAP, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
        &summ_fmt);
    // Device type: electric
    uint8_t dev_type = 0x00;
    esp_zb_cluster_add_attr(metering, ESP_ZB_ZCL_CLUSTER_ID_METERING,
        ESP_ZB_ZCL_ATTR_METERING_METERING_DEVICE_TYPE_ID,
        ESP_ZB_ZCL_ATTR_TYPE_8BITMAP, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY,
        &dev_type);

    // Multiplier and divisor: energy in Wh, divisor 1000 → kWh display
    uint32_t mult24 = 1;
    uint32_t div24 = 1000;
    esp_zb_cluster_add_attr(metering, ESP_ZB_ZCL_CLUSTER_ID_METERING,
        ESP_ZB_ZCL_ATTR_METERING_MULTIPLIER_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U24, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &mult24);
    esp_zb_cluster_add_attr(metering, ESP_ZB_ZCL_CLUSTER_ID_METERING,
        ESP_ZB_ZCL_ATTR_METERING_DIVISOR_ID,
        ESP_ZB_ZCL_ATTR_TYPE_U24, ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY, &div24);

    esp_zb_cluster_list_add_metering_cluster(cluster_list, metering,
                                              ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* OTA Upgrade cluster (0x0019) — client role */
    esp_zb_ota_cluster_cfg_t ota_cfg = {
        .ota_upgrade_file_version = OTA_FW_VERSION,
        .ota_upgrade_manufacturer = OTA_MANUFACTURER_CODE,
        .ota_upgrade_image_type = OTA_IMAGE_TYPE,
    };
    esp_zb_attribute_list_t *ota_cluster = esp_zb_ota_cluster_create(&ota_cfg);

    // OTA client variable (query timer, hw version, max data size)
    esp_zb_zcl_ota_upgrade_client_variable_t ota_client_var = {
        .timer_query = 0,
        .hw_version = 0x0001,
        .max_data_size = 223,
    };
    esp_zb_ota_cluster_add_attr(ota_cluster,
        ESP_ZB_ZCL_ATTR_OTA_UPGRADE_CLIENT_DATA_ID, &ota_client_var);

    // Server address/endpoint (0xFFFF/0xFF = unknown, discovered at runtime)
    uint16_t ota_upgrade_server_addr = 0xFFFF;
    uint8_t ota_upgrade_server_ep = 0xFF;
    esp_zb_ota_cluster_add_attr(ota_cluster,
        ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ADDR_ID, &ota_upgrade_server_addr);
    esp_zb_ota_cluster_add_attr(ota_cluster,
        ESP_ZB_ZCL_ATTR_OTA_UPGRADE_SERVER_ENDPOINT_ID, &ota_upgrade_server_ep);

    esp_zb_cluster_list_add_ota_cluster(cluster_list, ota_cluster,
                                         ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);

    return cluster_list;
}

/* ---------- Zigbee task ---------- */

static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,
        .install_code_policy = false,
        .nwk_cfg.zczr_cfg = {
            .max_children = 10,
        },
    };
    esp_zb_init(&zb_nwk_cfg);

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *cluster_list = create_cluster_list();

    esp_zb_endpoint_config_t ep_config = {
        .endpoint = ZB_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_METER_INTERFACE_DEVICE_ID,
        .app_device_version = 0,
    };
    esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_config);
    esp_zb_device_register(ep_list);

    esp_zb_core_action_handler_register(zb_action_handler);

    esp_zb_set_primary_network_channel_set(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);
    ESP_ERROR_CHECK(esp_zb_start(false));

    ESP_LOGI(TAG, "Zigbee stack started");
    esp_zb_stack_main_loop();
}

void zigbee_device_init(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
        },
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    xTaskCreate(esp_zb_task, "zigbee", 8192, NULL, 6, NULL);
    ESP_LOGI(TAG, "Zigbee device init complete");
}
