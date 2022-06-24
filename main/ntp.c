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
#include "esp_sntp.h"

#include "ntp.h"
#include "interface.h"


static void initialize_sntp(void);
void time_sync_notification_cb(struct timeval *tv);

// list of major public servers http://tf.nist.gov/tf-cgi/servers.cgi
// get ntp time and return an allocated tm struct (UTC)
bool ntp_get_time(struct tm **_dt) 
{
    sntp_servermode_dhcp(1);      // accept NTP offers from DHCP server, if any
	
	initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm dt = { 0 };
    int retry = 0;
    const int retry_count = 3;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
    localtime_r(&now, &dt);
	sntp_stop();
	
	// convert to unix time
	ESP_LOGI(TAG,"Timestamp: %d",now);
	// create tm struct
	*_dt = gmtime(&now);
	return true;
}

// print  date time in ISO-8601 local time format
void ntp_print_time() {
	struct tm* dt;
	int8_t tz;	
	char msg[30];
	
	if (ntp_get_time(&dt) )
	{
		tz =applyTZ(dt);
//	os_printf("##Time: isdst: %d %02d:%02d:%02d\n",dt->tm_isdst, dt->tm_hour, dt->tm_min, dt->tm_sec);		
//	os_printf("##Date: %02d-%02d-%04d\n", dt->tm_mday, dt->tm_mon+1, dt->tm_year+1900);	
		strftime(msg, 48, "%Y-%m-%dT%H:%M:%S", dt);
//	ISO-8601 local time   https://www.w3.org/TR/NOTE-datetime
//  YYYY-MM-DDThh:mm:ssTZD (eg 1997-07-16T19:20:30+01:00)
		if (tz >=0)
			kprintf("##SYS.DATE#: %s+%02d:00\n",msg,tz);
		else
			kprintf("##SYS.DATE#: %s%03d:00\n",msg,tz);
	}
		
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);

/*
 * If 'NTP over DHCP' is enabled, we set dynamic pool address
 * as a 'secondary' server. It will act as a fallback server in case that address
 * provided via NTP over DHCP is not accessible
 */
	sntp_stop();
	
	sntp_setservername(0, "ru.pool.ntp.org");

	sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

	sntp_set_time_sync_notification_cb(time_sync_notification_cb);

	sntp_init();
    ESP_LOGI(TAG, "List of configured NTP servers:");

    for (uint8_t i = 0; i < 1; ++i){
        if (sntp_getservername(i)){
            ESP_LOGI(TAG, "server %d: %s", i, sntp_getservername(i));
        } else {
            // we have either IPv4 or IPv6 address, let's print it
            char buff[15];
            ip_addr_t const *ip = sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, 15) != NULL)
                ESP_LOGI(TAG, "server %d: %s", i, buff);
        }
    }

}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}
