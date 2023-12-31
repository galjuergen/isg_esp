/*
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
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "ethernet_init.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h" // Update from V4.2
#include "mdns.h"

#include "mqtt.h"

#define TAG	"MAIN"

static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

#if CONFIG_CAN_BITRATE_20
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_20KBITS();
#define BITRATE "Bitrate is 20 Kbit/s"
#elif CONFIG_CAN_BITRATE_25
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_25KBITS();
#define BITRATE "Bitrate is 25 Kbit/s"
#elif CONFIG_CAN_BITRATE_50
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_50KBITS();
#define BITRATE "Bitrate is 50 Kbit/s"
#elif CONFIG_CAN_BITRATE_100
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_100KBITS();
#define BITRATE "Bitrate is 100 Kbit/s"
#elif CONFIG_CAN_BITRATE_125
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_125KBITS();
#define BITRATE "Bitrate is 125 Kbit/s"
#elif CONFIG_CAN_BITRATE_250
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
#define BITRATE "Bitrate is 250 Kbit/s"
#elif CONFIG_CAN_BITRATE_500
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
#define BITRATE "Bitrate is 500 Kbit/s"
#elif CONFIG_CAN_BITRATE_800
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_800KBITS();
#define BITRATE "Bitrate is 800 Kbit/s"
#elif CONFIG_CAN_BITRATE_1000
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_1MBITS();
#define BITRATE "Bitrate is 1 Mbit/s"
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

QueueHandle_t xQueue_mqtt_tx;
QueueHandle_t xQueue_twai_tx;

static void event_handler(void* arg, esp_event_base_t event_base,
								int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

/* Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "MASK: " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "GW: " IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
}

void eth_init_sta(void)
{
    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles;
    char if_key_str[10];
    char if_desc_str[10];

    // Initialize TCP/IP network interface aka the esp-netif (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize Ethernet driver
    ESP_ERROR_CHECK(ethernet_init_all(&eth_handles, &eth_port_cnt));

    // Create instance(s) of esp-netif for Ethernet(s)
    if (eth_port_cnt == 1) {
        // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
        // default esp-netif configuration parameters.
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        esp_netif_t *eth_netif = esp_netif_new(&cfg);
        // Attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
    } else {
        // Use ESP_NETIF_INHERENT_DEFAULT_ETH when multiple Ethernet interfaces are used and so you need to modify
        // esp-netif configuration parameters for each interface (name, priority, etc.).
        esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
        esp_netif_config_t cfg_spi = {
            .base = &esp_netif_config,
            .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
        };

        for (int i = 0; i < eth_port_cnt; i++) {
            sprintf(if_key_str, "ETH_%d", i);
            sprintf(if_desc_str, "eth%d", i);
            esp_netif_config.if_key = if_key_str;
            esp_netif_config.if_desc = if_desc_str;
            esp_netif_config.route_prio -= i * 5;
            esp_netif_t *eth_netif = esp_netif_new(&cfg_spi);

            // Attach Ethernet driver to TCP/IP stack
            ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[i])));
        }
    }

    // Register user defined event handers
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // Start Ethernet driver state machine
    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }

    // Print each device info
    for (int i = 0; i < eth_port_cnt; i++) {
        eth_dev_info_t info = ethernet_init_get_dev_info(eth_handles[i]);
        if (info.type == ETH_DEV_TYPE_INTERNAL_ETH) {
            ESP_LOGI(TAG, "Device Name: %s", info.name);
            ESP_LOGI(TAG, "Device type: ETH_DEV_TYPE_INTERNAL_ETH(%d)", info.type);
            ESP_LOGI(TAG, "Pins: mdc: %d, mdio: %d", info.pin.eth_internal_mdc, info.pin.eth_internal_mdio);
        } else if (info.type == ETH_DEV_TYPE_SPI) {
            ESP_LOGI(TAG, "Device Name: %s", info.name);
            ESP_LOGI(TAG, "Device type: ETH_DEV_TYPE_SPI(%d)", info.type);
            ESP_LOGI(TAG, "Pins: cs: %d, intr: %d", info.pin.eth_spi_cs, info.pin.eth_spi_int);
        }
    }
}

bool wifi_init_sta(void)
{
	bool ret = false;
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
									ESP_EVENT_ANY_ID,
									&event_handler,
									NULL,
									&instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
									IP_EVENT_STA_GOT_IP,
									&event_handler,
									NULL,
									&instance_got_ip));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = CONFIG_ESP_WIFI_SSID,
			.password = CONFIG_ESP_WIFI_PASSWORD,
			/* Setting a password implies station will connect to all security modes including WEP/WPA.
			 * However these modes are deprecated and not advisable to be used. Incase your Access point
			 * doesn't support WPA2, these mode can be enabled by commenting below line */
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,

			.pmf_cfg = {
				.capable = true,
				.required = false
			},
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
		WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
		pdFALSE,
		pdFALSE,
		portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
		ret = true;
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
	}

	/* The event will not be processed after unregister */
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
	vEventGroupDelete(s_wifi_event_group);
	return ret;
}

esp_err_t query_mdns_host(const char * host_name, char *ip)
{
	ESP_LOGD(__FUNCTION__, "Query A: %s", host_name);

	struct esp_ip4_addr addr;
	addr.addr = 0;

	esp_err_t err = mdns_query_a(host_name, 10000,	&addr);
	if(err){
		if(err == ESP_ERR_NOT_FOUND){
			ESP_LOGW(__FUNCTION__, "%s: Host was not found!", esp_err_to_name(err));
			return ESP_FAIL;
		}
		ESP_LOGE(__FUNCTION__, "Query Failed: %s", esp_err_to_name(err));
		return ESP_FAIL;
	}

	ESP_LOGD(__FUNCTION__, "Query A: %s.local resolved to: " IPSTR, host_name, IP2STR(&addr));
	sprintf(ip, IPSTR, IP2STR(&addr));
	return ESP_OK;
}

void convert_mdns_host(char * from, char * to)
{
	ESP_LOGI(__FUNCTION__, "from=[%s]",from);
	strcpy(to, from);
	char *sp;
	sp = strstr(from, ".local");
	if (sp == NULL) return;

	int _len = sp - from;
	ESP_LOGD(__FUNCTION__, "_len=%d", _len);
	char _from[128];
	strcpy(_from, from);
	_from[_len] = 0;
	ESP_LOGI(__FUNCTION__, "_from=[%s]", _from);

	char _ip[128];
	esp_err_t ret = query_mdns_host(_from, _ip);
	ESP_LOGI(__FUNCTION__, "query_mdns_host=%d _ip=[%s]", ret, _ip);
	if (ret != ESP_OK) return;

	strcpy(to, _ip);
	ESP_LOGI(__FUNCTION__, "to=[%s]", to);
}

esp_err_t build_table(TOPIC_t **topics, char *file, int16_t *ntopic)
{
	ESP_LOGI(TAG, "build_table file=%s", file);
	char line[128];
	int _ntopic = 0;

	FILE* f = fopen(file, "r");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
		return ESP_FAIL;
	}
	while (1){
		if ( fgets(line, sizeof(line) ,f) == 0 ) break;
		// strip newline
		char* pos = strchr(line, '\n');
		if (pos) {
			*pos = '\0';
		}
		ESP_LOGD(TAG, "line=[%s]", line);
		if (strlen(line) == 0) continue;
		if (line[0] == '#') continue;
		_ntopic++;
	}
	fclose(f);
	ESP_LOGI(TAG, "build_table _ntopic=%d", _ntopic);
	
	*topics = calloc(_ntopic, sizeof(TOPIC_t));
	if (*topics == NULL) {
		ESP_LOGE(TAG, "Error allocating memory for topic");
		return ESP_ERR_NO_MEM;
	}

	f = fopen(file, "r");
	if (f == NULL) {
		ESP_LOGE(TAG, "Failed to open file for reading");
		return ESP_FAIL;
	}

	char *ptr;
	int index = 0;
	while (1){
		if ( fgets(line, sizeof(line) ,f) == 0 ) break;
		// strip newline
		char* pos = strchr(line, '\n');
		if (pos) {
			*pos = '\0';
		}
		ESP_LOGD(TAG, "line=[%s]", line);
		if (strlen(line) == 0) continue;
		if (line[0] == '#') continue;

		// Frame type
		ptr = strtok(line, ",");
		ESP_LOGD(TAG, "ptr=%s", ptr);
		if (strcmp(ptr, "S") == 0) {
			(*topics+index)->frame = 0;
		} else if (strcmp(ptr, "E") == 0) {
			(*topics+index)->frame = 1;
		} else {
			ESP_LOGE(TAG, "This line is invalid [%s]", line);
			continue;
		}

		// CAN ID
		uint32_t canid;
		ptr = strtok(NULL, ",");
		if(ptr == NULL) continue;
		ESP_LOGD(TAG, "ptr=%s", ptr);
		canid = strtol(ptr, NULL, 16);
		if (canid == 0) {
			ESP_LOGE(TAG, "This line is invalid [%s]", line);
			continue;
		}
		(*topics+index)->canid = canid;

		// mqtt topic
		char *sp;
		ptr = strtok(NULL, ",");
		if(ptr == NULL) {
			ESP_LOGE(TAG, "This line is invalid [%s]", line);
			continue;
		}
		ESP_LOGD(TAG, "ptr=[%s] strlen=%d", ptr, strlen(ptr));
		sp = strstr(ptr,"#");
		if (sp != NULL) {
			ESP_LOGE(TAG, "This line is invalid [%s]", line);
			continue;
		}
		sp = strstr(ptr,"+");
		if (sp != NULL) {
			ESP_LOGE(TAG, "This line is invalid [%s]", line);
			continue;
		}
		(*topics+index)->topic = (char *)malloc(strlen(ptr)+1);
		strcpy((*topics+index)->topic, ptr);
		(*topics+index)->topic_len = strlen(ptr);
		index++;
	}
	fclose(f);
	*ntopic = index;
	return ESP_OK;
}

void dump_table(TOPIC_t *topics, int16_t ntopic)
{
	for(int i=0;i<ntopic;i++) {
		ESP_LOGI(pcTaskGetName(0), "topics=[%d] frame=%d canid=0x%"PRIx32" topic=[%s] topic_len=%d",
		i, (topics+i)->frame, (topics+i)->canid, (topics+i)->topic, (topics+i)->topic_len);
	}

}

void mqtt_pub_task(void *pvParameters);
void mqtt_sub_task(void *pvParameters);
void twai_task(void *pvParameters);


void app_main()
{
	// Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	  ESP_ERROR_CHECK(nvs_flash_erase());
	  ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

#if CONFIG_CONNECTION_TYPE_ETH
	// initialize ethernet (DHCP)
	eth_init_sta();
	ESP_LOGI(TAG, "--- eth init done ---");
#elif CONFIG_CONNECTION_TYPE_WIFI
	// initialize wifi
	if (wifi_init_sta() == false) {
		while(1) vTaskDelay(10);
	}
	ESP_LOGI(TAG, "--- wifi init done ---");
#endif

	// Initialize mDNS
	ESP_ERROR_CHECK( mdns_init() );
	ESP_LOGI(TAG, "--- mdns init done ---");

	// Install and start TWAI driver
	ESP_LOGI(TAG, "%s",BITRATE);
	ESP_LOGI(TAG, "CTX_GPIO=%d",CONFIG_CTX_GPIO);
	ESP_LOGI(TAG, "CRX_GPIO=%d",CONFIG_CRX_GPIO);

	static const twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CONFIG_CTX_GPIO, CONFIG_CRX_GPIO, TWAI_MODE_NORMAL);
	ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
	ESP_LOGI(TAG, "Driver installed");
	ESP_ERROR_CHECK(twai_start());
	ESP_LOGI(TAG, "Driver started");

	// Create Queue
	xQueue_mqtt_tx = xQueueCreate( 10, sizeof(MQTT_t) );
	configASSERT( xQueue_mqtt_tx );
	xQueue_twai_tx = xQueueCreate( 10, sizeof(twai_message_t) );
	configASSERT( xQueue_twai_tx );

	xTaskCreate(mqtt_pub_task, "mqtt_pub", 1024*4, NULL, 2, NULL);
	xTaskCreate(mqtt_sub_task, "mqtt_sub", 1024*4, NULL, 2, NULL);
	xTaskCreate(twai_task, "twai_rx", 1024*6, NULL, 2, NULL);
}
