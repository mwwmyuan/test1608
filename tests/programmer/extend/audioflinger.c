#include "audioflinger.h"
#include "hal_dma.h"
#include "hal_i2s.h"
#include "hal_codec.h"
#include "hal_spdif.h"
#include "hal_btpcm.h"
#include "codec_tlv32aic32.h"
#include "codec_best1000.h"

#include "string.h"
#include "cqueue.h"

#include "hal_trace.h"
#include "hal_cmu.h"
#include "pmu.h"
#include "analog.h"


//#define AF_STREAM_ID_0_PLAYBACK_FADEOUT 1

/* config params */
#define AF_I2S_INST HAL_I2S_ID_0
#define AF_CODEC_INST HAL_CODEC_ID_0
#define AF_SPDIF_INST HAL_SPDIF_ID_0
#define AF_BTPCM_INST HAL_BTPCM_ID_0

/* internal use */
enum AF_BOOL_T{
    AF_FALSE = 0,
    AF_TRUE = 1
};

enum AF_RESULT_T{
    AF_RES_SUCESS = 0,
    AF_RES_FAILD = 1
};

#define PP_PINGPANG(v) \
    (v == PP_PING? PP_PANG: PP_PING)

#define AF_STACK_SIZE (2048)
#define AUDIO_BUFFER_COUNT (4)

//status machine
enum AF_STATUS_T{
    AF_STATUS_NULL = 0x00,
    AF_STATUS_OPEN_CLOSE = 0x01,
    AF_STATUS_STREAM_OPEN_CLOSE = 0x02,
    AF_STATUS_STREAM_START_STOP = 0x04,
    AF_STATUS_MASK = 0x07,
};

struct af_stream_ctl_t{
    enum AF_PP_T pp_index;		//pingpong operate
    uint8_t pp_cnt;				//use to count the lost signals
    uint8_t status;				//status machine
    enum AUD_STREAM_USE_DEVICE_T use_device;
};

struct af_stream_cfg_t {
    //used inside
    struct af_stream_ctl_t ctl;

    //dma buf parameters, RAM can be alloced in different way
    uint8_t *dma_buf_ptr;
    uint32_t dma_buf_size;

    //store stream cfg parameters
    struct AF_STREAM_CONFIG_T cfg;

    //dma cfg parameters
    struct HAL_DMA_DESC_T dma_desc[AUDIO_BUFFER_COUNT];
    struct HAL_DMA_CH_CFG_T dma_cfg;

    //callback function
    AF_STREAM_HANDLER_T handler;
};


struct af_stream_cfg_t af_stream[AUD_STREAM_ID_NUM][AUD_STREAM_NUM];

#define AF_TRACE_DEBUG()    //TRACE("%s:%d\n", __func__, __LINE__)


void af_lock_thread(void)
{
}

void af_unlock_thread(void)
{
}


//used by dma irq and af_thread
static inline struct af_stream_cfg_t *af_get_stream_role(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream)
{
    ASSERT(id<AUD_STREAM_ID_NUM, "[%s] id >= AUD_STREAM_ID_NUM", __func__);
    ASSERT(stream<AUD_STREAM_NUM, "[%s] stream >= AUD_STREAM_NUM", __func__);

    return &af_stream[id][stream];
}

static void af_set_status(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream, enum AF_STATUS_T status)
{
    struct af_stream_cfg_t *role = NULL;

    role = af_get_stream_role(id, stream);

    role->ctl.status |= status;
}

static void af_clear_status(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream, enum AF_STATUS_T status)
{
    struct af_stream_cfg_t *role = NULL;

    role = af_get_stream_role(id, stream);

    role->ctl.status &= ~status;
}

//get current stream config parameters
uint32_t af_stream_get_cfg(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream, struct AF_STREAM_CONFIG_T **cfg, bool needlock)
{
    struct af_stream_cfg_t *role = NULL;

    if (needlock)
        af_lock_thread();
    role = af_get_stream_role(id, stream);
    *cfg = &(role->cfg);
    if (needlock)
        af_unlock_thread();
    return AF_RES_SUCESS;
}

static void af_dma_irq_handler(uint8_t ch, uint32_t remain_dst_tsize, uint32_t error, struct HAL_DMA_DESC_T *lli)
{
    struct af_stream_cfg_t *role = NULL;
    uint32_t signals = 0;

    //initial parameter
    for(uint8_t id=0; id< AUD_STREAM_ID_NUM; id++)
    {
        for(uint8_t stream=0; stream < AUD_STREAM_NUM; stream++)
        {
            role = af_get_stream_role((enum AUD_STREAM_ID_T)id, (enum AUD_STREAM_T)stream);
//            role = &af_stream[id][stream];

            if(role->dma_cfg.ch == ch)
            {
                role->ctl.pp_index = PP_PINGPANG(role->ctl.pp_index);
                role->ctl.pp_cnt++;
                signals = 0x01 << (id * 2 + stream);
                break;
            }
        }
    }
    
    ASSERT(signals, "[%s] ERROR: channel id = %d", __func__, ch);

    for(uint8_t i=0; i<AUD_STREAM_ID_NUM * AUD_STREAM_NUM; i++)
    {
        if(signals & (0x01 << i))
        {
            role = af_get_stream_role((enum AUD_STREAM_ID_T)(i>>1), (enum AUD_STREAM_T)(i & 0x01));

            af_lock_thread();
            if (role->handler)
            {
                uint8_t *buf = role->dma_buf_ptr + PP_PINGPANG(role->ctl.pp_index) * (role->dma_buf_size / 2);
                role->ctl.pp_cnt = 0;
                role->handler(buf, role->dma_buf_size / 2);
                //measure task and irq speed
                if(role->ctl.pp_cnt > 1)
                {
                    //if this TRACE happened, thread is slow and irq is fast
                    //so you can use larger dma buff to solve this problem
                    //af_stream_open: cfg->data
                    TRACE("[%s] WARNING: role[%d] lost %d signals", __func__, signals-1, role->ctl.pp_cnt-1);
                }
            }
            af_unlock_thread();
        }
    }
}

uint32_t af_open(void)
{
    AF_TRACE_DEBUG();
    struct af_stream_cfg_t *role = NULL;

    af_lock_thread();

    //initial parameters
    for(uint8_t id=0; id< AUD_STREAM_ID_NUM; id++)
    {
        for(uint8_t stream=0; stream < AUD_STREAM_NUM; stream++)
        {
            role = af_get_stream_role((enum AUD_STREAM_ID_T)id, (enum AUD_STREAM_T)stream);

            if(role->ctl.status == AF_STATUS_NULL)
            {
                role->dma_buf_ptr = NULL;
                role->dma_buf_size = 0;
                role->ctl.pp_index = PP_PING;
                role->ctl.status = AF_STATUS_OPEN_CLOSE;
                role->ctl.use_device = AUD_STREAM_USE_DEVICE_NULL;
                role->dma_cfg.ch = 0xff;
            }
            else
            {
                ASSERT(0, "[%s] ERROR: id = %d, stream = %d", __func__, id, stream);
            }
        }
    }

    af_unlock_thread();

    return AF_RES_SUCESS;
}

//Support memory<-->peripheral
// Note:Do not support peripheral <--> peripheral
uint32_t af_stream_open(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream, struct AF_STREAM_CONFIG_T *cfg)
{
    AF_TRACE_DEBUG();
    int dma_periph = 0;
    struct af_stream_cfg_t *role = NULL;
    enum AUD_STREAM_USE_DEVICE_T device;

    struct HAL_DMA_CH_CFG_T *dma_cfg = NULL;
    struct HAL_DMA_DESC_T *dma_desc = NULL;

    role = af_get_stream_role(id, stream);
    TRACE("[%s] id = %d, stream = %d", __func__, id, stream);

    //check af is open
    if(role->ctl.status != AF_STATUS_OPEN_CLOSE)
    {
        TRACE("[%s] ERROR: status = %d",__func__, role->ctl.status);
        return AF_RES_FAILD;
    }

	ASSERT(cfg->data_ptr != NULL, "[%s] ERROR: data_ptr == NULL!!!", __func__);
	ASSERT(cfg->data_size !=0, "[%s] ERROR: data_size == 0!!!", __func__);

    AF_TRACE_DEBUG();

    af_lock_thread();

    device = cfg->device;
    role->ctl.use_device = device;

    dma_desc = &role->dma_desc[0];

    dma_cfg = &role->dma_cfg;
    memset(dma_cfg, 0, sizeof(*dma_cfg));
    dma_cfg->dst_bsize = HAL_DMA_BSIZE_4;
    dma_cfg->src_bsize = HAL_DMA_BSIZE_4;
    if (device == AUD_STREAM_USE_INT_CODEC && cfg->bits == AUD_BITS_24
            && stream == AUD_STREAM_PLAYBACK)
    {
        dma_cfg->dst_width = HAL_DMA_WIDTH_WORD;
        dma_cfg->src_width = HAL_DMA_WIDTH_WORD;
        dma_cfg->src_tsize = cfg->data_size / AUDIO_BUFFER_COUNT / 4; /* cause of word width */
    }
    else if (device == AUD_STREAM_USE_DPD_RX && stream == AUD_STREAM_CAPTURE)
    {
        dma_cfg->dst_width = HAL_DMA_WIDTH_WORD;
        dma_cfg->src_width = HAL_DMA_WIDTH_WORD;
        dma_cfg->src_tsize = cfg->data_size / AUDIO_BUFFER_COUNT / 4; /* cause of word width */
    }    
    else
    {
        dma_cfg->dst_width = HAL_DMA_WIDTH_HALFWORD;
        dma_cfg->src_width = HAL_DMA_WIDTH_HALFWORD;
        dma_cfg->src_tsize = cfg->data_size / AUDIO_BUFFER_COUNT / 2; /* cause of half word width */
    }
    dma_cfg->try_burst = 1;
    dma_cfg->handler = af_dma_irq_handler;

    if(stream ==  AUD_STREAM_PLAYBACK)
    {
        AF_TRACE_DEBUG();
        dma_cfg->src_periph = (enum HAL_DMA_PERIPH_T)0;
        dma_cfg->type = HAL_DMA_FLOW_M2P_DMA;

        //open device and stream
        if(device == AUD_STREAM_USE_EXT_CODEC)
        {
            AF_TRACE_DEBUG();
            tlv32aic32_open();
            tlv32aic32_stream_open(stream);

            dma_cfg->dst_periph = HAL_AUDMA_I2S_TX;
            dma_periph = HAL_AUDMA_I2S_TX;
        }
		else if(device == AUD_STREAM_USE_I2S)
		{
			AF_TRACE_DEBUG();
            hal_i2s_open(AF_I2S_INST);
            dma_cfg->dst_periph = HAL_AUDMA_I2S_TX;
            dma_periph = HAL_AUDMA_I2S_TX;
		}
        else if(device == AUD_STREAM_USE_INT_CODEC)
        {
            AF_TRACE_DEBUG();
            codec_best1000_open();
            codec_best1000_stream_open(stream);

            dma_cfg->dst_periph = HAL_AUDMA_CODEC_TX;
            dma_periph = HAL_AUDMA_CODEC_TX;
        }
        else if(device == AUD_STREAM_USE_INT_SPDIF)
        {
            AF_TRACE_DEBUG();
            hal_spdif_open(AF_SPDIF_INST);
            dma_cfg->dst_periph = HAL_AUDMA_SPDIF0_TX;
            dma_periph = HAL_AUDMA_SPDIF0_TX;
        }
        else if(device == AUD_STREAM_USE_BT_PCM)
        {
            AF_TRACE_DEBUG();
            hal_btpcm_open(AF_BTPCM_INST);
            dma_cfg->dst_periph = HAL_AUDMA_BTPCM_TX;
            dma_periph = HAL_AUDMA_BTPCM_TX;
        }
		else
		{
			ASSERT(0, "[%s] ERROR: device %d is not defined!", __func__, device);
		}
    }
    else
    {
        AF_TRACE_DEBUG();
        dma_cfg->dst_periph = (enum HAL_DMA_PERIPH_T)0;
        dma_cfg->type = HAL_DMA_FLOW_P2M_DMA;

        //open device and stream
        if(device == AUD_STREAM_USE_EXT_CODEC)
        {
            AF_TRACE_DEBUG();
            tlv32aic32_open();
            tlv32aic32_stream_open(stream);

            dma_cfg->src_periph = HAL_AUDMA_I2S_RX;
            dma_periph = HAL_AUDMA_I2S_RX;
        }
		else if(device == AUD_STREAM_USE_I2S)
		{
            AF_TRACE_DEBUG();
            hal_i2s_open(AF_I2S_INST);

            dma_cfg->src_periph = HAL_AUDMA_I2S_RX;
            dma_periph = HAL_AUDMA_I2S_RX;
		}
        else if(device == AUD_STREAM_USE_INT_CODEC)
        {
            AF_TRACE_DEBUG();
            codec_best1000_open();
            codec_best1000_stream_open(stream);

            dma_cfg->src_periph = HAL_AUDMA_CODEC_RX;
            dma_periph = HAL_AUDMA_CODEC_RX;
        }
        else if(device == AUD_STREAM_USE_INT_SPDIF)
        {
            AF_TRACE_DEBUG();
            hal_spdif_open(AF_SPDIF_INST);

            dma_cfg->src_periph = HAL_AUDMA_SPDIF0_RX;
            dma_periph = HAL_AUDMA_SPDIF0_RX;
        }
        else if(device == AUD_STREAM_USE_BT_PCM)
        {
            AF_TRACE_DEBUG();
            hal_btpcm_open(AF_BTPCM_INST);

            dma_cfg->src_periph = HAL_AUDMA_BTPCM_RX;
            dma_periph = HAL_AUDMA_BTPCM_RX;
        }
        else if(device == AUD_STREAM_USE_DPD_RX)
        {
            AF_TRACE_DEBUG();
            dma_cfg->src_periph = HAL_AUDMA_DPD_RX;
            dma_periph = HAL_AUDMA_DPD_RX;
        }  
		else
		{
			ASSERT(0, "[%s] ERROR: device %d is not defined!", __func__, device);
		}
    }

    dma_cfg->ch = hal_audma_get_chan(dma_periph, HAL_DMA_HIGH_PRIO);

    AF_TRACE_DEBUG();
    role->dma_buf_ptr = cfg->data_ptr;
    role->dma_buf_size = cfg->data_size;

    if (stream == AUD_STREAM_PLAYBACK) {
        AF_TRACE_DEBUG();
        dma_cfg->src = (uint32_t)(role->dma_buf_ptr);
        hal_audma_init_desc(&dma_desc[0], dma_cfg, &dma_desc[1], 0);
        dma_cfg->src = (uint32_t)(role->dma_buf_ptr + role->dma_buf_size/4);
        hal_audma_init_desc(&dma_desc[1], dma_cfg, &dma_desc[2], 1);
        dma_cfg->src = (uint32_t)(role->dma_buf_ptr + role->dma_buf_size/2);
        hal_audma_init_desc(&dma_desc[2], dma_cfg, &dma_desc[3], 0);
        dma_cfg->src = (uint32_t)(role->dma_buf_ptr + (role->dma_buf_size/4)*3);
        hal_audma_init_desc(&dma_desc[3], dma_cfg, &dma_desc[0], 1);
    }
    else {
        AF_TRACE_DEBUG();
        dma_cfg->dst = (uint32_t)(role->dma_buf_ptr);
        hal_audma_init_desc(&dma_desc[0], dma_cfg, &dma_desc[1], 0);
        dma_cfg->dst = (uint32_t)(role->dma_buf_ptr + role->dma_buf_size/4);
        hal_audma_init_desc(&dma_desc[1], dma_cfg, &dma_desc[2], 1);
        dma_cfg->dst = (uint32_t)(role->dma_buf_ptr + role->dma_buf_size/2);
        hal_audma_init_desc(&dma_desc[2], dma_cfg, &dma_desc[3], 0);
        dma_cfg->dst = (uint32_t)(role->dma_buf_ptr + (role->dma_buf_size/4)*3);
        hal_audma_init_desc(&dma_desc[3], dma_cfg, &dma_desc[0], 1);
    }

    AF_TRACE_DEBUG();
    role->handler = cfg->handler;

    af_set_status(id, stream, AF_STATUS_STREAM_OPEN_CLOSE);

    af_stream_setup(id, stream, cfg);

    af_unlock_thread();

    return AF_RES_SUCESS;
}

//volume, path, sample rate, channel num ...
uint32_t af_stream_setup(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream, struct AF_STREAM_CONFIG_T *cfg)
{
    AF_TRACE_DEBUG();
    struct af_stream_cfg_t *role = NULL;

    enum AUD_STREAM_USE_DEVICE_T device;

    role = af_get_stream_role(id, stream);
    device = role->ctl.use_device;

    //check stream is open
    if(!(role->ctl.status & AF_STATUS_STREAM_OPEN_CLOSE))
    {
        TRACE("[%s] ERROR: status = %d",__func__, role->ctl.status);
        return AF_RES_FAILD;
    }

    af_lock_thread();
    role->cfg = *cfg;

    AF_TRACE_DEBUG();
    if(device == AUD_STREAM_USE_EXT_CODEC)
    {
        AF_TRACE_DEBUG();

		struct tlv32aic32_config_t tlv32aic32_cfg;
		tlv32aic32_cfg.bits = cfg->bits;
		tlv32aic32_cfg.channel_num = cfg->channel_num;
		tlv32aic32_cfg.sample_rate = cfg->sample_rate;
		tlv32aic32_cfg.use_dma = AF_TRUE;
		tlv32aic32_stream_setup(stream, &tlv32aic32_cfg);
    }
	else if(device == AUD_STREAM_USE_I2S)
	{
		AF_TRACE_DEBUG();

		struct HAL_I2S_CONFIG_T i2s_cfg;
		i2s_cfg.bits = cfg->bits;
		i2s_cfg.channel_num = cfg->channel_num;
		i2s_cfg.sample_rate = cfg->sample_rate;
		i2s_cfg.use_dma = AF_TRUE;

		hal_i2s_setup_stream(AF_I2S_INST, stream, &i2s_cfg);
	}
    else if(device == AUD_STREAM_USE_INT_CODEC)
    {
        AF_TRACE_DEBUG();
        struct HAL_CODEC_CONFIG_T codec_cfg;
        codec_cfg.bits = cfg->bits;
        codec_cfg.sample_rate = cfg->sample_rate;
        codec_cfg.channel_num = cfg->channel_num;
        codec_cfg.use_dma = AF_TRUE;
        codec_cfg.vol = cfg->vol;
        codec_cfg.io_path = cfg->io_path;

        AF_TRACE_DEBUG();
        codec_best1000_stream_setup(stream, &codec_cfg);
    }
    else if(device == AUD_STREAM_USE_INT_SPDIF)
    {
        AF_TRACE_DEBUG();
        struct HAL_SPDIF_CONFIG_T spdif_cfg;
        spdif_cfg.use_dma = AF_TRUE;
        spdif_cfg.bits = cfg->bits;
        spdif_cfg.channel_num = cfg->channel_num;
        spdif_cfg.sample_rate = cfg->sample_rate;
        hal_spdif_setup_stream(AF_SPDIF_INST, stream, &spdif_cfg);
    }
    else if(device == AUD_STREAM_USE_BT_PCM)
    {
        AF_TRACE_DEBUG();
        struct HAL_BTPCM_CONFIG_T btpcm_cfg;
        btpcm_cfg.use_dma = AF_TRUE;
        btpcm_cfg.bits = cfg->bits;
        btpcm_cfg.channel_num = cfg->channel_num;
        btpcm_cfg.sample_rate = cfg->sample_rate;
        hal_btpcm_setup_stream(AF_BTPCM_INST, stream, &btpcm_cfg);
    }
    else if(device == AUD_STREAM_USE_DPD_RX)
    {
        AF_TRACE_DEBUG();
    }
	else
	{
		ASSERT(0, "[%s] ERROR: device %d is not defined!", __func__, device);
	}

    AF_TRACE_DEBUG();
    af_unlock_thread();

    return AF_RES_SUCESS;
}

uint32_t af_stream_start(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream)
{
    AF_TRACE_DEBUG();
    struct af_stream_cfg_t *role = NULL;
    enum AUD_STREAM_USE_DEVICE_T device;

    role = af_get_stream_role(id, stream);
    device = role->ctl.use_device;

    //check stream is open and not start.
    if(role->ctl.status != (AF_STATUS_OPEN_CLOSE | AF_STATUS_STREAM_OPEN_CLOSE))
    {
        TRACE("[%s] ERROR: status = %d",__func__, role->ctl.status);
        return AF_RES_FAILD;
    }

    af_lock_thread();

    hal_audma_sg_start(&role->dma_desc[0], &role->dma_cfg);

    AF_TRACE_DEBUG();
    if(device == AUD_STREAM_USE_EXT_CODEC)
    {
        AF_TRACE_DEBUG();
        tlv32aic32_stream_start(stream);
    }
	else if(device == AUD_STREAM_USE_I2S)
	{
		hal_i2s_start_stream(AF_I2S_INST, stream);
	}
    else if(device == AUD_STREAM_USE_INT_CODEC)
    {
        AF_TRACE_DEBUG();
        codec_best1000_stream_start(stream);
    }
    else if(device == AUD_STREAM_USE_INT_SPDIF)
    {
        AF_TRACE_DEBUG();
        hal_spdif_start_stream(AF_SPDIF_INST, stream);
    }
    else if(device == AUD_STREAM_USE_BT_PCM)
    {
        AF_TRACE_DEBUG();
        hal_btpcm_start_stream(AF_BTPCM_INST, stream);
    }
    else if(device == AUD_STREAM_USE_DPD_RX)
    {
        AF_TRACE_DEBUG();
        hal_btpcm_start_stream(AF_BTPCM_INST, stream);
    }
    else
    {
        ASSERT(0, "[%s] ERROR: device %d is not defined!", __func__, device);
    }

    AF_TRACE_DEBUG();
    af_set_status(id, stream, AF_STATUS_STREAM_START_STOP);

    af_unlock_thread();

    return AF_RES_SUCESS;
}

uint32_t af_stream_stop(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream)
{
    AF_TRACE_DEBUG();
    struct af_stream_cfg_t *role = NULL;
    enum AUD_STREAM_USE_DEVICE_T device;

    role = af_get_stream_role(id, stream);
    device = role->ctl.use_device;

    //check stream is start and not stop
    if(role->ctl.status != (AF_STATUS_OPEN_CLOSE | AF_STATUS_STREAM_OPEN_CLOSE | AF_STATUS_STREAM_START_STOP))
    {
        TRACE("[%s] ERROR: status = %d",__func__, role->ctl.status);
        return AF_RES_FAILD;
    }

    af_lock_thread();

    hal_audma_stop(role->dma_cfg.ch);
    
    if(device == AUD_STREAM_USE_EXT_CODEC)
    {
        AF_TRACE_DEBUG();
        tlv32aic32_stream_stop(stream);
    }
	else if(device == AUD_STREAM_USE_I2S)
	{
        AF_TRACE_DEBUG();
        hal_i2s_stop_stream(AF_I2S_INST, stream);
	}
    else if(device == AUD_STREAM_USE_INT_CODEC)
    {
        AF_TRACE_DEBUG();
        codec_best1000_stream_stop(stream);
    }
    else if(device == AUD_STREAM_USE_INT_SPDIF)
    {
        AF_TRACE_DEBUG();
        hal_spdif_stop_stream(AF_SPDIF_INST, stream);
    }
    else if(device == AUD_STREAM_USE_BT_PCM)
    {
        AF_TRACE_DEBUG();
        hal_btpcm_stop_stream(AF_BTPCM_INST, stream);
    }
    else
    {
        ASSERT(0, "[%s] ERROR: device %d is not defined!", __func__, device);
    }

#ifndef _AUDIO_NO_THREAD_
    //deal with thread
#endif


    AF_TRACE_DEBUG();
    af_clear_status(id, stream, AF_STATUS_STREAM_START_STOP);

    af_unlock_thread();

    return AF_RES_SUCESS;
}

uint32_t af_stream_close(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream)
{
    AF_TRACE_DEBUG();
    struct af_stream_cfg_t *role = NULL;
    enum AUD_STREAM_USE_DEVICE_T device;

    role = af_get_stream_role(id, stream);
    device = role->ctl.use_device;

    //check stream is stop and not close.
    if(role->ctl.status != (AF_STATUS_OPEN_CLOSE | AF_STATUS_STREAM_OPEN_CLOSE))
    {
        TRACE("[%s] ERROR: status = %d",__func__, role->ctl.status);
        return AF_RES_FAILD;
    }

    AF_TRACE_DEBUG();
    af_lock_thread();

    memset(role->dma_buf_ptr, 0, role->dma_buf_size);
    hal_audma_free_chan(role->dma_cfg.ch);

    //	TODO: more parameter should be set!!!
//    memset(role, 0xff, sizeof(struct af_stream_cfg_t));
    role->handler = NULL;
    role->ctl.pp_index = PP_PING;
    role->ctl.use_device = AUD_STREAM_USE_DEVICE_NULL;
    role->dma_buf_ptr = NULL;
    role->dma_buf_size = 0;

    role->dma_cfg.ch = 0xff;

    if(device == AUD_STREAM_USE_EXT_CODEC)
    {
        AF_TRACE_DEBUG();
        tlv32aic32_stream_close(stream);
    }
	else if(device == AUD_STREAM_USE_I2S)
	{
		AF_TRACE_DEBUG();
        hal_i2s_close(AF_I2S_INST);
	}
    else if(device == AUD_STREAM_USE_INT_CODEC)
    {
        AF_TRACE_DEBUG();
		codec_best1000_stream_close(stream);
		codec_best1000_close();
    }
    else if(device == AUD_STREAM_USE_INT_SPDIF)
    {
        AF_TRACE_DEBUG();
        hal_spdif_close(AF_SPDIF_INST);
    }
    else if(device == AUD_STREAM_USE_BT_PCM)
    {
        AF_TRACE_DEBUG();
        hal_btpcm_close(AF_BTPCM_INST);
    }
    else if(device == AUD_STREAM_USE_DPD_RX)
    {
        AF_TRACE_DEBUG();
    }    
    else
    {
        ASSERT(0, "[%s] ERROR: device %d is not defined!", __func__, device);
    }

#ifndef _AUDIO_NO_THREAD_
    //deal with thread
#endif

    AF_TRACE_DEBUG();
    af_clear_status(id, stream, AF_STATUS_STREAM_OPEN_CLOSE);

    af_unlock_thread();

    return AF_RES_SUCESS;
}

uint32_t af_close(void)
{
    AF_TRACE_DEBUG();
    struct af_stream_cfg_t *role = NULL;

    af_lock_thread();

    for(uint8_t id=0; id< AUD_STREAM_ID_NUM; id++)
    {
        for(uint8_t stream=0; stream < AUD_STREAM_NUM; stream++)
        {
            role = af_get_stream_role((enum AUD_STREAM_ID_T)id, (enum AUD_STREAM_T)stream);

            if(role->ctl.status == AF_STATUS_OPEN_CLOSE)
            {
                role->ctl.status = AF_STATUS_NULL;
            }
            else
            {
                ASSERT(0, "[%s] ERROR: id = %d, stream = %d", __func__, id, stream);
            }

        }
    }

    af_unlock_thread();

    return AF_RES_SUCESS;
}

static uint8_t *app_audio_buff;
static uint32_t buff_size_used;
static uint32_t app_audio_buffer_size;

int af_audio_mempool_init(uint8_t *buff, uint16_t len)
{
    app_audio_buff = buff;
    app_audio_buffer_size = len;
    buff_size_used = 0;
    //if do not memset, memset in app level
    memset(app_audio_buff, 0, app_audio_buffer_size);

    return 0;
}

uint32_t af_audio_mempool_free_buff_size(void)
{
    return app_audio_buffer_size - buff_size_used;
}

int af_audio_mempool_get_buff(uint8_t **buff, uint32_t size)
{
    uint32_t buff_size_free;

    buff_size_free = af_audio_mempool_free_buff_size();

    if (size % 4){
        size = size + (4 - size % 4);
    }

    ASSERT(size <= buff_size_free, "[%s] size = %d > free size = %d", __func__, size, buff_size_free);

    *buff = app_audio_buff + buff_size_used;

    buff_size_used += size;

    return 0;
}

static CQueue af_audio_pcm_queue;

int af_audio_pcmbuff_init(uint8_t *buff, uint16_t len)
{
    InitCQueue(&af_audio_pcm_queue, len, buff);
    memset(buff, 0x00, len);

    return 0;
}

int af_audio_pcmbuff_length(void)
{
    int len;

    len = LengthOfCQueue(&af_audio_pcm_queue);

    return len;
}

int af_audio_pcmbuff_put(uint8_t *buff, uint16_t len)
{
    int status;

    status = EnCQueue(&af_audio_pcm_queue, buff, len);

    return status;
}

int af_audio_pcmbuff_get(uint8_t *buff, uint16_t len)
{
    unsigned char *e1 = NULL, *e2 = NULL;
    unsigned int len1 = 0, len2 = 0;
    int status;

    status = PeekCQueue(&af_audio_pcm_queue, len, &e1, &len1, &e2, &len2);
    if (len==(len1+len2)){
        memcpy(buff,e1,len1);
        memcpy(buff+len1,e2,len2);
        DeCQueue(&af_audio_pcm_queue, 0, len);
        DeCQueue(&af_audio_pcm_queue, 0, len2);
    }else{
        memset(buff, 0x00, len);
        status = -1;
    }

    return status;
}

int af_audio_pcmbuff_discard(uint16_t len)
{
    int status;

    status = DeCQueue(&af_audio_pcm_queue, 0, len);

    return status;
}

void af_copy_track_one_to_two_16bits(int16_t *dst_buf, int16_t *src_buf, uint32_t src_len)
{
    uint32_t i = 0;
    for (i = 0; i < src_len; i++) {
        //dst_buf[i*2 + 0] = dst_buf[i*2 + 1] = ((unsigned short)(src_buf[i])<<1);
        dst_buf[i*2 + 0] = dst_buf[i*2 + 1] = src_buf[i];
    }
}

