#include "cmsis_os.h"
#include "cmsis.h"
#include "audioflinger.h"
#include "hal_dma.h"
#include "hal_i2s.h"
#include "hal_codec.h"
#include "hal_spdif.h"
#include "hal_btpcm.h"
#include "codec_tlv32aic32.h"
#include "codec_best1000.h"

#include "string.h"

#include "hal_trace.h"
#include "hal_cmu.h"
#include "hal_timer.h"
#include "pmu.h"
#include "analog.h"

//#define AF_STREAM_ID_0_PLAYBACK_FADEOUT

//#define CORRECT_SAMPLE_VALUE
#define AF_LOC __attribute__((section(".fast_text_sram")))

#define RTOS

#define AF_FADE_OUT_SIGNAL_ID           15
#define AF_FADE_IN_SIGNAL_ID            14

#define AF_CODEC_RIGHT_CHAN_ATTN        0.968277856 // -0.28 dB

#define AF_CODEC_VOL_UPDATE_STEP        0.00002

#define AF_CODEC_DC_MAX_SCALE           32767

#define AF_CODEC_DC_STABLE_INTERVAL     MS_TO_TICKS(5)
#define AF_CODEC_PA_RESTART_INTERVAL    MS_TO_TICKS(20)

#define AF_CODEC_FADE_PERIOD_MS         60
#define AF_CODEC_FADE_MIN_SAMPLE_CNT    200

enum AF_DAC_PA_STATE_T {
    AF_DAC_PA_NULL,
    AF_DAC_PA_ON_TRIGGER,
    AF_DAC_PA_ON_SOFT_START,
    AF_DAC_PA_OFF_TRIGGER,
    AF_DAC_PA_OFF_SOFT_END,
    AF_DAC_PA_OFF_SOFT_END_DONE,
    AF_DAC_PA_OFF_DC_START,
};

#ifdef AUDIO_SW_OUTPUT_GAIN
static float cur_output_coef[2];
static float new_output_coef[2];
#else
static uint8_t cur_data_shift[2];
static uint8_t new_data_shift[2];
static int32_t prev_data[2];
#endif

#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
static float dac_dc_gain_attn;
static int16_t dac_dc[2];
static bool dac_dc_valid;
static volatile enum AF_DAC_PA_STATE_T dac_pa_state;
static uint32_t dac_dc_start_time;
static uint32_t dac_pa_stop_time;

#ifdef AUDIO_SW_OUTPUT_GAIN
static const float fade_curve[] = {
    0.00000000, 0.00001334, 0.00005623, 0.00017783, 0.00056234, 0.00177828, 0.00562341, 0.01778279,
    0.05623413, 0.17782794, 0.56234133, 0.80560574, 0.86305408, 0.89733331, 0.92184771, 0.94094446,
    0.95659002, 0.96984292, 0.98133882, 0.99148953, 1.00000000,
};
STATIC_ASSERT(ARRAY_SIZE(fade_curve) >= 2, "Invalid fade curve");
static uint16_t fade_curve_idx[2];
static uint16_t fade_step_idx[2];
static float fade_gain_step[2];
static float fade_gain_range[2];
static uint16_t fade_sample_cnt;
static uint16_t fade_sample_proc;
static uint16_t fade_step_cnt;
static float saved_output_coef;
#ifdef RTOS
static osThreadId fade_thread_id;
#endif
#ifdef AF_STREAM_ID_0_PLAYBACK_FADEOUT
#error "Cannot eanble 2 kinds of fade out codes at the same time"
#endif
#endif
#endif

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

struct af_stream_fade_out_t{
    bool stop_on_process;
    uint8_t stop_process_cnt;
    osThreadId stop_request_tid;
    uint32_t need_fadeout_len;
    uint32_t need_fadeout_len_processed;
};


struct af_stream_cfg_t af_stream[AUD_STREAM_ID_NUM][AUD_STREAM_NUM];

#ifdef AF_STREAM_ID_0_PLAYBACK_FADEOUT
static struct af_stream_fade_out_t af_stream_fade_out ={
                                                .stop_on_process = false,
                                                .stop_process_cnt = 0,
                                                .stop_request_tid = NULL,
                                                .need_fadeout_len = 0,
                                                .need_fadeout_len_processed = 0,
};
#endif

#define AF_TRACE_DEBUG()    //TRACE("%s:%d\n", __func__, __LINE__)


static osThreadId af_thread_tid;

static void af_thread(void const *argument);
osThreadDef(af_thread, osPriorityAboveNormal, AF_STACK_SIZE);

osMutexId audioflinger_mutex_id = NULL;
osMutexDef(audioflinger_mutex);

void af_lock_thread(void)
{
    osMutexWait(audioflinger_mutex_id, osWaitForever);
}

void af_unlock_thread(void)
{
    osMutexRelease(audioflinger_mutex_id);
}

#ifdef AF_STREAM_ID_0_PLAYBACK_FADEOUT
int af_stream_fadeout_start(uint32_t sample)
{
    TRACE("fadein_config sample:%d", sample);
    af_stream_fade_out.need_fadeout_len = sample;
    af_stream_fade_out.need_fadeout_len_processed = sample;
    return 0;
}

int af_stream_fadeout_stop(void)
{
    af_stream_fade_out.stop_process_cnt = 0;
    af_stream_fade_out.stop_on_process = false;
    return 0;
}
uint32_t af_stream_fadeout(int16_t *buf, uint32_t len, enum AUD_CHANNEL_NUM_T num)
{
    uint32_t i;
    uint32_t j = 0;
    uint32_t start;
    uint32_t end;

    start = af_stream_fade_out.need_fadeout_len_processed;
    end = af_stream_fade_out.need_fadeout_len_processed  > len ?
           af_stream_fade_out.need_fadeout_len_processed - len :  0;

    if (start <= end){
//        TRACE("skip fadeout");
        memset(buf, 0, len*2);
        return len;
    }
//    TRACE("fadeout l:%d start:%d end:%d", len, start, end);
//    DUMP16("%05d ", buf, 10);
//    DUMP16("%05d ", buf+len-10, 10);


    if (num == AUD_CHANNEL_NUM_1){
        for (i = start; i > end; i--){
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
        }
    }else if (num == AUD_CHANNEL_NUM_2){
        for (i = start; i > end; i-=2){
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
        }
    }else if (num == AUD_CHANNEL_NUM_4){
        for (i = start; i > end; i-=4){
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
        }
    }else if (num == AUD_CHANNEL_NUM_8){
        for (i = start; i > end; i-=8){
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
            *(buf+j) = *(buf+j)*i/af_stream_fade_out.need_fadeout_len;
            j++;
        }

    }
    af_stream_fade_out.need_fadeout_len_processed -= j;

//    TRACE("out i:%d process:%d x:%d", i, af_stream_fade_out.need_fadeout_len_processed, end+((start-end)/AUD_CHANNEL_NUM_2));
//    DUMP16("%05d ", buf, 10);
//    DUMP16("%05d ", buf+len-10, 10);

    return len;
}

void af_stream_stop_wait_finish()
{
    af_lock_thread();
    af_stream_fade_out.stop_on_process = true;
    af_stream_fade_out.stop_request_tid = osThreadGetId();
    osSignalClear(af_stream_fade_out.stop_request_tid, (1 << AF_FADE_OUT_SIGNAL_ID));
    af_unlock_thread();
    osSignalWait((1 << AF_FADE_OUT_SIGNAL_ID), 300);
}

void af_stream_stop_process(struct af_stream_cfg_t *af_cfg, uint8_t *buf, uint32_t len)
{
    af_lock_thread();
    if (af_stream_fade_out.stop_on_process){
//        TRACE("%s num:%d size:%d len:%d cnt:%d", __func__, af_cfg->cfg.channel_num, af_cfg->cfg.data_size, len,  af_stream_fade_out.stop_process_cnt);
        af_stream_fadeout((int16_t *)buf, len/2, af_cfg->cfg.channel_num);

//        TRACE("process ret:%d %d %d", *(int16_t *)(buf+len-2-2-2), *(int16_t *)(buf+len-2-2), *(int16_t *)(buf+len-2));
        if (af_stream_fade_out.stop_process_cnt++>3){
            TRACE("stop_process end");
            osSignalSet(af_stream_fade_out.stop_request_tid, (1 << AF_FADE_OUT_SIGNAL_ID));
        }
    }
    af_unlock_thread();
}
#endif

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

#if 0
static void af_dump_cfg()
{
    struct af_stream_cfg_t *role = NULL;

    TRACE("[%s] dump cfg.........start");
    //initial parameter
    for(uint8_t id=0; id< AUD_STREAM_ID_NUM; id++)
    {
        for(uint8_t stream=0; stream < AUD_STREAM_NUM; stream++)
        {
            role = af_get_stream_role((enum AUD_STREAM_ID_T)id, (enum AUD_STREAM_T)stream);

            TRACE("id = %d, stream = %d:", id, stream);
            TRACE("ctl.use_device = %d", role->ctl.use_device);
            TRACE("cfg.device = %d", role->cfg.device);
            TRACE("dma_cfg.ch = %d", role->dma_cfg.ch);
        }
    }
    TRACE("[%s] dump cfg.........end");
}
#endif

#ifdef AUDIO_SW_OUTPUT_GAIN
static inline void af_codec_output_gain_changed(float coef)
{
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
    saved_output_coef = coef;
    if (dac_pa_state != AF_DAC_PA_NULL) {
        return;
    }
#endif

    new_output_coef[0] = coef;
    new_output_coef[1] = new_output_coef[0] * AF_CODEC_RIGHT_CHAN_ATTN;
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
    new_output_coef[0] *= dac_dc_gain_attn;
    new_output_coef[1] *= dac_dc_gain_attn;
#endif
}

AF_LOC static float af_codec_update_output_gain(int16_t val, int chan)
{
    float cur_gain;
    float tgt_gain;

    cur_gain = cur_output_coef[chan];
    tgt_gain = new_output_coef[chan];

#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
    float step;

    if (dac_pa_state == AF_DAC_PA_ON_SOFT_START) {
        step = fade_gain_step[chan] * fade_gain_range[chan];
        fade_step_idx[chan]++;
        if (fade_step_idx[chan] >= fade_step_cnt) {
            fade_step_idx[chan] = 0;
            fade_curve_idx[chan]++;
            if (fade_curve_idx[chan] < ARRAY_SIZE(fade_curve) - 1) {
                fade_gain_step[chan] = (fade_curve[fade_curve_idx[chan] + 1] - fade_curve[fade_curve_idx[chan]]) / fade_step_cnt;
            }
        }
    } else if (dac_pa_state == AF_DAC_PA_OFF_SOFT_END) {
        step = fade_gain_step[chan] * fade_gain_range[chan];
        fade_step_idx[chan]--;
        if (fade_step_idx[chan] == 0) {
            fade_step_idx[chan] = fade_step_cnt;
            if (fade_curve_idx[chan] > 0) {
                fade_curve_idx[chan]--;
            }
            if (fade_curve_idx[chan] > 0) {
                fade_gain_step[chan] = (fade_curve[fade_curve_idx[chan]] - fade_curve[fade_curve_idx[chan] - 1]) / fade_step_cnt;
            }
        }
    } else if (dac_pa_state == AF_DAC_PA_NULL) {
        step = AF_CODEC_VOL_UPDATE_STEP;
    } else {
        return cur_gain;
    }
#else
    static const float step = AF_CODEC_VOL_UPDATE_STEP;
#endif

    if (cur_gain > tgt_gain) {
        cur_gain -= step;
        if (cur_gain < tgt_gain) {
            cur_gain = tgt_gain;
        }
    } else {
        cur_gain += step;
        if (cur_gain > tgt_gain) {
            cur_gain = tgt_gain;
        }
    }

    return cur_gain;
}
#endif
#if defined( AUDIO_SW_OUTPUT_GAIN) || defined(AUDIO_OUTPUT_DIFF_DC_CALIB)

AF_LOC static void af_codec_post_data_loop(uint8_t *buf, uint32_t size, enum AUD_BITS_T bits, enum AUD_CHANNEL_NUM_T chans)
{
    uint32_t cnt;

    if (bits <= AUD_BITS_16) {
        int16_t *ptr16 = (int16_t *)buf;
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
        int16_t dc_l = dac_dc[0];
        int16_t dc_r = dac_dc[1];

#endif
        if (chans == AUD_CHANNEL_NUM_1) {
            cnt = size / sizeof(int16_t);
            while (cnt-- > 0) {
#ifdef AUDIO_SW_OUTPUT_GAIN
                // Update the gain
                if (new_output_coef[0] != cur_output_coef[0]) {
                    cur_output_coef[0] = af_codec_update_output_gain(*ptr16, 0);
                }
                // Apply the gain
                *ptr16 = (int16_t)(*ptr16 * cur_output_coef[0]);
#else // Shift data right
                // Zero-crossing detection
                if (new_data_shift[0] != cur_data_shift[0]) {
                    if ((prev_data[0] >= 0 && ptr16[0] <= 0) || (prev_data[0] <= 0 && ptr16[0] >= 0)) {
                        cur_data_shift[0] = new_data_shift[0];
                    }
                }
                // Save previous data and shift data
                prev_data[0] = *ptr16;
                *ptr16 >>= cur_data_shift[0];
#endif
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
                *ptr16 = __SSAT(*ptr16 + dc_l, 16);
#endif
                ptr16++;
            }
        } else {
            cnt = size / sizeof(int16_t) / 2;
            while (cnt-- > 0) {
#ifdef AUDIO_SW_OUTPUT_GAIN
                // Update the gain
                if (new_output_coef[0] != cur_output_coef[0]) {
                    cur_output_coef[0] = af_codec_update_output_gain(*ptr16, 0);
                }
                if (new_output_coef[1] != cur_output_coef[1]) {
                    cur_output_coef[1] = af_codec_update_output_gain(*(ptr16 + 1), 1);
                }
                // Apply the gain
                *ptr16 = (int16_t)(*ptr16 * cur_output_coef[0]);
                *(ptr16 + 1) = (int16_t)(*(ptr16 + 1) * cur_output_coef[1]);
#else // Shift data right
                // Zero-crossing detection
                if (new_data_shift[0] != cur_data_shift[0]) {
                    if ((prev_data[0] >= 0 && ptr16[0] <= 0) || (prev_data[0] <= 0 && ptr16[0] >= 0)) {
                        cur_data_shift[0] = new_data_shift[0];
                    }
                }
                if (new_data_shift[1] != cur_data_shift[1]) {
                    if ((prev_data[1] >= 0 && ptr16[1] <= 0) || (prev_data[1] <= 0 && ptr16[1] >= 0)) {
                        cur_data_shift[1] = new_data_shift[1];
                    }
                }
                // Save previous data and shift data
                prev_data[0] = *ptr16;
                *ptr16 >>= cur_data_shift[0];
                prev_data[1] = *(ptr16 + 1);
                *(ptr16 + 1) >>= cur_data_shift[1];
#endif
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
                *ptr16 = __SSAT(*ptr16 + dc_l, 16);
                *(ptr16 + 1) = __SSAT(*(ptr16 + 1) + dc_r, 16);
#endif
                ptr16 += 2;
            }
        }
    } else {
        int32_t *ptr32 = (int32_t *)buf;
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
        int32_t dc_l = dac_dc[0] << 2;
        int32_t dc_r = dac_dc[1] << 2;
#endif
        if (chans == AUD_CHANNEL_NUM_1) {
            cnt = size / sizeof(int32_t);
            while (cnt-- > 0) {
#ifdef CORRECT_SAMPLE_VALUE
                *ptr32 = ((*ptr32) << 14) >> 14;
#endif
#ifdef AUDIO_SW_OUTPUT_GAIN
                // Update the gain
                if (new_output_coef[0] != cur_output_coef[0]) {
                    cur_output_coef[0] = af_codec_update_output_gain(*ptr32, 0);
                }
                // Apply the gain
                *ptr32 = (int32_t)(*ptr32 * cur_output_coef[0]);
#else // Shift data right
                // Zero-crossing detection
                if (new_data_shift[0] != cur_data_shift[0]) {
                    if ((prev_data[0] >= 0 && ptr32[0] <= 0) || (prev_data[0] <= 0 && ptr32[0] >= 0)) {
                        cur_data_shift[0] = new_data_shift[0];
                    }
                }
                // Save previous data and shift data
                prev_data[0] = *ptr32;
                *ptr32 >>= cur_data_shift[0];
#endif
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
                *ptr32 = __SSAT(*ptr32 + dc_l, 18);
#elif defined(CORRECT_SAMPLE_VALUE)
                *ptr32 = __SSAT(*ptr32, 18);
#endif
                ptr32++;
            }
        } else {
            cnt = size / sizeof(int32_t) / 2;
            while (cnt-- > 0) {
#ifdef CORRECT_SAMPLE_VALUE
                *ptr32 = ((*ptr32) << 14) >> 14;
                *(ptr32 + 1) = ((*(ptr32 + 1)) << 14) >> 14;
#endif
#ifdef AUDIO_SW_OUTPUT_GAIN
                // Update the gain
                if (new_output_coef[0] != cur_output_coef[0]) {
                    cur_output_coef[0] = af_codec_update_output_gain(*ptr32 >> 2, 0);
                }
                if (new_output_coef[1] != cur_output_coef[1]) {
                    cur_output_coef[1] = af_codec_update_output_gain(*(ptr32 + 1) >> 2, 1);
                }
                // Apply the gain
                *ptr32 = (int32_t)(*ptr32 * cur_output_coef[0]);
                *(ptr32 + 1) = (int32_t)(*(ptr32 + 1) * cur_output_coef[1]);
#else // Shift data right
                // Zero-crossing detection
                if (new_data_shift[0] != cur_data_shift[0]) {
                    if ((prev_data[0] >= 0 && ptr32[0] <= 0) || (prev_data[0] <= 0 && ptr32[0] >= 0)) {
                        cur_data_shift[0] = new_data_shift[0];
                    }
                }
                if (new_data_shift[1] != cur_data_shift[1]) {
                    if ((prev_data[1] >= 0 && ptr32[1] <= 0) || (prev_data[1] <= 0 && ptr32[1] >= 0)) {
                        cur_data_shift[1] = new_data_shift[1];
                    }
                }
                // Save previous data and shift data
                prev_data[0] = *ptr32;
                *ptr32 >>= cur_data_shift[0];
                prev_data[1] = *(ptr32 + 1);
                *(ptr32 + 1) >>= cur_data_shift[1];
#endif
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
                *ptr32 = __SSAT(*ptr32 + dc_l, 18);
                *(ptr32 + 1) = __SSAT(*(ptr32 + 1) + dc_r, 18);
#elif defined(CORRECT_SAMPLE_VALUE)
                *ptr32 = __SSAT(*ptr32, 18);
                *(ptr32 + 1) = __SSAT(*(ptr32 + 1), 18);
#endif
                ptr32 += 2;
            }
        }
    }
}

static inline uint32_t POSSIBLY_UNUSED af_codec_get_sample_count(uint32_t size, enum AUD_BITS_T bits, enum AUD_CHANNEL_NUM_T chans)
{
    uint32_t div;

    if (bits <= AUD_BITS_16) {
        div = 2;
    } else {
        div = 4;
    }
    if (chans != AUD_CHANNEL_NUM_1) {
        div *= 2;
    }
    return size / div;
}

AF_LOC static void af_codec_playback_post_handler(uint8_t *buf, struct af_stream_cfg_t *role)
{
#ifndef AUDIO_SW_OUTPUT_GAIN
    // Shift Data right
    new_data_shift[0] = hal_codec_get_alg_dac_shift();
    new_data_shift[1] = new_data_shift[0];
#endif

    af_codec_post_data_loop(buf, role->dma_buf_size / 2, role->cfg.bits, role->cfg.channel_num);

#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
    uint32_t time;

    if ((dac_pa_state == AF_DAC_PA_ON_TRIGGER) &&
            (((time = hal_sys_timer_get()) - dac_dc_start_time) >= AF_CODEC_DC_STABLE_INTERVAL) &&
            (time - dac_pa_stop_time >= AF_CODEC_PA_RESTART_INTERVAL)) {
            
        analog_aud_codec_speaker_enable(true);
#ifdef AUDIO_SW_OUTPUT_GAIN
        dac_pa_state = AF_DAC_PA_ON_SOFT_START;
        fade_curve_idx[0] = 0;
        fade_step_idx[0] = 0;
        fade_gain_step[0] = (fade_curve[1] - fade_curve[0]) / fade_step_cnt;
        fade_curve_idx[1] = fade_curve_idx[0];
        fade_step_idx[1] = fade_step_idx[0];
        fade_gain_step[1] = fade_gain_step[0];
        fade_sample_proc = 0;
    } else if (dac_pa_state == AF_DAC_PA_ON_SOFT_START) {
        fade_sample_proc += af_codec_get_sample_count(role->dma_buf_size / 2, role->cfg.bits, role->cfg.channel_num);
        if (fade_sample_proc >= fade_sample_cnt) {
            dac_pa_state = AF_DAC_PA_NULL;
            af_codec_output_gain_changed(saved_output_coef);
        }
#else
        dac_pa_state = AF_DAC_PA_NULL;
#endif
#ifdef RTOS
        if (dac_pa_state == AF_DAC_PA_NULL) {
            osSignalSet(fade_thread_id, (1 << AF_FADE_IN_SIGNAL_ID));
        }
#endif
    } else if (dac_pa_state == AF_DAC_PA_OFF_TRIGGER) {
#ifdef AUDIO_SW_OUTPUT_GAIN
        dac_pa_state = AF_DAC_PA_OFF_SOFT_END;
        fade_curve_idx[0] = ARRAY_SIZE(fade_curve) - 1;
        fade_step_idx[0] = fade_step_cnt;
        fade_gain_step[0] = (fade_curve[ARRAY_SIZE(fade_curve) - 1] - fade_curve[ARRAY_SIZE(fade_curve) - 2]) / fade_step_cnt;
        fade_curve_idx[1] = fade_curve_idx[0];
        fade_step_idx[1] = fade_step_idx[0];
        fade_gain_step[1] = fade_gain_step[0];
        fade_sample_proc = 0;
    } else if (dac_pa_state == AF_DAC_PA_OFF_SOFT_END) {
        fade_sample_proc += af_codec_get_sample_count(role->dma_buf_size / 2, role->cfg.bits, role->cfg.channel_num);
        if (fade_sample_proc >= fade_sample_cnt) {
            dac_pa_state = AF_DAC_PA_OFF_SOFT_END_DONE;
        }
    } else if (dac_pa_state == AF_DAC_PA_OFF_SOFT_END_DONE) {
#endif
        dac_dc_start_time = hal_sys_timer_get();
        dac_pa_state = AF_DAC_PA_OFF_DC_START;
#if 0
	TRACE("dac_dc[0] = %d [1] = %d",dac_dc[0],dac_dc[1]);
	TRACE("fade_gain_step[0] = %f [1] = %f",fade_gain_step[0],fade_gain_step[1]);
	TRACE("saved_output_coef = %f",saved_output_coef);
	TRACE("new_output_coef[0] = %f",new_output_coef[0] );
	TRACE("new_output_coef[0] = %f",new_output_coef[1] );
	TRACE("dac_dc_gain_attn = %f",dac_dc_gain_attn[0]);
	TRACE("dac_dc_gain_attn = %f",dac_dc_gain_attn[1]);
	TRACE("fade_gain_range[0] = %f [1] = %f", fade_gain_range[0],fade_gain_range[1] );
	TRACE("cur_output_coef[0]  =  %f [1] = %f",cur_output_coef[0], cur_output_coef[1] );
#endif
    } else if (dac_pa_state == AF_DAC_PA_OFF_DC_START) {
        time = hal_sys_timer_get();
        if (time - dac_dc_start_time >= AF_CODEC_DC_STABLE_INTERVAL) {
            dac_pa_state = AF_DAC_PA_NULL;
            analog_aud_codec_speaker_enable(false);
            dac_pa_stop_time = time;
#ifdef AUDIO_SW_OUTPUT_GAIN
            af_codec_output_gain_changed(saved_output_coef);
#endif
#ifdef RTOS
            osSignalSet(fade_thread_id, (1 << AF_FADE_OUT_SIGNAL_ID));
#endif
        }
    }
#endif
}
#endif

static void af_thread(void const *argument)
{
    osEvent evt;
    uint32_t signals = 0;
    struct af_stream_cfg_t *role = NULL;

    while(1)
    {
        //wait any signal
        evt = osSignalWait(0x0, osWaitForever);

        signals = evt.value.signals;
//        TRACE("[%s] status = %x, signals = %d", __func__, evt.status, evt.value.signals);

        if(evt.status == osEventSignal)
        {
            for(uint8_t i=0; i<AUD_STREAM_ID_NUM * AUD_STREAM_NUM; i++)
            {
                if(signals & (0x01 << i))
                {
                    role = af_get_stream_role((enum AUD_STREAM_ID_T)(i>>1), (enum AUD_STREAM_T)(i & 0x01));
//                    role = &af_stream[i>>1][i & 0x01];

                    af_lock_thread();
                    if (role->handler)
                    {
                        uint8_t *buf = role->dma_buf_ptr + PP_PINGPANG(role->ctl.pp_index) * (role->dma_buf_size / 2);
            			role->ctl.pp_cnt = 0;
                        role->handler(buf, role->dma_buf_size / 2);
#if defined( AUDIO_SW_OUTPUT_GAIN) || defined(AUDIO_OUTPUT_DIFF_DC_CALIB)

                        if ((enum AUD_STREAM_T)(i & 0x01) == AUD_STREAM_PLAYBACK && role->ctl.use_device == AUD_STREAM_USE_INT_CODEC) {
                            af_codec_playback_post_handler(buf, role);
                        }
#endif
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
#ifdef AF_STREAM_ID_0_PLAYBACK_FADEOUT
                    af_stream_stop_process(role, role->dma_buf_ptr + PP_PINGPANG(role->ctl.pp_index) * (role->dma_buf_size / 2), role->dma_buf_size / 2);
#endif
                }
            }
        }
        else
        {
            TRACE("[%s] ERROR: evt.status = %d", __func__, evt.status);
            continue;
        }
    }
}

static void af_dma_irq_handler(uint8_t ch, uint32_t remain_dst_tsize, uint32_t error, struct HAL_DMA_DESC_T *lli)
{
    struct af_stream_cfg_t *role = NULL;

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
//                TRACE("[%s] id = %d, stream = %d, ch = %d", __func__, id, stream, ch);
//                TRACE("[%s] PLAYBACK pp_cnt = %d", af_stream[AUD_STREAM_PLAYBACK].ctl.pp_cnt);
                osSignalSet(af_thread_tid, 0x01 << (id * 2 + stream));
                return;
            }
        }
    }

    //invalid dma irq
    ASSERT(0, "[%s] ERROR: channel id = %d", __func__, ch);
}

uint32_t af_open(void)
{
    AF_TRACE_DEBUG();
    struct af_stream_cfg_t *role = NULL;

    if (audioflinger_mutex_id == NULL)
    {
        audioflinger_mutex_id = osMutexCreate((osMutex(audioflinger_mutex)));
    }

    af_lock_thread();

#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
    if (!dac_dc_valid) {
        uint16_t max_dc;

        dac_dc_valid = true;
        analog_aud_get_dc_calib_value(&dac_dc[0], &dac_dc[1]);
        if (ABS(dac_dc[0]) >= ABS(dac_dc[1])) {
            max_dc = ABS(dac_dc[0]);
        } else {
            max_dc = ABS(dac_dc[1]);
        }
        ASSERT(max_dc + 1 < AF_CODEC_DC_MAX_SCALE, "Bad dc values: (%d, %d)", dac_dc[0], dac_dc[1]);
        if (max_dc) {
            dac_dc_gain_attn = 1 - (float)(max_dc + 1) / AF_CODEC_DC_MAX_SCALE;
        } else {
            dac_dc_gain_attn = 1;
        }
    }
#endif

#ifdef AUDIO_SW_OUTPUT_GAIN
    hal_codec_set_sw_output_coef_callback(af_codec_output_gain_changed);
#endif

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

#ifndef _AUDIO_NO_THREAD_
    af_thread_tid = osThreadCreate(osThread(af_thread), NULL);
    osSignalSet(af_thread_tid, 0x0);

#endif

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

    if (device == AUD_STREAM_USE_INT_CODEC && stream == AUD_STREAM_PLAYBACK) {
#ifdef AUDIO_SW_OUTPUT_GAIN
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
        // Fade in
        cur_output_coef[0] = 0;
        cur_output_coef[1] = 0;
#else
        cur_output_coef[0] = new_output_coef[0];
        cur_output_coef[1] = new_output_coef[1];
#endif
#else // Shift data right
        // Avoid pop sound when enabling zero-crossing gain adjustment
        if (role->dma_buf_ptr && role->dma_buf_size >= 16) {
            int32_t *buf = (int32_t *)role->dma_buf_ptr;
            buf[0] = buf[1] = -1;
            buf[2] = buf[3] = 0;
        }
        // Init data shift values
        new_data_shift[0] = hal_codec_get_alg_dac_shift();
        new_data_shift[1] = new_data_shift[0];
        cur_data_shift[0] = new_data_shift[0];
        cur_data_shift[1] = new_data_shift[1];
        prev_data[0] = 0;
        prev_data[1] = 0;
#endif

#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
#ifdef AUDIO_SW_OUTPUT_GAIN
        fade_sample_cnt = (role->cfg.sample_rate + (1000 - 1)) / 1000 * AF_CODEC_FADE_PERIOD_MS;
        if (fade_sample_cnt < AF_CODEC_FADE_MIN_SAMPLE_CNT) {
            fade_sample_cnt = AF_CODEC_FADE_MIN_SAMPLE_CNT;
        }
        fade_step_cnt = fade_sample_cnt / (ARRAY_SIZE(fade_curve) - 1);
        if (fade_step_cnt == 0) {
            fade_step_cnt = 1;
        }

        fade_gain_range[0] = new_output_coef[0];
        fade_gain_range[1] = new_output_coef[1];
#ifdef RTOS
        fade_thread_id = osThreadGetId();
        osSignalClear(fade_thread_id, (1 << AF_FADE_IN_SIGNAL_ID));
#endif
#endif
        dac_pa_state = AF_DAC_PA_ON_TRIGGER;
        af_codec_post_data_loop(role->dma_buf_ptr, role->dma_buf_size, role->cfg.bits, role->cfg.channel_num);
#endif
    }

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

    if (device == AUD_STREAM_USE_INT_CODEC && stream == AUD_STREAM_PLAYBACK) {
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
        dac_dc_start_time = hal_sys_timer_get();
        while (dac_pa_state == AF_DAC_PA_ON_TRIGGER) {
#ifdef RTOS
            osSignalWait((1 << AF_FADE_IN_SIGNAL_ID), 300);
            osSignalClear(fade_thread_id, (1 << AF_FADE_IN_SIGNAL_ID));
#else
            af_thread(NULL);
#endif
        }
#endif
    }

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

#ifdef AF_STREAM_ID_0_PLAYBACK_FADEOUT
    if (id == AUD_STREAM_ID_0 && stream == AUD_STREAM_PLAYBACK){
        af_stream_fadeout_start(512);
        af_stream_stop_wait_finish();
	}
#endif

    af_lock_thread();

    if (device == AUD_STREAM_USE_INT_CODEC && stream == AUD_STREAM_PLAYBACK) {
#ifdef AUDIO_OUTPUT_DIFF_DC_CALIB
#ifdef AUDIO_SW_OUTPUT_GAIN
        // Fade out
        new_output_coef[0] = 0;
        new_output_coef[1] = 0;
        fade_gain_range[0] = cur_output_coef[0];
        fade_gain_range[1] = cur_output_coef[1];
#ifdef RTOS
        fade_thread_id = osThreadGetId();
        osSignalClear(fade_thread_id, (1 << AF_FADE_OUT_SIGNAL_ID));
#endif
#endif
        dac_pa_state = AF_DAC_PA_OFF_TRIGGER;
        af_unlock_thread();
        while (dac_pa_state != AF_DAC_PA_NULL) {
#ifdef RTOS
            osSignalWait((1 << AF_FADE_OUT_SIGNAL_ID), 300);
            osSignalClear(fade_thread_id, (1 << AF_FADE_OUT_SIGNAL_ID));
#else
            af_thread(NULL);
#endif
        }
        af_lock_thread();
#endif
    }

    hal_audma_stop(role->dma_cfg.ch);

#if defined(RTOS) && defined(AF_STREAM_ID_0_PLAYBACK_FADEOUT)
    if (id == AUD_STREAM_ID_0 && stream == AUD_STREAM_PLAYBACK){
        af_stream_fadeout_stop();
    }
#endif

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

void af_set_priority(int priority)
{
    osThreadSetPriority(af_thread_tid, priority);
}

int af_get_priority(void)
{
    return (int)osThreadGetPriority(af_thread_tid);
}

