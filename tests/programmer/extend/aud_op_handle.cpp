#include "stdint.h"
#include "stdbool.h"
#include "plat_types.h"
#include "string.h"
#include "nv_factory_section.h"
#include "bt_drv_interface.h"
#include "hal_trace.h"
#include "hal_cache.h"
#include "hal_analogif.h"
#include "hal_codecip.h"
#include "hal_codec.h"
#include "extend_cmd_handle.h"
#include "aud_op_handle.h"
#include "audioflinger.h"
#include "analog.h"

#define AUD_OP_BUFF_SIZE    	(120*2*2)
#define AUD_OP_CACHE_2_UNCACHE(addr) \
    ((unsigned char *)((unsigned int)addr & ~(0x04000000)))

enum AF_AUDIO_CACHE_T {
    AF_AUDIO_CACHE_CACHEING= 0,
    AF_AUDIO_CACHE_OK,
    AF_AUDIO_CACHE_QTY,
};

struct AUD_OP_TXRX_CHAIN_CFG_T {
    unsigned char txpa_gain;
    unsigned char sdm_gain;
    unsigned char sdac_vol;
    unsigned char mic_gain1;
    unsigned char mic_gain2;
    unsigned char sadc_vol;
};

struct  CODEC_DAC_VOL_T codec_dac_vol=
{16, 3,  21}; /* -21db  VOL_WARNINGTONE */

static struct AUD_OP_TXRX_CHAIN_CFG_T txrx_chain_cfg= {16,3,21,5,7,7};

static enum AF_AUDIO_CACHE_T a2dp_cache_status = AF_AUDIO_CACHE_QTY;
static int16_t *af_audioloop_play_cache = NULL;

static uint8_t audio_buff[1024*6];

int aud_op_miccfg_update(uint8_t ga1a, uint8_t ga2a, uint8_t sadc_vol)
{
    analog_codec_rx_gain_sel(0x03, ga1a, ga2a);
    hal_codec_ad_chain_set(HAL_CODEC_ID_0, sadc_vol);
    return 0;
}

int aud_op_skpcfg_update(uint8_t txpa_gain, uint8_t sdm_gain, uint8_t sdac_vol)
{
    analog_codec_tx_pa_gain_sel(txpa_gain);
    hal_codec_da_chain_set(HAL_CODEC_ID_0, sdm_gain,sdac_vol);
    return 0;
}

static uint32_t aud_op_audioloop_data_come(uint8_t *buf, uint32_t len)
{
    af_audio_pcmbuff_put(buf, len);
    if (a2dp_cache_status == AF_AUDIO_CACHE_QTY){
        a2dp_cache_status = AF_AUDIO_CACHE_OK;
    }
    return len;
}

static uint32_t aud_op_audioloop_more_data(uint8_t *buf, uint32_t len)
{
    if (a2dp_cache_status != AF_AUDIO_CACHE_QTY){
        af_audio_pcmbuff_get((uint8_t *)af_audioloop_play_cache, len/2);
        af_copy_track_one_to_two_16bits((int16_t *)buf, af_audioloop_play_cache, len/2/2);
    }
    return len;
}

int aud_op_audioloop(bool on)
{
    uint8_t *buff_play = NULL;
    uint8_t *buff_capture = NULL;
    uint8_t *buff_loop = NULL;
    struct AF_STREAM_CONFIG_T stream_cfg;
    static bool isRun =  false;

    if (isRun==on)
        return 0;

    if (on){
        a2dp_cache_status = AF_AUDIO_CACHE_QTY;
        af_audio_mempool_init(audio_buff, sizeof(audio_buff));
        af_audio_mempool_get_buff(&buff_capture, AUD_OP_BUFF_SIZE);
        af_audio_mempool_get_buff(&buff_play, AUD_OP_BUFF_SIZE*2);
        af_audio_mempool_get_buff((uint8_t **)&af_audioloop_play_cache, AUD_OP_BUFF_SIZE*2/2/2);
        af_audio_mempool_get_buff(&buff_loop, AUD_OP_BUFF_SIZE<<2);
        af_audio_pcmbuff_init(buff_loop, AUD_OP_BUFF_SIZE<<2);
        stream_cfg.bits = AUD_BITS_16;
        stream_cfg.channel_num = AUD_CHANNEL_NUM_1;
        stream_cfg.sample_rate = AUD_SAMPRATE_8000;
#if FPGA==0
        stream_cfg.device = AUD_STREAM_USE_INT_CODEC;
#else
        stream_cfg.device = AUD_STREAM_USE_EXT_CODEC;
#endif
        stream_cfg.vol = 0;
        stream_cfg.io_path = AUD_INPUT_PATH_MAINMIC;
        stream_cfg.handler = aud_op_audioloop_data_come;

        stream_cfg.data_ptr = AUD_OP_CACHE_2_UNCACHE(buff_capture);
        stream_cfg.data_size = AUD_OP_BUFF_SIZE;
        af_stream_open(AUD_STREAM_ID_0, AUD_STREAM_CAPTURE, &stream_cfg);

        stream_cfg.channel_num = AUD_CHANNEL_NUM_2;
        stream_cfg.io_path = AUD_OUTPUT_PATH_SPEAKER;
        stream_cfg.handler = aud_op_audioloop_more_data;

        stream_cfg.data_ptr = AUD_OP_CACHE_2_UNCACHE(buff_play);
        stream_cfg.data_size = AUD_OP_BUFF_SIZE*2;
        af_stream_open(AUD_STREAM_ID_0, AUD_STREAM_PLAYBACK, &stream_cfg);

        af_stream_start(AUD_STREAM_ID_0, AUD_STREAM_PLAYBACK);
        af_stream_start(AUD_STREAM_ID_0, AUD_STREAM_CAPTURE);

        aud_op_skpcfg_update(txrx_chain_cfg.txpa_gain, txrx_chain_cfg.sdm_gain, txrx_chain_cfg.sdac_vol);
        aud_op_miccfg_update(txrx_chain_cfg.mic_gain1, txrx_chain_cfg.mic_gain2, txrx_chain_cfg.sadc_vol);

    } else {
        af_stream_stop(AUD_STREAM_ID_0, AUD_STREAM_CAPTURE);
        af_stream_stop(AUD_STREAM_ID_0, AUD_STREAM_PLAYBACK);
        af_stream_close(AUD_STREAM_ID_0, AUD_STREAM_CAPTURE);
        af_stream_close(AUD_STREAM_ID_0, AUD_STREAM_PLAYBACK);
    }

    isRun=on;
    return 0;
}

const int16_t sine_patten[] = {
    0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170,0,23170, 32767,23170,0,-23170, -32767, -23170
};

static uint32_t aud_op_sinetone_more_data(uint8_t *buf, uint32_t len)
{
    af_copy_track_one_to_two_16bits((int16_t *)buf, (int16_t *)sine_patten, len/2/2);
    return len;
}

int aud_op_sinetone(bool on)
{
    uint8_t *buff_play = NULL;
    struct AF_STREAM_CONFIG_T stream_cfg;
    static bool isRun =  false;

    if (isRun==on)
        return 0;

    if (on){
        af_audio_mempool_init(audio_buff, sizeof(audio_buff));
        af_audio_mempool_get_buff(&buff_play, AUD_OP_BUFF_SIZE*2);
        stream_cfg.bits = AUD_BITS_16;
        stream_cfg.sample_rate = AUD_SAMPRATE_8000;
#if FPGA==0
        stream_cfg.device = AUD_STREAM_USE_INT_CODEC;
#else
        stream_cfg.device = AUD_STREAM_USE_EXT_CODEC;
#endif

        stream_cfg.channel_num = AUD_CHANNEL_NUM_2;
        stream_cfg.io_path = AUD_OUTPUT_PATH_SPEAKER;
        stream_cfg.handler = aud_op_sinetone_more_data;

        stream_cfg.data_ptr = AUD_OP_CACHE_2_UNCACHE(buff_play);
        stream_cfg.data_size = AUD_OP_BUFF_SIZE*2;
        af_stream_open(AUD_STREAM_ID_0, AUD_STREAM_PLAYBACK, &stream_cfg);
        af_stream_start(AUD_STREAM_ID_0, AUD_STREAM_PLAYBACK);

        aud_op_skpcfg_update(txrx_chain_cfg.txpa_gain, txrx_chain_cfg.sdm_gain, txrx_chain_cfg.sdac_vol);
    } else {
        af_stream_stop(AUD_STREAM_ID_0, AUD_STREAM_PLAYBACK);
        af_stream_close(AUD_STREAM_ID_0, AUD_STREAM_PLAYBACK);
    }

    isRun=on;
    return 0;
}

enum ERR_CODE aud_op_handle_cmd(enum AUD_OP_TYPE cmd, unsigned char *param, unsigned int len)
{
    enum ERR_CODE nRet = ERR_NONE;
    uint8_t cret[7];

    cret[0] = ERR_NONE;

    switch (cmd) {
        case AUD_OP_CONFIG_SET: {
            memcpy(&txrx_chain_cfg,param,sizeof(struct AUD_OP_TXRX_CHAIN_CFG_T));            
            aud_op_skpcfg_update(txrx_chain_cfg.txpa_gain, txrx_chain_cfg.sdm_gain, txrx_chain_cfg.sdac_vol);
            aud_op_miccfg_update(txrx_chain_cfg.mic_gain1, txrx_chain_cfg.mic_gain2, txrx_chain_cfg.sadc_vol);
            extend_cmd_send_reply(cret,1);
            break;
        }
        case AUD_OP_CONFIG_GET: {            
            memcpy(&cret[1],&txrx_chain_cfg,sizeof(struct AUD_OP_TXRX_CHAIN_CFG_T));
            extend_cmd_send_reply(cret,7);
            break;
        }
        case AUD_OP_SINETONE: {
            if (*param)
                aud_op_sinetone(true);
            else
                aud_op_sinetone(false);
            extend_cmd_send_reply(cret,1);
            break;
        }
        case AUD_OP_LOOPBACK: {
            if (*param)
                aud_op_audioloop(true);
            else
                aud_op_audioloop(false);
            extend_cmd_send_reply(cret,1);
            break;
        }
        default: {
            TRACE("Invalid command: 0x%x", cmd);
            return ERR_INTERNAL;
        }
    }   

    return nRet;
}


