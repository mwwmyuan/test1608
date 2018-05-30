#ifndef __APP_BT_STREAM_H__
#define __APP_BT_STREAM_H__

#if defined(A2DP_AAC_ON)
#define BT_AUDIO_BUFF_SIZE		(2048*2*2)
#else
#define BT_AUDIO_BUFF_SIZE		(512*5*2*2)
#endif
#define BT_AUDIO_SCO_BUFF_SIZE    	(120*2*2)	//120:frame size, 2:uint16_t, 2:pingpong; (2: LR dual channel)
#define BT_AUDIO_CACHE_2_UNCACHE(addr) \
    ((unsigned char *)((unsigned int)addr & ~(0x04000000)))

#if BT_AUDIO_BUFF_SIZE < BT_AUDIO_SCO_BUFF_SIZE * 4
#error BT_AUDIO_BUFF_SIZE must be at least BT_AUDIO_SCO_BUFF_SIZE * 4
#endif

enum APP_BT_STREAM_T {
	APP_BT_STREAM_HFP_PCM = 0,
	APP_BT_STREAM_HFP_CVSD,
	APP_BT_STREAM_HFP_VENDOR,
	APP_BT_STREAM_A2DP_SBC,
	APP_BT_STREAM_A2DP_AAC,
	APP_BT_STREAM_A2DP_VENDOR,
	APP_FACTORYMODE_AUDIO_LOOP,
	APP_PLAY_BACK_AUDIO,
    APP_A2DP_SOURCE_LINEIN_AUDIO,
	APP_A2DP_SOURCE_I2S_AUDIO,
	APP_BT_STREAM_NUM,
};

enum APP_BT_SETTING_T {
	APP_BT_SETTING_OPEN = 0,
	APP_BT_SETTING_CLOSE,
	APP_BT_SETTING_SETUP,
	APP_BT_SETTING_RESTART,
	APP_BT_SETTING_CLOSEALL,
	APP_BT_SETTING_CLOSEMEDIA,
	APP_BT_SETTING_NUM,
};


typedef struct {
    uint16_t id;
    uint16_t status;

    uint16_t aud_type;
    uint16_t aud_id;

    uint8_t freq;
}APP_AUDIO_STATUS;

int app_bt_stream_open(APP_AUDIO_STATUS* status);

int app_bt_stream_close(APP_BT_STREAM_T player);

int app_bt_stream_setup(APP_BT_STREAM_T player, uint8_t status);

int app_bt_stream_restart(APP_AUDIO_STATUS* status);

int app_bt_stream_closeall();

bool app_bt_stream_isrun(APP_BT_STREAM_T player);

void app_bt_stream_volumeup(void);

void app_bt_stream_volumedown(void);

void app_bt_stream_volume_ptr_update(uint8_t *bdAddr);

struct btdevice_volume * app_bt_stream_volume_get_ptr(void);

int app_bt_stream_volumeset(int8_t vol);

void app_bt_stream_a2dpvolume_reset(void);

void app_bt_stream_hfpvolume_reset(void);

void app_bt_stream_copy_track_one_to_two_16bits(int16_t *dst_buf, int16_t *src_buf, uint32_t src_len);

#endif
