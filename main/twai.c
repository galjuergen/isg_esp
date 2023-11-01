/*	TWAI Network Example

	This example code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h" // Update from V4.2
#include "sdkconfig.h"

#include "elster.h"
#include "mqtt.h"

static const char *TAG = "TWAI";

extern QueueHandle_t xQueue_mqtt_tx;
extern QueueHandle_t xQueue_twai_tx;

uint8_t cyclicReadPacketPos = 0u;
ElsterPacketSend cyclicReadPackets[] = {
	{ 0x480, ELSTER_PT_READ, 0x0112}, // PROGRAMMSCHALTER
	{ 0x180, ELSTER_PT_READ, 0x4f07}, // KUEHLEN_AKTIVIERT
	{ 0x180, ELSTER_PT_READ, 0x000e}, // SPEICHERISTTEMP
	{ 0x500, ELSTER_PT_READ, 0x01d6}, // WPVORLAUFIST
	{ 0x500, ELSTER_PT_READ, 0x0016}, // RUECKLAUFISTTEMP
	{ 0x500, ELSTER_PT_READ, 0x000c}, // AUSSENTEMP
	{ 0x601, ELSTER_PT_READ, 0x4ec7}, // RAUM_IST_TEMPERATUR
	{ 0x601, ELSTER_PT_READ, 0x4ece}, // RAUM_SOLL_TEMPERATUR
	{ 0x601, ELSTER_PT_READ, 0x4ec8}, // RAUM_IST_FEUCHTE
	{ 0x601, ELSTER_PT_READ, 0x4ee0}, // RAUM_TAUPUNKT_TEMPERATUR
	{ 0x514, ELSTER_PT_READ, 0x091c}, // WW_SUM_KWH
	{ 0x514, ELSTER_PT_READ, 0x091d}, // WW_SUM_MWH
	{ 0x514, ELSTER_PT_READ, 0x0920}, // HEIZ_SUM_KWH
	{ 0x514, ELSTER_PT_READ, 0x0921}, // HEIZ_SUM_MWH
};

TimerHandle_t timerHndTwaiRequests;

void send_2_can(uint32_t canid, int16_t data_len, uint8_t const * const data)
{
	ESP_LOGI(TAG,"send_2_can");
	twai_message_t tx_msg;
	
	tx_msg.extd = 0; // use standard frame
	tx_msg.ss = 1;
	tx_msg.self = 0;
	tx_msg.dlc_non_comp = 0;
	tx_msg.identifier = canid;
	tx_msg.data_length_code = data_len;
	if (data_len > 8) {
		ESP_LOGW(TAG, "Data length is reduced to 8 bytes");
		tx_msg.data_length_code = 8;
	}
	for (int i=0;i<tx_msg.data_length_code;i++) {
		tx_msg.data[i] = data[i];
	}
	if (xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY) != pdPASS) {
		ESP_LOGE(pcTaskGetName(0), "xQueueSend Fail");
	}
}

void vTimerCallbackTwaiExpired( TimerHandle_t xTimer )
{
	uint8_t raw[7] = { 0u };
	ElsterPrepareSendPacket(7, raw, cyclicReadPackets[cyclicReadPacketPos]);
	send_2_can(0x680, 7, raw);

	cyclicReadPacketPos++;
	if (cyclicReadPacketPos >= sizeof(cyclicReadPackets)/sizeof(cyclicReadPackets[0]))
		cyclicReadPacketPos = 0u;
}

void twai_task(void *pvParameters)
{
	ESP_LOGI(TAG,"task start");

	twai_message_t rx_msg;
	twai_message_t tx_msg;
	MQTT_t mqttBuf;
	mqttBuf.topic_type = PUBLISH;

	timerHndTwaiRequests = xTimerCreate(
      "twaiTimer", /* name */
      pdMS_TO_TICKS(CONFIG_WPM_REQUEST_PERIOD * 1000), /* period/time */
      pdTRUE, /* auto reload */
      (void*)0, /* timer ID */
      vTimerCallbackTwaiExpired); /* callback */
	xTimerStart(timerHndTwaiRequests, pdMS_TO_TICKS(3000));

	while (1) {
		esp_err_t ret = twai_receive(&rx_msg, pdMS_TO_TICKS(10));
		if (ret == ESP_OK) {
			ESP_LOGD(TAG,"twai_receive identifier=0x%"PRIx32" flags=0x%"PRIx32" data_length_code=%d",
				rx_msg.identifier, rx_msg.flags, rx_msg.data_length_code);

			//int ext = rx_msg.flags & 0x01; // flags is Deprecated
			//int rtr = rx_msg.flags & 0x02; // flags is Deprecated
			int ext = rx_msg.extd;
			int rtr = rx_msg.rtr;
			// ESP_LOGD(TAG, "ext=%x rtr=%x", ext, rtr);


#if CONFIG_ENABLE_PRINT
			if (ext == 0) {
				printf("Standard ID: 0x%03"PRIx32"     ", rx_msg.identifier);
			} else {
				printf("Extended ID: 0x%08"PRIx32, rx_msg.identifier);
			}
			printf(" DLC: %d	Data: ", rx_msg.data_length_code);

			if (rtr == 0) {
				for (int i = 0; i < rx_msg.data_length_code; i++) {
					printf("0x%02x ", rx_msg.data[i]);
				}
			} else {
				printf("REMOTE REQUEST FRAME");

			}
			printf("\n");
#endif

			ElsterPacketReceive packet = ElsterRawToReceivePacket((uint16_t)rx_msg.identifier, (uint8_t)rx_msg.data_length_code, rx_msg.data);
			if (packet.receiver == 0x680)
			{
				switch(packet.packetType)
				{
					case ELSTER_PT_RESPONSE:
					{
						strncpy(mqttBuf.topic, "wp/read/", 9);
						strncpy(&mqttBuf.topic[8], packet.indexName, sizeof(mqttBuf.topic) - 8);
						mqttBuf.data_len = strnlen(packet.value, sizeof(packet.value));
						strncpy(mqttBuf.data, packet.value, sizeof(mqttBuf.data));
						if (xQueueSend(xQueue_mqtt_tx, &mqttBuf, portMAX_DELAY) != pdPASS) {
							ESP_LOGE(TAG, "xQueueSend Fail");
						}
						break;
					}
					default:
					{
						break;
					}
				}
			}
		} else if (ret == ESP_ERR_TIMEOUT) {
			if (xQueueReceive(xQueue_twai_tx, &tx_msg, 0) == pdTRUE) {
				ESP_LOGI(TAG, "tx_msg.identifier=[0x%"PRIx32"] tx_msg.extd=%d", tx_msg.identifier, tx_msg.extd);
				twai_status_info_t status_info;
				twai_get_status_info(&status_info);
				ESP_LOGD(TAG, "status_info.state=%d",status_info.state);
				if (status_info.state != TWAI_STATE_RUNNING) {
					ESP_LOGE(TAG, "TWAI driver not running %d", status_info.state);
					continue;
				}
				ESP_LOGD(TAG, "status_info.msgs_to_tx=%"PRIu32, status_info.msgs_to_tx);
				ESP_LOGD(TAG, "status_info.msgs_to_rx=%"PRIu32, status_info.msgs_to_rx);
				//esp_err_t ret = twai_transmit(&tx_msg, pdMS_TO_TICKS(10));
				esp_err_t ret = twai_transmit(&tx_msg, 0);
				if (ret == ESP_OK) {
					ESP_LOGI(TAG, "twai_transmit success");
				} else {
					ESP_LOGE(TAG, "twai_transmit Fail %s", esp_err_to_name(ret));
				}
			}

		} else {
			ESP_LOGE(TAG, "twai_receive Fail %s", esp_err_to_name(ret));
		}
	} // end while

	// Never reach here
	vTaskDelete(NULL);
}

