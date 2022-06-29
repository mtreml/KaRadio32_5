/*
 * audio_renderer.c
 *
 *  Created on: 12.03.2017
 *      Author: michaelboeckling
 *
 *  Rev. 17.4.2017/Jorgen Kragh Jakobsen
 *      Added 32 bit entry for i2s output and setup i2c call for Merus Audio power audio amp.
 */

#include "esp_log.h"
#include "soc/rtc.h"
#include "esp_chip_info.h"

#define TAG "Renderer"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/dac.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_attr.h"
#include <math.h>

#include "gpio.h"
#include "app_main.h"
#include "MerusAudio.h"
#include "audio_player.h"
#include "audio_renderer.h"

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE

#define EXAMPLE_DATA_BIT_WIDTH      (I2S_DATA_BIT_WIDTH_16BIT)
#define EXAMPLE_SAMPLE_RATE         (36000)

static i2s_chan_handle_t tx_handle = NULL;

static volatile int is_overflow = 0;

static renderer_config_t *renderer_instance = NULL;
static component_status_t renderer_status = UNINITIALIZED;
//static QueueHandle_t i2s_event_queue;

static const uint32_t VUCP_PREAMBLE_B = 0xCCE80000; // 11001100 11101000
static const uint32_t VUCP_PREAMBLE_M = 0xCCE20000; // 11001100 11100010
static const uint32_t VUCP_PREAMBLE_W = 0xCCE40000; // 11001100 11100100

// BMC (Biphase Mark Coded) values (bit order reversed, i.e. LSB first)
DRAM_ATTR static const uint16_t SPDIF_BMCLOOKUP[256] = 
{
	0xcccc, 0x4ccc, 0x2ccc, 0xaccc, 0x34cc, 0xb4cc, 0xd4cc, 0x54cc,
	0x32cc, 0xb2cc, 0xd2cc, 0x52cc, 0xcacc, 0x4acc, 0x2acc, 0xaacc,
	0x334c, 0xb34c, 0xd34c, 0x534c, 0xcb4c, 0x4b4c, 0x2b4c, 0xab4c,
	0xcd4c, 0x4d4c, 0x2d4c, 0xad4c, 0x354c, 0xb54c, 0xd54c, 0x554c,
	0x332c, 0xb32c, 0xd32c, 0x532c, 0xcb2c, 0x4b2c, 0x2b2c, 0xab2c,
	0xcd2c, 0x4d2c, 0x2d2c, 0xad2c, 0x352c, 0xb52c, 0xd52c, 0x552c,
	0xccac, 0x4cac, 0x2cac, 0xacac, 0x34ac, 0xb4ac, 0xd4ac, 0x54ac,
	0x32ac, 0xb2ac, 0xd2ac, 0x52ac, 0xcaac, 0x4aac, 0x2aac, 0xaaac,
	0x3334, 0xb334, 0xd334, 0x5334, 0xcb34, 0x4b34, 0x2b34, 0xab34,
	0xcd34, 0x4d34, 0x2d34, 0xad34, 0x3534, 0xb534, 0xd534, 0x5534,
	0xccb4, 0x4cb4, 0x2cb4, 0xacb4, 0x34b4, 0xb4b4, 0xd4b4, 0x54b4,
	0x32b4, 0xb2b4, 0xd2b4, 0x52b4, 0xcab4, 0x4ab4, 0x2ab4, 0xaab4,
	0xccd4, 0x4cd4, 0x2cd4, 0xacd4, 0x34d4, 0xb4d4, 0xd4d4, 0x54d4,
	0x32d4, 0xb2d4, 0xd2d4, 0x52d4, 0xcad4, 0x4ad4, 0x2ad4, 0xaad4,
	0x3354, 0xb354, 0xd354, 0x5354, 0xcb54, 0x4b54, 0x2b54, 0xab54,
	0xcd54, 0x4d54, 0x2d54, 0xad54, 0x3554, 0xb554, 0xd554, 0x5554,
	0x3332, 0xb332, 0xd332, 0x5332, 0xcb32, 0x4b32, 0x2b32, 0xab32,
	0xcd32, 0x4d32, 0x2d32, 0xad32, 0x3532, 0xb532, 0xd532, 0x5532,
	0xccb2, 0x4cb2, 0x2cb2, 0xacb2, 0x34b2, 0xb4b2, 0xd4b2, 0x54b2,
	0x32b2, 0xb2b2, 0xd2b2, 0x52b2, 0xcab2, 0x4ab2, 0x2ab2, 0xaab2,
	0xccd2, 0x4cd2, 0x2cd2, 0xacd2, 0x34d2, 0xb4d2, 0xd4d2, 0x54d2,
	0x32d2, 0xb2d2, 0xd2d2, 0x52d2, 0xcad2, 0x4ad2, 0x2ad2, 0xaad2,
	0x3352, 0xb352, 0xd352, 0x5352, 0xcb52, 0x4b52, 0x2b52, 0xab52,
	0xcd52, 0x4d52, 0x2d52, 0xad52, 0x3552, 0xb552, 0xd552, 0x5552,
	0xccca, 0x4cca, 0x2cca, 0xacca, 0x34ca, 0xb4ca, 0xd4ca, 0x54ca,
	0x32ca, 0xb2ca, 0xd2ca, 0x52ca, 0xcaca, 0x4aca, 0x2aca, 0xaaca,
	0x334a, 0xb34a, 0xd34a, 0x534a, 0xcb4a, 0x4b4a, 0x2b4a, 0xab4a,
	0xcd4a, 0x4d4a, 0x2d4a, 0xad4a, 0x354a, 0xb54a, 0xd54a, 0x554a,
	0x332a, 0xb32a, 0xd32a, 0x532a, 0xcb2a, 0x4b2a, 0x2b2a, 0xab2a,
	0xcd2a, 0x4d2a, 0x2d2a, 0xad2a, 0x352a, 0xb52a, 0xd52a, 0x552a,
	0xccaa, 0x4caa, 0x2caa, 0xacaa, 0x34aa, 0xb4aa, 0xd4aa, 0x54aa,
	0x32aa, 0xb2aa, 0xd2aa, 0x52aa, 0xcaaa, 0x4aaa, 0x2aaa, 0xaaaa
};
static void write_i2s(const void *buffer);

//KaraDio32
void IRAM_ATTR renderer_volume(uint32_t vol)
{
	// log volume (magic)
	if (vol == 1) return;  // volume 0
//	ESP_LOGI(TAG, "Renderer vol: %d %X",vol,vol );
	if (vol >= 255) 
	{
		renderer_instance->volume = 0x10000;
		ESP_LOGD(TAG, "Renderer volume max:  %d  %X",renderer_instance->volume,renderer_instance->volume );
		return;
	}
	vol = 255  - vol;
	uint32_t value = (log10(255/((float)vol+1)) * 105.54571334);	
//	ESP_LOGI(TAG, "Renderer value: %X",value );
	if (value >= 254) value = 256;
	renderer_instance->volume = value<<8; // *256
	ESP_LOGD(TAG, "Renderer volume:  %X",renderer_instance->volume );
}
//----------------------------------------------------------------------------------------------
/**
 * I2S is MSB first (big-endian) two's complement (signed) integer format.
 * The I2S module receives and transmits left-channel data first.
 *
 * ESP32 is little-endian.
 */
static void IRAM_ATTR render_i2s_samples(char *buf, uint32_t buf_len, pcm_format_t *buf_desc)
{
	register int res = ESP_OK;
	uint8_t* outBuf8;
	uint32_t* outBuf32;
	uint64_t* outBuf64;
	
    // handle changed sample rate
    if(renderer_instance->sample_rate != buf_desc->sample_rate)
    {
        ESP_LOGD(TAG, "changing sample rate from %d to %d", renderer_instance->sample_rate, buf_desc->sample_rate);
        uint32_t rate = buf_desc->sample_rate * renderer_instance->sample_rate_modifier;
        //res =  i2s_set_sample_rates(renderer_instance->i2s_num, rate);

	    if (res != ESP_OK) {
			ESP_LOGE(TAG, "i2s_set_clk error %d",res);
		}
        else renderer_instance->sample_rate = buf_desc->sample_rate;
    }

    uint8_t buf_bytes_per_sample = (buf_desc->bit_depth / 8);
    uint32_t num_samples = buf_len / buf_bytes_per_sample / buf_desc->num_channels;
//KaraDio32 Volume control
	uint32_t mult = renderer_instance->volume;
	
	if ((mult!= 0x10000)) 
	{	
		uint32_t pmax= num_samples*buf_desc->num_channels;
		if (buf_bytes_per_sample ==2)
		{
			int16_t *psample;
			psample = (int16_t*)buf;
			for (int32_t i = 0; i < pmax; i++) 
			{
				psample[i] = (((int32_t)psample[i] * mult)>>16) & 0xFFFF;	
			}
		} else
		{
			int32_t *psample;
			psample = (int32_t*)buf;
			for (int32_t i = 0; i < pmax; i++) 
			{
				psample[i] = ((int64_t)(psample[i] * mult)>>16) & 0xFFFFFFFF;	
			}
		}			
	}

//-------------------------
    // formats match, we can write the whole block
    if (buf_desc->bit_depth == renderer_instance->bit_depth
            && buf_desc->buffer_format == PCM_INTERLEAVED
            && buf_desc->num_channels == 2
            && renderer_instance->output_mode != DAC_BUILT_IN 
			&& renderer_instance->output_mode != PDM
			)
	{
	  	if (renderer_status == RUNNING)
	  	{
		  	write_i2s(buf);
	  	}
		return;
    }

    // support only 16 bit buffers for now
    if(buf_desc->bit_depth != I2S_DATA_BIT_WIDTH_16BIT) {
        ESP_LOGD(TAG, "unsupported decoder bit depth: %d", buf_desc->bit_depth);
		renderer_stop();
		audio_player_stop();    
		return;
    }

    // pointer to left / right sample position
    char *ptr_l = buf;
    char *ptr_r = buf + buf_bytes_per_sample;
    uint8_t stride = buf_bytes_per_sample * 2;

    // right half of the buffer contains all the right channel samples
    if(buf_desc->buffer_format == PCM_LEFT_RIGHT)
    {
        ptr_r = buf + buf_len / 2;
        stride = buf_bytes_per_sample;
    }

    if (buf_desc->num_channels == 1)  // duplicate 
	{
        ptr_r = ptr_l;
    }
	

// har-in-air correction	
	uint32_t outBufBytes = buf_len*(2/buf_desc->num_channels);
	if (renderer_instance->bit_depth == I2S_DATA_BIT_WIDTH_32BIT) outBufBytes <<= 1;
	
	outBuf8 = malloc(outBufBytes);

	if (outBuf8 == NULL) 
	{
		ESP_LOGE(TAG, "malloc outBuf8 failed len:%d ",buf_len);
		renderer_stop();
		audio_player_stop(); 
		return;
	}
	outBuf32 =(uint32_t*)outBuf8;
	outBuf64 = (uint64_t*)outBuf8;

	
    for (int i = 0; i < num_samples; i++) {
        if (renderer_status == STOPPED) break;

        if((renderer_instance->output_mode == DAC_BUILT_IN))//||(renderer_instance->output_mode == PDM))
        {
            // assume 16 bit src bit_depth
            int16_t left = *(int16_t *) ptr_l;
            int16_t right = *(int16_t *) ptr_r;
			
            // The built-in DAC wants unsigned samples, so we shift the range
            // from -32768-32767 to 0-65535.
            left  = left  + 0x8000;
            right = right + 0x8000;					

            uint32_t sample = (uint16_t) left;
            sample = (sample << 16 & 0xffff0000) | ((uint16_t) right);

			outBuf32[i] = sample;
        }
		
        else {

            switch (renderer_instance->bit_depth)
            {
                case I2S_DATA_BIT_WIDTH_16BIT:
                    ; // workaround
                    /* low - high / low - high */
                    const char samp32[4] = {ptr_l[0], ptr_l[1], ptr_r[0], ptr_r[1]};
					outBuf32[i] = (uint32_t)(*((uint32_t*)samp32));					
                    break;

                case I2S_DATA_BIT_WIDTH_32BIT:
                    ; // workaround

                    const char samp64[8] = {0, 0, ptr_l[0], ptr_l[1], 0, 0, ptr_r[0], ptr_r[1]};					
					outBuf64[i] = *((uint64_t*)samp64);
                    break;

                default:
                    ESP_LOGE(TAG, "bit depth unsupported: %d", renderer_instance->bit_depth);
            }
        }

        ptr_r += stride;
        ptr_l += stride;
    }
//	
//	ESP_LOGI(TAG, "I2S write from %x for %d bytes", (uint32_t)outBuf8, bytes_left);
	uint8_t* iobuf = outBuf8;
	write_i2s(iobuf);
	free (outBuf8);
}

// for s/pdif
static bool set_sample_rate(int hz)
{
//  if (!i2sOn) return false;
  if (hz < 32000) return false;
  if (hz == renderer_instance->sample_rate) return true;
  ESP_LOGD(TAG, "Changing S/PDIF from %d to %d", renderer_instance->sample_rate, hz);

  renderer_instance->sample_rate = hz;
  //if (i2s_set_sample_rates(renderer_instance->i2s_num, 2 * hz) != ESP_OK) 
  if(false)
  {
	ESP_LOGE(TAG, "ERROR changing S/PDIF sample rate");	  
	return false;
  }		


  if ((2 * hz )== 88200) {  // for sdk 3.3 only?
	// Manually fix the APLL rate for 44100.
	// See: https://github.com/espressif/esp-idf/issues/2634
	// sdm0 = 28, sdm1 = 8, sdm2 = 5, odir = 0 -> 88199.977
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
	rtc_clk_apll_coeff_set(0,  28, 8, 5);
	rtc_clk_apll_enable(1);
#else
	rtc_clk_apll_enable(1, 28, 8, 5, 0);
#endif
  }	  
  
  return true;
}

// S/PDIF volume
static inline void change_volume16(int16_t *buffer, size_t num_samples)
{
	volatile uint32_t mult = renderer_instance->volume;

	if (mult != 0x10000) // && (renderer_instance->output_mode != DAC_BUILT_IN) && (renderer_instance->output_mode != PDM))// need volume?
	{
		int16_t *psample = buffer;
		for (int32_t i = 0; i < num_samples; i++)
		{
			psample[i] = (((uint32_t)buffer[i] * mult) >> 16) & 0xFFFF;
		}
	}
}

static void  encode_spdif(uint32_t *spdif_buffer, int16_t *buffer, size_t num_samples, pcm_format_t *buf_desc)
{
		// pointer to left / right sample positio
	int16_t *ptr_l = buffer;
	int16_t *ptr_r = buffer + 1;
	uint8_t stride = 2;

	// right half of the buffer contains all the right channel samples
	if(buf_desc->buffer_format == PCM_LEFT_RIGHT)
	{
		ptr_r = buffer + num_samples;
		stride = 1;
	}

	if (buf_desc->num_channels == 1) {
		ptr_r = ptr_l;
	}

	uint32_t *spdif_ptr = spdif_buffer;
	uint16_t hi, lo, aux;

	for (int i = 0; i < num_samples; i++)
	{
		if (renderer_status == STOPPED) break;

		uint16_t sample_left = *ptr_l;

		// BMC encode and flip left channel bits
		hi = SPDIF_BMCLOOKUP[(uint8_t)(sample_left >> 8)];
		lo = SPDIF_BMCLOOKUP[(uint8_t)sample_left];
		// Low word is inverted depending on first bit of high word
		lo ^= (~((int16_t)hi) >> 16);
		spdif_ptr[0] = ((uint32_t)lo << 16) | hi;
		// Fixed 4 bits auxillary-audio-databits, the first used as parity
		// Depending on first bit of low word, invert the bits
		aux = 0xb333 ^ (((uint32_t)((int16_t)lo)) >> 17);
		// Send 'B' preamble only for the first frame of data-block
		if (renderer_instance->frame_num == 0) {
			spdif_ptr[1] = VUCP_PREAMBLE_B | aux;
		} else {
			spdif_ptr[1] = VUCP_PREAMBLE_M | aux;
		}

		uint16_t sample_right = *ptr_r;
		// BMC encode right channel, similar as above
		hi = SPDIF_BMCLOOKUP[(uint8_t)(sample_right >> 8)];
		lo = SPDIF_BMCLOOKUP[(uint8_t)sample_right];
		lo ^= (~((int16_t)hi) >> 16);
		spdif_ptr[2] = ((uint32_t)lo << 16) | hi;
		aux = 0xb333 ^ (((uint32_t)((int16_t)lo)) >> 17);
		spdif_ptr[3] = VUCP_PREAMBLE_W | aux;

		spdif_ptr += 4;
		ptr_r += stride;
		ptr_l += stride;

		if (++(renderer_instance->frame_num) > 191) renderer_instance->frame_num = 0;
	}
}


/* Ported from ESP8266Audio for Ka-Radio32
 * Original source at:
*      https://github.com/earlephilhower/ESP8266Audio/blob/master/src/AudioOutputSPDIF.cpp
*/
static void  render_spdif_samples(const void *buf, uint32_t buf_len, pcm_format_t *buf_desc)
{

	
	int16_t *pcm_buffer = (int16_t*)buf;

	// support only 16 bit buffers for now
	if(buf_desc->bit_depth != I2S_DATA_BIT_WIDTH_16BIT) {
		ESP_LOGE(TAG, "unsupported decoder bit depth: %d", buf_desc->bit_depth);
		renderer_stop();
		audio_player_stop(); 
		return;
	}
//
//-------------------------

	uint8_t buf_bytes_per_sample = (buf_desc->bit_depth / 8);
	uint32_t num_samples = buf_len / buf_bytes_per_sample / buf_desc->num_channels;
//	uint32_t num_samples = buf_len / ((buf_desc->bit_depth / 8) )/ buf_desc->num_channels;

	// aac max: #define OUTPUT_BUFFER_SIZE  (2048 * sizeof(SHORT) * 2)
	//	mp3max:  short int short_sample_buff[2][32];
	size_t bytes_cnt = num_samples * sizeof(uint32_t) * 4;
	
//	ESP_LOGI(TAG, "render_spdif_samples len: %d, bytes_cnt: %d",buf_len,bytes_cnt);
	
	uint32_t *spdif_buffer = heap_caps_malloc(bytes_cnt, MALLOC_CAP_DEFAULT);
	if (spdif_buffer == NULL)
	{
		ESP_LOGE(TAG, "spdif buf failed len:%d ",buf_len);
		renderer_stop();
		audio_player_stop(); 
		return;
	}

    // handle changed sample rate
	if (set_sample_rate(buf_desc->sample_rate) != true) {
		ESP_LOGE(TAG, "i2s_set_clk error");
	}

	// adjust digital volume
	change_volume16(pcm_buffer, num_samples * buf_desc->num_channels);

	// generate SPDIF stream
	encode_spdif(spdif_buffer, pcm_buffer, num_samples, buf_desc);
	write_i2s(spdif_buffer);
	free (spdif_buffer);
}

// Decoded frame
void IRAM_ATTR render_samples(char *buf, uint32_t buf_len, pcm_format_t *buf_desc)
{
    if(renderer_status != RUNNING) return;

	if(renderer_instance->output_mode == SPDIF) render_spdif_samples(buf, buf_len, buf_desc);
	else render_i2s_samples(buf, buf_len, buf_desc);
}

void  renderer_zero_dma_buffer()
{
    //i2s_zero_dma_buffer(renderer_instance->i2s_num);
}


renderer_config_t *renderer_get()
{
    return renderer_instance;
}

/* init renderer sink */
void renderer_init(renderer_config_t *config)
{
    // update global
    renderer_instance = config;
	renderer_instance->frame_num = 0;

    renderer_status = INITIALIZED;
}

void renderer_start()
{
    if(renderer_status == RUNNING) return;
	
    renderer_instance->frame_num = 0;
	ESP_LOGD(TAG, "Start");
    renderer_status = RUNNING;		
}

void renderer_stop()
{
    if(renderer_status == STOPPED) return;
//	if(renderer_status == RUNNING)

    renderer_status = STOPPED;	
	ESP_LOGD(TAG, "Stop");	

    renderer_instance->frame_num = 0;
}

void renderer_destroy()
{
    renderer_status = UNINITIALIZED;
//    i2s_driver_uninstall(renderer_instance->i2s_num);
}

static void write_i2s(const void *buffer)
{
    uint32_t buf_len = 0;
     size_t bytes_written = 0;
    uint32_t cnt = 0;

    while (1) {
        if (i2s_channel_write(tx_handle, buffer, buf_len, &bytes_written, 1000) == ESP_OK) 
		{
            if (cnt == 0) {
                printf("[i2s write] %d bytes are written successfully\n", bytes_written);
            }
            cnt++;
            cnt %= 20;
        } else {
            printf("[i2s write] %d bytes are written, timeout triggered\n", bytes_written);
        }
    }
    vTaskDelete(NULL);
}

bool i2s_init()
{
    
	renderer_config_t *config;
	config = renderer_get();
	
    config->bit_depth = I2S_DATA_BIT_WIDTH_16BIT;
    config->i2s_num = I2S_NUM_0;
    config->sample_rate = 44100;
    config->sample_rate_modifier = 1.0;
    config->output_mode = get_audio_output_mode();	
		
	esp_chip_info_t out_info;
	esp_chip_info(&out_info);
		
	gpio_num_t lrck;
	gpio_num_t bclk;
	gpio_num_t i2sdata;
	gpio_get_i2s(&lrck ,&bclk ,&i2sdata );
	
	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    /* Giving both tx and rx handle will make the i2s works in full-duplex mode and can share the bclk and ws signal */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &tx_handle));
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(EXAMPLE_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(EXAMPLE_DATA_BIT_WIDTH, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk,
            .ws   = lrck,
            .dout = i2sdata,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
#if SOC_I2S_SUPPORTS_APLL
    // APLL clock is more accurate when sample rate is high
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_APLL;
#endif
    /* Initialize the tx channel handle to standard mode */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));

    printf("I2S tx channels have been initialized to standard output mode\n");

    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    printf("I2S tx channel enabled\n");
    
	
    if(config->output_mode == I2S_MERUS) 
	{
        if (init_ma120(0x50))			// setup ma120x0p and initial volume
			config->output_mode = I2S;	// error, back to I2S
    }
    else	if(config->output_mode == SPDIF)
 			{
				config->sample_rate = 0;
				set_sample_rate(44100);
			}

	return true;
}