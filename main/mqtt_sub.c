/*
	This code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "driver/twai.h"

#include "elster.h"
#include "mqtt.h"

static const char *TAG = "SUB";

static EventGroupHandle_t s_mqtt_event_group;

extern const uint8_t root_cert_pem_start[] asm("_binary_root_cert_pem_start");
extern const uint8_t root_cert_pem_end[] asm("_binary_root_cert_pem_end");

#define MQTT_CONNECTED_BIT BIT0

extern QueueHandle_t xQueue_mqtt_tx;
extern QueueHandle_t xQueue_twai_tx;

static QueueHandle_t xQueueSubscribe;


#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
#else
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
#endif
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
	esp_mqtt_event_handle_t event = event_data;
#endif

	switch (event->event_id) {
		case MQTT_EVENT_CONNECTED:
			ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
			xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
			//esp_mqtt_client_subscribe(mqtt_client, CONFIG_SUB_TOPIC, 0);
			break;
		case MQTT_EVENT_DISCONNECTED:
			ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
			xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
			break;
		case MQTT_EVENT_SUBSCRIBED:
			ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_UNSUBSCRIBED:
			ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_PUBLISHED:
			ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_DATA:
			ESP_LOGI(TAG, "MQTT_EVENT_DATA");
			//ESP_LOGI(TAG, "TOPIC=%.*s\r", event->topic_len, event->topic);
			//ESP_LOGI(TAG, "DATA=%.*s\r", event->data_len, event->data);
			MQTT_t mqttBuf;
			mqttBuf.topic_type = SUBSCRIBE;
			mqttBuf.topic_len = event->topic_len;
			for(int i=0;i<event->topic_len;i++) {
				mqttBuf.topic[i] = event->topic[i];
				mqttBuf.topic[i+1] = 0;
			}
			mqttBuf.data_len = event->data_len;
			for(int i=0;i<event->data_len;i++) {
				mqttBuf.data[i] = event->data[i];
			}
			xQueueSend(xQueueSubscribe, &mqttBuf, 0);
			break;
		case MQTT_EVENT_ERROR:
			ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
			break;
		default:
			ESP_LOGI(TAG, "Other event id:%d", event->event_id);
			break;
	}
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
	return ESP_OK;
#endif
}

esp_err_t query_mdns_host(const char * host_name, char *ip);
void convert_mdns_host(char * from, char * to);

void mqtt_sub_task(void *pvParameters)
{
#if CONFIG_ENABLE_SECURE_MQTT
	ESP_LOGI(TAG, "Start Subscribe Broker:%s", CONFIG_MQTTS_BROKER);
#else
	ESP_LOGI(TAG, "Start Subscribe Broker:%s", CONFIG_MQTT_BROKER);
#endif

	/* Create Eventgroup */
	s_mqtt_event_group = xEventGroupCreate();
	configASSERT( s_mqtt_event_group );
	xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);

	/* Create Queue */
	xQueueSubscribe = xQueueCreate( 10, sizeof(MQTT_t) );
	configASSERT( xQueueSubscribe );

	// Set client id from mac
	uint8_t mac[8];
	ESP_ERROR_CHECK(esp_base_mac_addr_get(mac));
	for(int i=0;i<8;i++) {
		ESP_LOGD(TAG, "mac[%d]=%x", i, mac[i]);
	}
	char client_id[64];
	sprintf(client_id, "sub-%02x%02x%02x%02x%02x%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	ESP_LOGI(TAG, "client_id=[%s]", client_id);

	// Resolve mDNS host name
	char ip[128];
	char uri[138];
#if CONFIG_ENABLE_SECURE_MQTT
	ESP_LOGI(TAG, "CONFIG_MQTTS_BROKER=[%s]", CONFIG_MQTTS_BROKER);
	convert_mdns_host(CONFIG_MQTTS_BROKER, ip);
	ESP_LOGI(TAG, "ip=[%s]", ip);
	sprintf(uri, "mqtts://%s", ip);
#else
	ESP_LOGI(TAG, "CONFIG_MQTT_BROKER=[%s]", CONFIG_MQTT_BROKER);
	convert_mdns_host(CONFIG_MQTT_BROKER, ip);
	ESP_LOGI(TAG, "ip=[%s]", ip);
	sprintf(uri, "mqtt://%s", ip);
#endif
	ESP_LOGI(TAG, "uri=[%s]", uri);


#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
	esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = uri,
#if CONFIG_ENABLE_SECURE_MQTT
		.broker.verification.certificate = (const char *)root_cert_pem_start,
		.broker.address.port = 8883,
#else
		.broker.address.port = 1883,
#endif
#if CONFIG_BROKER_AUTHENTICATION
		.credentials.username = CONFIG_AUTHENTICATION_USERNAME,
		.credentials.authentication.password = CONFIG_AUTHENTICATION_PASSWORD,
#endif
		.credentials.client_id = client_id
	};

#else
	esp_mqtt_client_config_t mqtt_cfg = {
		.uri = uri,
#if CONFIG_ENABLE_SECURE_MQTT
		.cert_pem = (const char *)root_cert_pem_start,
		.port = 8883,
#else
		.port = 1883,
#endif
		.event_handle = mqtt_event_handler,
#if CONFIG_BROKER_AUTHENTICATION
		.username = CONFIG_AUTHENTICATION_USERNAME,
		.password = CONFIG_AUTHENTICATION_PASSWORD,
#endif
		.client_id = client_id
	};
#endif // ESP_IDF_VERSION

	esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
	esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
#endif

	esp_mqtt_client_start(mqtt_client);
	xEventGroupWaitBits(s_mqtt_event_group, MQTT_CONNECTED_BIT, false, true, portMAX_DELAY);
	ESP_LOGI(TAG, "Connect to MQTT Server");

	esp_mqtt_client_subscribe(mqtt_client, "wp/write/KUEHLEN_AKTIVIERT", 0);
	esp_mqtt_client_subscribe(mqtt_client, "wp/write/PROGRAMMSCHALTER", 0);
	esp_mqtt_client_subscribe(mqtt_client, "wp/write/DATUM", 0);
	esp_mqtt_client_subscribe(mqtt_client, "wp/write/TAG", 0);
	esp_mqtt_client_subscribe(mqtt_client, "wp/write/MONAT", 0);
	esp_mqtt_client_subscribe(mqtt_client, "wp/write/JAHR", 0);
	esp_mqtt_client_subscribe(mqtt_client, "wp/write/UHRZEIT", 0);

	twai_message_t tx_msg;
	MQTT_t mqttBuf;
	while (1) {
		xQueueReceive(xQueueSubscribe, &mqttBuf, portMAX_DELAY);
		ESP_LOGI(TAG, "type=%d", mqttBuf.topic_type);

		if (mqttBuf.topic_type != SUBSCRIBE) continue;
		ESP_LOGI(TAG, "TOPIC=[%s]", mqttBuf.topic);
		for(int i=0;i<mqttBuf.data_len;i++) {
			ESP_LOGI(TAG, "DATA=0x%x", mqttBuf.data[i]);
		}

		tx_msg.extd = 0;
		tx_msg.ss = 1;
		tx_msg.self = 0;
		tx_msg.dlc_non_comp = 0;
		tx_msg.identifier = 0x680;
		tx_msg.data_length_code = 7;

		if (strcmp(mqttBuf.topic, "wp/write/PROGRAMMSCHALTER") == 0)
		{
			ESP_LOGI(TAG, "match programm");
			uint16_t value = TranslateString(mqttBuf.data, et_little_endian);
			if ((value >> 8) <= 5u)
			{
				// set value
				ElsterPacketSend pSet = { 0x480, ELSTER_PT_WRITE, 0x0112}; // PROGRAMMSCHALTER
				ElsterPrepareSendPacket(7, tx_msg.data, pSet);
				ElsterSetValueDefault(7, tx_msg.data, value);
				xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);

				// get value right after setting
				ElsterPacketSend pRead = { 0x480, ELSTER_PT_READ, 0x0112}; // PROGRAMMSCHALTER
				ElsterPrepareSendPacket(7, tx_msg.data, pRead);
				xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);
			}
		}
		else if (strcmp(mqttBuf.topic, "wp/write/KUEHLEN_AKTIVIERT") == 0)
		{
			ESP_LOGI(TAG, "match kuehlen");
			
			// set value
			ElsterPacketSend pSet = { 0x180, ELSTER_PT_WRITE, 0x4f07}; // KUEHLEN_AKTIVIERT
			ElsterPrepareSendPacket(7, tx_msg.data, pSet);
			ElsterSetValueBool(7, tx_msg.data, mqttBuf.data[0] == '1');
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);
		
			// get value right after setting
			ElsterPacketSend pRead = { 0x180, ELSTER_PT_READ, 0x4f07}; // KUEHLEN_AKTIVIERT
			ElsterPrepareSendPacket(7, tx_msg.data, pRead);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);
		}
		else if (strcmp(mqttBuf.topic, "wp/write/DATUM") == 0)
		{
			ESP_LOGI(TAG, "match datum");
			uint16_t value = TranslateString(mqttBuf.data, et_datum);
			
			// set value
			ElsterPacketSend pSet = { 0x480, ELSTER_PT_WRITE, 0x000a}; // DATUM
			ElsterPrepareSendPacket(7, tx_msg.data, pSet);
			ElsterSetValueDefault(7, tx_msg.data, value);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);

			// get value right after setting
			ElsterPacketSend pRead = { 0x480, ELSTER_PT_READ, 0x000a}; // DATUM
			ElsterPrepareSendPacket(7, tx_msg.data, pRead);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);
		}
		else if (strcmp(mqttBuf.topic, "wp/write/TAG") == 0)
		{
			ESP_LOGI(TAG, "match tag");
			uint16_t value = TranslateString(mqttBuf.data, et_little_endian);
			
			// set value
			ElsterPacketSend pSet = { 0x480, ELSTER_PT_WRITE, 0x0122}; // TAG
			ElsterPrepareSendPacket(7, tx_msg.data, pSet);
			ElsterSetValueDefault(7, tx_msg.data, value);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);

			// get value right after setting
			ElsterPacketSend pRead = { 0x480, ELSTER_PT_READ, 0x0122}; // TAG
			ElsterPrepareSendPacket(7, tx_msg.data, pRead);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);
		}
		else if (strcmp(mqttBuf.topic, "wp/write/MONAT") == 0)
		{
			ESP_LOGI(TAG, "match tag");
			uint16_t value = TranslateString(mqttBuf.data, et_little_endian);
			
			// set value
			ElsterPacketSend pSet = { 0x480, ELSTER_PT_WRITE, 0x0123}; // MONAT
			ElsterPrepareSendPacket(7, tx_msg.data, pSet);
			ElsterSetValueDefault(7, tx_msg.data, value);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);

			// get value right after setting
			ElsterPacketSend pRead = { 0x480, ELSTER_PT_READ, 0x0122}; // TAG
			ElsterPrepareSendPacket(7, tx_msg.data, pRead);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);
		}
		else if (strcmp(mqttBuf.topic, "wp/write/JAHR") == 0)
		{
			ESP_LOGI(TAG, "match tag");
			uint16_t value = TranslateString(mqttBuf.data, et_little_endian);
			
			// set value
			ElsterPacketSend pSet = { 0x480, ELSTER_PT_WRITE, 0x0124}; // JAHR
			ElsterPrepareSendPacket(7, tx_msg.data, pSet);
			ElsterSetValueDefault(7, tx_msg.data, value);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);

			// get value right after setting
			ElsterPacketSend pRead = { 0x480, ELSTER_PT_READ, 0x0124}; // JAHR
			ElsterPrepareSendPacket(7, tx_msg.data, pRead);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);
		}
		else if (strcmp(mqttBuf.topic, "wp/write/UHRZEIT") == 0)
		{
			ESP_LOGI(TAG, "match uhrzeit");
			uint16_t value = TranslateString(mqttBuf.data, et_zeit);
			
			// set value
			ElsterPacketSend pSet = { 0x480, ELSTER_PT_WRITE, 0x0009}; // UHRZEIT
			ElsterPrepareSendPacket(7, tx_msg.data, pSet);
			ElsterSetValueDefault(7, tx_msg.data, value);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);

			// get value right after setting
			ElsterPacketSend pRead = { 0x480, ELSTER_PT_READ, 0x0009}; // UHRZEIT
			ElsterPrepareSendPacket(7, tx_msg.data, pRead);
			xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY);
		}

	} // end while

	// Never reach here
	ESP_LOGI(TAG, "Task Delete");
	esp_mqtt_client_stop(mqtt_client);
	vTaskDelete(NULL);
}
