/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#include "nx_client.h"

#include <stdio.h>

#include "screen.h"
#include "sensor.h"
#include "stm32f4xx_hal.h"

#include "nx_api.h"
#include "nx_azure_iot_hub_client.h"
#include "nx_azure_iot_json_reader.h"
#include "nx_azure_iot_provisioning_client.h"

// These are sample files, user can build their own certificate and ciphersuites
#include "azure_iot_cert.h"
#include "azure_iot_ciphersuites.h"
#include "azure_iot_nx_client.h"

#include "azure_config.h"
#include "pnp_device_info.h"

#define IOT_MODEL_ID                "dtmi:com:mxchip:mxchip_iot_devkit:example:RTOSGetStarted;1"
#define TELEMETRY_INTERVAL_PROPERTY "telemetryInterval"
#define LED_STATE_PROPERTY          "ledState"

#define IOT_MODEL_COMPONENT_NAME    "deviceInformation"

#define TELEMETRY_INTERVAL_EVENT 1

#define MAX_MESSAGE_SIZE 96

static AZURE_IOT_NX_CONTEXT azure_iot_nx_client;
static TX_EVENT_FLAGS_GROUP azure_iot_flags;

static int32_t telemetry_interval = 10;

static void set_led_state(bool level)
{
    if (level)
    {
        printf("LED is turned ON\r\n");
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }
    else
    {
        printf("LED is turned OFF\r\n");
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    }
}

static int get_dec_value (float value) {
    int decvalue = value;
    return decvalue;
}

static int get_frac_value (float value) {
    int fracvalue = abs(100 * (value - (long)value));
    return fracvalue;
}

static void direct_method_cb(AZURE_IOT_NX_CONTEXT* nx_context,
    const UCHAR* method,
    USHORT method_length,
    UCHAR* payload,
    USHORT payload_length,
    VOID* context,
    USHORT context_length)
{
    UINT status;
    UINT http_status    = 501;
    CHAR* http_response = "{}";

    if (strncmp((CHAR*)method, "turnOnLed", method_length) == 0)
    {
        printf("Method= turnOnLed invoked\r\n");

        set_led_state(true);
        azure_iot_nx_client_publish_bool_property(&azure_iot_nx_client, LED_STATE_PROPERTY, true);
        http_status = 200;
    }

    if (strncmp((CHAR*)method, "turnOffLed", method_length) == 0)
    {
        printf("Method= turnOffLed invoked\r\n");

        set_led_state(false);
        azure_iot_nx_client_publish_bool_property(&azure_iot_nx_client, LED_STATE_PROPERTY, false);
        http_status = 200;
    }

    if (strncmp((CHAR*)method, "displayText", method_length) == 0)
    {
        printf("Method= displayText invoked\r\n");

        char print_message[20] = {""};
        strncpy(print_message, (CHAR*)payload + 1, payload_length - 2);
        print_message[payload_length - 2] = '\0';
        printf("Screen : %s\r\n", print_message);
        screen_print(print_message, L0);
        http_status = 200;
    }

    if ((status = nx_azure_iot_hub_client_direct_method_message_response(&nx_context->iothub_client,
             http_status,
             context,
             context_length,
             (UCHAR*)http_response,
             strlen(http_response),
             NX_WAIT_FOREVER)))
    {
        printf("Direct method response failed! (0x%08x)\r\n", status);
        return;
    }
}

static void device_twin_desired_property_cb(UCHAR* component_name,
    UINT component_name_len,
    UCHAR* property_name,
    UINT property_name_len,
    NX_AZURE_IOT_JSON_READER property_value_reader,
    UINT version,
    VOID* userContextCallback)
{
    UINT status;
    AZURE_IOT_NX_CONTEXT* nx_context = (AZURE_IOT_NX_CONTEXT*)userContextCallback;

    if (strncmp((CHAR*)property_name, TELEMETRY_INTERVAL_PROPERTY, property_name_len) == 0)
    {
        status = nx_azure_iot_json_reader_token_int32_get(&property_value_reader, &telemetry_interval);
        if (status == NX_AZURE_IOT_SUCCESS)
        {
            // Set a telemetry event so we pick up the change immediately
            tx_event_flags_set(&azure_iot_flags, TELEMETRY_INTERVAL_EVENT, TX_OR);

            // Confirm reception back to hub
            azure_nx_client_respond_int_writeable_property(
                nx_context, TELEMETRY_INTERVAL_PROPERTY, telemetry_interval, 200, version);
        }
    }
}

static void device_twin_property_cb(UCHAR* component_name,
    UINT component_name_len,
    UCHAR* property_name,
    UINT property_name_len,
    NX_AZURE_IOT_JSON_READER property_value_reader,
    UINT version,
    VOID* userContextCallback)
{
    UINT status;
    AZURE_IOT_NX_CONTEXT* nx_context = (AZURE_IOT_NX_CONTEXT*)userContextCallback;

    if (strncmp((CHAR*)property_name, TELEMETRY_INTERVAL_PROPERTY, property_name_len) == 0)
    {
        status = nx_azure_iot_json_reader_token_int32_get(&property_value_reader, &telemetry_interval);
        if (status == NX_AZURE_IOT_SUCCESS)
        {
            // Set a telemetry event so we pick up the change immediately
            tx_event_flags_set(&azure_iot_flags, TELEMETRY_INTERVAL_EVENT, TX_OR);
        }
    }

    // Confirm reception back to hub
    azure_nx_client_respond_int_writeable_property(
        nx_context, TELEMETRY_INTERVAL_PROPERTY, telemetry_interval, 200, version);
}

static void publish_device_property_all() 
{
    CHAR property_message[256];
    snprintf(property_message, sizeof(property_message), "{\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":\"%s\",\"%s\":%d,\"%s\":%d,\"__t\":\"c\"}"
    , DEVICE_INFO_MANUfACTURER_PROPERTY, MANUfACTURER_PROPERTY
    , DEVICE_INFO_MODEL_PROPERTY, MODEL_PROPERTY
    , DEVICE_INFO_SWVERSION_PROPERTY, SWVERSION_PROPERTY
    , DEVICE_INFO_OSNAME_PROPERTY, OSNAME_PROPERTY
    , DEVICE_INFO_PROCESSORARCHITECTURE_PROPERTY, PROCESSORARCHITECTURE_PROPERTY
    , DEVICE_INFO_PROCESSORMANUfACTURER_PROPERTY, PROCESSORMANUfACTURER_PROPERTY
    , DEVICE_INFO_TOTALSTORAGE_PROPERTY, TOTALSTORAGE_PROPERTY
    , DEVICE_INFO_TOTAKMEMORY_PROPERTY, TOTAKMEMORY_PROPERTY);

    azure_iot_nx_client_publish_property(&azure_iot_nx_client, IOT_MODEL_COMPONENT_NAME, property_message);
}

static void publish_device_telemetry(lps22hb_t lps22hb_data, hts221_data_t hts221_data)
{
    CHAR combined_message[MAX_MESSAGE_SIZE];
    // Send multiple telemetries in a single message
    snprintf(combined_message, sizeof(combined_message), "{\"%s\":%d.%1d,\"%s\":%d.%1d,\"%s\":%d.%1d}"
    , HUMIDITY, get_dec_value(hts221_data.humidity_perc), get_frac_value(hts221_data.humidity_perc)
    , TEMPERATURE, get_dec_value(lps22hb_data.temperature_degC), get_frac_value(lps22hb_data.temperature_degC)
    , PRESSURE, get_dec_value(lps22hb_data.pressure_hPa / 10), get_frac_value(lps22hb_data.pressure_hPa / 10));

    azure_iot_nx_client_publish_telemetry(&azure_iot_nx_client, combined_message);
}

static void publish_device_telemetry_magnetometer(lis2mdl_data_t lis2mdl_data)
{
    CHAR combined_message[MAX_MESSAGE_SIZE];

    // Send multiple telemetries in a single message
    snprintf(combined_message, sizeof(combined_message), "{\"%s\":%d.%1d,\"%s\":%d.%1d,\"%s\":%d.%1d}"
    , MAGNETOMETERX, get_dec_value(lis2mdl_data.magnetic_mG[0]), get_frac_value(lis2mdl_data.magnetic_mG[0])
    , MAGNETOMETERY, get_dec_value(lis2mdl_data.magnetic_mG[1]), get_frac_value(lis2mdl_data.magnetic_mG[1])
    , MAGNETOMETERZ, get_dec_value(lis2mdl_data.magnetic_mG[2]), get_frac_value(lis2mdl_data.magnetic_mG[2]));

    azure_iot_nx_client_publish_telemetry(&azure_iot_nx_client, combined_message);
}

static void publish_device_telemetry_acceleration(lsm6dsl_data_t lsm6dsl_data)
{
    CHAR combined_message[MAX_MESSAGE_SIZE];

    // Send multiple telemetries in a single message
    snprintf(combined_message, sizeof(combined_message), "{\"%s\":%d.%1d,\"%s\":%d.%1d,\"%s\":%d.%1d}"
    , ACCELEROMETERX, get_dec_value(lsm6dsl_data.acceleration_mg[0] / 1000), get_frac_value(lsm6dsl_data.acceleration_mg[0] / 1000)
    , ACCELEROMETERY, get_dec_value(lsm6dsl_data.acceleration_mg[1] / 1000), get_frac_value(lsm6dsl_data.acceleration_mg[1] / 1000)
    , ACCELEROMETERZ, get_dec_value(lsm6dsl_data.acceleration_mg[2] / 1000), get_frac_value(lsm6dsl_data.acceleration_mg[2] / 1000));

    azure_iot_nx_client_publish_telemetry(&azure_iot_nx_client, combined_message);
}

static void publish_device_telemetry_angular_rate(lsm6dsl_data_t lsm6dsl_data)
{
    CHAR combined_message[MAX_MESSAGE_SIZE];

    // Send multiple telemetries in a single message
    snprintf(combined_message, sizeof(combined_message), "{\"%s\":%d.%1d,\"%s\":%d.%1d,\"%s\":%d.%1d}"
    , GYROSCOPEX, get_dec_value(lsm6dsl_data.angular_rate_mdps[0] / 1000), get_frac_value(lsm6dsl_data.angular_rate_mdps[0] / 1000)
    , GYROSCOPEY, get_dec_value(lsm6dsl_data.angular_rate_mdps[0] / 1000), get_frac_value(lsm6dsl_data.angular_rate_mdps[0] / 1000)
    , GYROSCOPEZ, get_dec_value(lsm6dsl_data.angular_rate_mdps[2] / 1000), get_frac_value(lsm6dsl_data.angular_rate_mdps[2] / 1000));

    azure_iot_nx_client_publish_telemetry(&azure_iot_nx_client, combined_message);
}

UINT azure_iot_nx_client_entry(
    NX_IP* ip_ptr, NX_PACKET_POOL* pool_ptr, NX_DNS* dns_ptr, UINT (*unix_time_callback)(ULONG* unix_time))
{
    UINT status;
    ULONG events        = 0;
    int telemetry_state = 0;
    lps22hb_t lps22hb_data;
    hts221_data_t hts221_data;
    lsm6dsl_data_t lsm6dsl_data;
    lis2mdl_data_t lis2mdl_data;

    if ((status = tx_event_flags_create(&azure_iot_flags, "Azure IoT flags")))
    {
        printf("FAIL: Unable to create nx_client event flags (0x%04x)\r\n", status);
        return status;
    }

#ifdef ENABLE_DPS
    status = azure_iot_nx_client_dps_create(&azure_iot_nx_client,
        ip_ptr,
        pool_ptr,
        dns_ptr,
        unix_time_callback,
        IOT_DPS_ENDPOINT,
        IOT_DPS_ID_SCOPE,
        IOT_DPS_REGISTRATION_ID,
        IOT_PRIMARY_KEY,
        IOT_MODEL_ID);
#else
    status = azure_iot_nx_client_create(&azure_iot_nx_client,
        ip_ptr,
        pool_ptr,
        dns_ptr,
        unix_time_callback,
        IOT_HUB_HOSTNAME,
        IOT_DEVICE_ID,
        IOT_PRIMARY_KEY,
        IOT_MODEL_ID);
#endif
    if (status != NX_SUCCESS)
    {
        printf("ERROR: failed to create iot client 0x%04x\r\n", status);
        return status;
    }

    // Register the callbacks
    azure_iot_nx_client_register_direct_method(&azure_iot_nx_client, direct_method_cb);
    azure_iot_nx_client_register_device_twin_desired_prop(&azure_iot_nx_client, device_twin_desired_property_cb);
    azure_iot_nx_client_register_device_twin_prop(&azure_iot_nx_client, device_twin_property_cb);

    if ((status = azure_iot_nx_client_connect(&azure_iot_nx_client)))
    {
        printf("ERROR: failed to connect nx client (0x%08x)\r\n", status);
        return status;
    }

    // Request the device twin for writeable property update
    // if ((status = nx_azure_iot_hub_client_device_twin_properties_request(
    //          &azure_iot_nx_client.iothub_client, NX_WAIT_FOREVER)))
    // {
    //     printf("ERROR: failed to request device twin (0x%08x)\r\n", status);
    //     return status;
    // }

    // Send reported properties
    azure_iot_nx_client_publish_int_writeable_property(&azure_iot_nx_client, TELEMETRY_INTERVAL_PROPERTY, telemetry_interval);
    publish_device_property_all();

    printf("\r\nStarting Main loop\r\n");
    screen_print("Azure IoT", L0);
    while (true)
    {
        tx_event_flags_get(
            &azure_iot_flags, TELEMETRY_INTERVAL_EVENT, TX_OR_CLEAR, &events, telemetry_interval * NX_IP_PERIODIC_RATE);

        switch (telemetry_state)
        {
            case 0:
                // Send the compensated temperature, pressure, humidity and magnetic
                lps22hb_data = lps22hb_data_read();
                hts221_data = hts221_data_read();
                publish_device_telemetry(lps22hb_data, hts221_data);
                break;

            case 1:
                // Send the compensated magnetic
                lis2mdl_data = lis2mdl_data_read();
                publish_device_telemetry_magnetometer(lis2mdl_data);
                break;

            case 2:
                // Send the compensated acceleration
                lsm6dsl_data = lsm6dsl_data_read();
                publish_device_telemetry_acceleration(lsm6dsl_data);
                break;

            case 3:
                // Send the compensated gyroscope
                lsm6dsl_data = lsm6dsl_data_read();
                publish_device_telemetry_angular_rate(lsm6dsl_data);
                break;
        }

        telemetry_state = (telemetry_state + 1) % 4;
    }

    return NX_SUCCESS;
}
