//////////////////////////////////////////////////
// Simple NTP client for ESP8266, RTOS SDK.
// Copyright 2016 jp cocatrix (KaraWin)
// jp@karawin.fr
// See license.txt for license terms.
//////////////////////////////////////////////////
// esp32
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#define TAG "NTP"
//
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include <esp_sntp.h>
#include "esp_netif_sntp.h"

#include "ntp.h"
#include "interface.h"

static void initialize_sntp();

// list of major public servers http://tf.nist.gov/tf-cgi/servers.cgi
// get ntp time and return an allocated tm struct (UTC)
bool ntp_get_time(struct tm **_dt)
{

	// wait for time to be set
	time_t now = 0;

	struct tm dt = {0};

	int retry = 0;
	const int retry_count = 3;

	if (_sntp_init != 0)
		initialize_sntp();

	if (_sntp_init != 0)
		return false;

	while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count)
	{
		ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
		vTaskDelay(2000 / portTICK_PERIOD_MS);
	}
	time(&now);
	localtime_r(&now, &dt);

	// convert to unix time
	ESP_LOGI(TAG, "Timestamp: %" PRIi64 "", now);
	// create tm struct
	*_dt = gmtime(&now);
	// esp_netif_sntp_deinit();

	return true;
}

// print  date time in ISO-8601 local time format
void ntp_print_time()
{
	struct tm *dt;
	int8_t tz;
	char msg[30];

	if (ntp_get_time(&dt))
	{
		tz = applyTZ(dt);
		//	os_printf("##Time: isdst: %d %02d:%02d:%02d\n",dt->tm_isdst, dt->tm_hour, dt->tm_min, dt->tm_sec);
		//	os_printf("##Date: %02d-%02d-%04d\n", dt->tm_mday, dt->tm_mon+1, dt->tm_year+1900);
		strftime(msg, 48, "%d-%m-%Y %H:%M:%S", dt);
		//	ISO-8601 local time   https://www.w3.org/TR/NOTE-datetime
		//  YYYY-MM-DDThh:mm:ssTZD (eg 1997-07-16T19:20:30+01:00)
		if (tz >= 0)
			kprintf("#SYS.DATE#: %s+%02d\n", msg, tz);
		else
			kprintf("##SYS.DATE#: %s%03d:00\n", msg, tz);
	}
}

static void initialize_sntp()
{
	ESP_LOGI(TAG, "Initializing SNTP");

	esp_sntp_stop();
	/*
	 * If 'NTP over DHCP' is enabled, we set dynamic pool address
	 * as a 'secondary' server. It will act as a fallback server in case that address
	 * provided via NTP over DHCP is not accessible
	 */

	esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("ru.pool.ntp.org");

	config.start = false;					  // start SNTP service explicitly (after connecting)
	config.server_from_dhcp = true;			  // accept NTP offers from DHCP server, if any (need to enable *before* connecting)
	config.renew_servers_after_new_IP = true; // let esp-netif update configured SNTP server(s) after receiving DHCP lease
	config.index_of_first_server = 1;		  // updates from server num 1, leaving server 0 (from DHCP) intact
	config.smooth_sync = true;
	config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;

	_sntp_init = esp_netif_sntp_init(&config);

	ESP_LOGI(TAG, "Starting SNTP");
	esp_netif_sntp_start();
}
