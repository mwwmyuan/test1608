#ifdef __MBED__
#include "mbed.h"
#endif
// Standard C Included Files
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "rtos.h"
#include "tgt_hardware.h"
#ifdef __MBED__
#include "SDFileSystem.h"
#endif
#include "hal_uart.h"
#include "hal_timer.h"
#include "hal_trace.h"
#include "hal_codec.h"
#include "audioflinger.h"
#include "cqueue.h"
#include "app_audio.h"
#include "analog.h"

// BT
extern "C" {
#include "eventmgr.h"
#include "me.h"
#include "sec.h"
#include "a2dp.h"
#include "avdtp.h"
#include "avctp.h"
#include "avrcp.h"
#include "hf.h"
}

#ifdef __AUDIO_RESAMPLE__
#include "hal_chipid.h"
#include "audio_resample.h"
#endif

#if defined(A2DP_AAC_ON)
#include "btapp.h"
#endif

enum A2DPPLAY_STRTEAM_T {
    A2DPPLAY_STRTEAM_PUT= 0,
    A2DPPLAY_STRTEAM_GET,
    A2DPPLAY_STRTEAM_QTY,
};

/* sbc decoder */
static bool need_init_decoder = true;
static SbcDecoder* sbc_decoder = NULL;


#define A2DP_AUDIO_SYNC_WITH_LOCAL (1)

#define A2DP_AUDIO_SYNC_TRACE(s,...)
//TRACE(s, ##__VA_ARGS__)


#ifdef A2DP_AUDIO_SYNC_WITH_LOCAL
#ifdef __AUDIO_RESAMPLE__
#define A2DP_AUDIO_SYNC_WITH_LOCAL_STEP (1)
#else
#define A2DP_AUDIO_SYNC_WITH_LOCAL_SAMPLERATE_DEFAULT (0)
#define A2DP_AUDIO_SYNC_WITH_LOCAL_SAMPLERATE_INC     (2)
#define A2DP_AUDIO_SYNC_WITH_LOCAL_SAMPLERATE_DEC     (-2)
#define A2DP_AUDIO_SYNC_WITH_LOCAL_SAMPLERATE_STEP    (0.00005f)
#endif
#define A2DPPLAY_SYNC_STATUS_SET (0x01)
#define A2DPPLAY_SYNC_STATUS_RESET (0x02)
#define A2DPPLAY_SYNC_STATUS_PROC (0x04)

enum A2DP_AUDIO_SYNC_STATUS {
    A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_DEC,
    A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_INC,
    A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_DEFAULT,
};
#endif

#define A2DPPLAY_CACHE_OK_THRESHOLD (sbc_frame_size<<6)

#if (FPGA==1) && !defined(__FPGA_FLASH__) 
#define CFG_HW_AUD_EQ_NUM_BANDS (8)
const int8_t cfg_aud_eq_sbc_band_settings[CFG_HW_AUD_EQ_NUM_BANDS] = {0, 0, 0, 0, 0, 0, 0, 0};
#endif
static float sbc_eq_band_gain[CFG_HW_AUD_EQ_NUM_BANDS];

/* sbc queue */
#define SBC_TEMP_BUFFER_SIZE 128
#define SBC_QUEUE_SIZE_DEFAULT (SBC_TEMP_BUFFER_SIZE*64)
#define SBC_QUEUE_SIZE (1024*11)
CQueue sbc_queue;

static uint16_t g_sbc_queue_size = SBC_QUEUE_SIZE_DEFAULT;
static int16_t sbc_frame_size = SBC_TEMP_BUFFER_SIZE;
static uint16_t sbc_frame_rev_len = 0;
static uint16_t sbc_frame_num = 0;

static enum APP_AUDIO_CACHE_T a2dp_cache_status = APP_AUDIO_CACHE_QTY;

#define A2DP_SYNC_WITH_GET_MUTUX_TIMEROUT_CNT (5)
#define A2DP_SYNC_WITH_GET_MUTUX_TIMEROUT_MS (20)
static osThreadId a2dp_put_thread_tid = NULL;
static bool a2dp_get_need_sync = false;

#ifdef __AUDIO_RESAMPLE__
extern enum AUD_SAMPRATE_T a2dp_sample_rate;
static bool allow_resample = false;
#endif

extern int a2dp_timestamp_parser_needsync(void);

#define A2DP_SYNC_WITH_GET_MUTUX_ALLOC() do{ \
                                                if (a2dp_put_thread_tid == NULL){ \
                                                    a2dp_put_thread_tid = osThreadGetId(); \
                                                } \
                                           }while(0)

#define A2DP_SYNC_WITH_GET_MUTUX_FREE() do{ \
                                                a2dp_put_thread_tid = NULL; \
                                          }while(0)

#define A2DP_SYNC_WITH_GET_MUTUX_WAIT() do{ \
                                                a2dp_get_need_sync = true; \
                                                if (a2dp_put_thread_tid){ \
                                                    osSignalClear(a2dp_put_thread_tid, 0x80); \
                                                    osSignalWait(0x80, A2DP_SYNC_WITH_GET_MUTUX_TIMEROUT_MS); \
                                                }\
                                          }while(0)

#define A2DP_SYNC_WITH_GET_MUTUX_SET() do{ \
                                            if (a2dp_get_need_sync){ \
                                                a2dp_get_need_sync = false; \
                                                if (a2dp_put_thread_tid){ \
                                                    osSignalSet(a2dp_put_thread_tid, 0x80); \
                                                } \
                                            } \
                                      }while(0)

//#define A2DP_SYNC_WITH_PUT_MUTUX (1)
#define A2DP_SYNC_WITH_PUT_MUTUX_TIMEROUT_CNT (1)
#define A2DP_SYNC_WITH_PUT_MUTUX_TIMEROUT_MS (3)
static osThreadId a2dp_get_thread_tid = NULL;
static bool a2dp_get_put_sync = false;

#define A2DP_SYNC_WITH_PUT_MUTUX_ALLOC() do{ \
                                                if (a2dp_get_thread_tid == NULL){ \
                                                    a2dp_get_thread_tid = osThreadGetId(); \
                                                } \
                                           }while(0)

#define A2DP_SYNC_WITH_PUT_MUTUX_FREE() do{ \
                                                a2dp_get_thread_tid = NULL; \
                                          }while(0)

#define A2DP_SYNC_WITH_PUT_MUTUX_WAIT() do{ \
                                                a2dp_get_put_sync = true; \
                                                if (a2dp_get_thread_tid){\
                                                    osSignalClear(a2dp_get_thread_tid, 0x80); \
                                                    osSignalWait(0x5, A2DP_SYNC_WITH_PUT_MUTUX_TIMEROUT_MS); \
                                                }\
                                          }while(0)

#define A2DP_SYNC_WITH_PUT_MUTUX_SET() do{ \
                                                if (a2dp_get_put_sync){ \
                                                    a2dp_get_put_sync = false; \
                                                    if (a2dp_get_thread_tid){\
                                                        osSignalSet(a2dp_get_thread_tid, 0x80); \
                                                    }\
                                                } \
                                          }while(0)


int a2dp_audio_sbc_set_frame_info(int rcv_len, int frame_num)
{
    if ((!rcv_len)||(!frame_num)){
        return 0;
    }

    if (sbc_frame_rev_len != rcv_len || sbc_frame_num != frame_num){
        sbc_frame_rev_len = rcv_len;
        sbc_frame_num = frame_num;
        sbc_frame_size = rcv_len/frame_num;
    }

    return 0;
}

#ifdef A2DP_AUDIO_SYNC_WITH_LOCAL
#ifdef __AUDIO_RESAMPLE__
#define AUDIO_SYNC_BY_RESAMPLE
#define  RESAMPLE_ID_44_NUM  3 
#define  RESAMPLE_ID_48_NUM  4 
enum resample_id_t  s44100_resample_table[RESAMPLE_ID_44_NUM] = {
    RESAMPLE_ID_44P1TO50P7,
    RESAMPLE_ID_44P1TO51P88,
    RESAMPLE_ID_44P1TO52P8
};

enum resample_id_t  s48000_resample_table[RESAMPLE_ID_48_NUM] = {
    RESAMPLE_ID_48TO50P7,    
    RESAMPLE_ID_48TO50P82, 
    RESAMPLE_ID_48TO51P7, 
    RESAMPLE_ID_48TO52P82 
};

static enum resample_id_t get_resample_id(enum AUD_SAMPRATE_T sample_rate,int shift)
{
    enum resample_id_t id;
    int freq_shift = shift;
    if( sample_rate == AUD_SAMPRATE_48000){
        if (freq_shift > RESAMPLE_ID_48_NUM -1) freq_shift = RESAMPLE_ID_48_NUM -1;
        id = s48000_resample_table[freq_shift];
    }
    if( sample_rate == AUD_SAMPRATE_44100){
        if (freq_shift > RESAMPLE_ID_44_NUM -1) freq_shift = RESAMPLE_ID_44_NUM -1;
        id = s44100_resample_table[freq_shift];
    }

    return id; 
} 
static int a2dp_audio_sync_proc(uint8_t status, int shift)
{
    struct AF_STREAM_CONFIG_T *cfg;
    bool need_shift = false;
    static int cur_shift = 0;
    static int dest_shift = 0;

    LOCK_APP_AUDIO_QUEUE();
    if (status & A2DPPLAY_SYNC_STATUS_RESET){
        cur_shift = 0;
        dest_shift = 0;
    }
    if (status & A2DPPLAY_SYNC_STATUS_SET){
        dest_shift = shift;
    }
    if (cur_shift > dest_shift){
        cur_shift--;
        need_shift = true;
    }
    if (cur_shift<dest_shift){
        cur_shift++;
        need_shift = true;
    }
    if (need_shift){
        A2DP_AUDIO_SYNC_TRACE("a2dp_audio_sync_proc shift:%d\n", cur_shift);
        af_stream_get_cfg(AUD_STREAM_ID_0, AUD_STREAM_PLAYBACK, &cfg, false);
        switch (cfg->sample_rate) {
            case AUD_SAMPRATE_44100:
                audio_resample_reopen(get_resample_id(AUD_SAMPRATE_44100, cur_shift));
                break;
            case AUD_SAMPRATE_48000:
                audio_resample_reopen(get_resample_id(AUD_SAMPRATE_48000, cur_shift));
                break;
            default:
                break;
        }
    }
    UNLOCK_APP_AUDIO_QUEUE();
    return 0;
}
#else
static enum A2DP_AUDIO_SYNC_STATUS sync_status = A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_DEFAULT;

static int a2dp_audio_sync_proc(uint8_t status, int shift)
{
    struct AF_STREAM_CONFIG_T *cfg;
    struct HAL_CODEC_CONFIG_T codec_cfg;
    
    bool need_shift = false;
    static int cur_shift = 0;
    static int dest_shift = 0;

    LOCK_APP_AUDIO_QUEUE();
    if (status & A2DPPLAY_SYNC_STATUS_RESET){
        sync_status = A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_DEFAULT;
        cur_shift = 0;
        dest_shift = 0;
    }
    if (status & A2DPPLAY_SYNC_STATUS_SET){
        dest_shift = shift;
    }
    if (cur_shift > dest_shift){
        cur_shift--;
        need_shift = true;
    }
    if (cur_shift<dest_shift){
        cur_shift++;
        need_shift = true;
    }
    if (need_shift){
        A2DP_AUDIO_SYNC_TRACE("a2dp_audio_sync_proc shift:%d\n", cur_shift);
        af_stream_get_cfg(AUD_STREAM_ID_0, AUD_STREAM_PLAYBACK, &cfg, false);        
        codec_cfg.sample_rate = cfg->sample_rate;
        hal_codec_setup_stream_smaple_rate_trim(HAL_CODEC_ID_0, AUD_STREAM_PLAYBACK, &codec_cfg, A2DP_AUDIO_SYNC_WITH_LOCAL_SAMPLERATE_STEP*cur_shift);
    }
    UNLOCK_APP_AUDIO_QUEUE();
    return 0;
}

#endif
#endif

bool a2dp_audio_isrunning(enum A2DPPLAY_STRTEAM_T stream, bool run)
{
    static bool stream_running[A2DPPLAY_STRTEAM_QTY] = {false, false};

    if (stream >= A2DPPLAY_STRTEAM_QTY)
        return false;

    stream_running[stream] = run;

    if (stream_running[A2DPPLAY_STRTEAM_PUT] && stream_running[A2DPPLAY_STRTEAM_GET])
        return true;
    else
        return false;
}

static short *raw_pcm_data;
#if defined(A2DP_AAC_ON)
static int g_aac_pcm_index = 0;
static int g_aac_pcm_offset[2] ={0};
static int g_aac_init_raw_pcm =   1;
int decode_aac_frame(unsigned char *pcm_buffer, unsigned int pcm_len);
#endif
int store_sbc_buffer(unsigned char *buf, unsigned int len)
{
    int size;
    int cnt = 0;
    int nRet = 0;

    if (!a2dp_audio_isrunning(A2DPPLAY_STRTEAM_PUT, true)){
        TRACE("%s not ready:%d cache_status:%d",__func__, len, a2dp_cache_status);
    }
    
     //TRACE("sb %d %x %x %x %x %x %x %x %x %x", len, buf[0],
     //buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8]);
    switch (a2dp_cache_status) {
        case APP_AUDIO_CACHE_CACHEING:
            LOCK_APP_AUDIO_QUEUE();
            nRet = APP_AUDIO_EnCQueue(&sbc_queue, buf, len);
            size = APP_AUDIO_LengthOfCQueue(&sbc_queue);
            UNLOCK_APP_AUDIO_QUEUE();
#ifdef  A2DP_AAC_ON
            if(bt_sbc_player_get_codec_type() == AVDTP_CODEC_TYPE_MPEG2_4_AAC){
                if (size > 2048){
                    TRACE("aac cache ok:%d\n", size);
                    a2dp_cache_status = APP_AUDIO_CACHE_OK;
                }
                if(nRet){
                    TRACE("cache add overflow\n");
                    a2dp_cache_status = APP_AUDIO_CACHE_OK;
                }
                if (a2dp_cache_status == APP_AUDIO_CACHE_OK){
#ifdef __LOCK_AUDIO_THREAD__
                    af_unlock_thread();
#endif
                    A2DP_SYNC_WITH_GET_MUTUX_ALLOC();
                    A2DP_SYNC_WITH_GET_MUTUX_WAIT();
#ifdef __LOCK_AUDIO_THREAD__
                    af_lock_thread();
#endif
                }                
            }else
#endif
            {
                if (size >= (g_sbc_queue_size-(sbc_frame_size*20))){
                    TRACE("cache ok:%d\n", size);
                    a2dp_cache_status = APP_AUDIO_CACHE_OK;
                }else if (sbc_frame_size && (size>=A2DPPLAY_CACHE_OK_THRESHOLD)){
                    TRACE("cache ok:%d\n", size);
                    a2dp_cache_status = APP_AUDIO_CACHE_OK;
                }else{
                    TRACE("cache add:%d\n", len);
                }
                if (CQ_ERR == nRet){
                    TRACE("cache add overflow\n");
                    a2dp_cache_status = APP_AUDIO_CACHE_OK;
                }
                if (a2dp_cache_status == APP_AUDIO_CACHE_OK){
#ifdef __LOCK_AUDIO_THREAD__
                    af_unlock_thread();
#endif
                    A2DP_SYNC_WITH_GET_MUTUX_ALLOC();
                    A2DP_SYNC_WITH_GET_MUTUX_WAIT();
#ifdef __LOCK_AUDIO_THREAD__
                    af_lock_thread();
#endif
                }
            }
            break;
        case APP_AUDIO_CACHE_OK:
            do{
                LOCK_APP_AUDIO_QUEUE();
                nRet = APP_AUDIO_EnCQueue(&sbc_queue, buf, len);
//                size = APP_AUDIO_LengthOfCQueue(&sbc_queue);
                UNLOCK_APP_AUDIO_QUEUE();
//                TRACE("cache add:%d %d/%d \n", len, size, g_sbc_queue_size);
                if (CQ_OK == nRet){
                    nRet = 0;
                    break;
                }else{
    //                TRACE("cache flow control:%d\n", cnt);
#ifdef A2DP_AUDIO_SYNC_WITH_LOCAL
                    a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_SET, 0);
#endif
                    nRet = -1;
#ifdef __LOCK_AUDIO_THREAD__
                    af_unlock_thread();
#endif
                    A2DP_SYNC_WITH_GET_MUTUX_ALLOC();
                    A2DP_SYNC_WITH_GET_MUTUX_WAIT();
#ifdef __LOCK_AUDIO_THREAD__
                    af_lock_thread();
#endif
                }
            }while(cnt++<A2DP_SYNC_WITH_GET_MUTUX_TIMEROUT_CNT);
            break;
        case APP_AUDIO_CACHE_QTY:
        default:
            break;
    }
#ifdef A2DP_SYNC_WITH_PUT_MUTUX
    A2DP_SYNC_WITH_PUT_MUTUX_SET();
#endif

    if (nRet){
        TRACE("cache overflow\n");
    }
    return 0;
}
#if defined(A2DP_AAC_ON)
#include "aacdec.h"

#define AAC_READBUF_SIZE	(AAC_MAINBUF_SIZE)	/* pick something big enough to hold a bunch of frames */
unsigned char aac_input_mid_buf[AAC_READBUF_SIZE];
#define CHECK_AAC_HEAD_LENGTH (AAC_READBUF_SIZE)
HAACDecoder *hAACDecoder = NULL;
AACFrameInfo aacFrameInfo;

int get_aac_frame(unsigned char * frame, unsigned int len)
{
    int status;
    unsigned int len1 = 0, len2 = 0;
    unsigned char *e1 = NULL, *e2 = NULL;
    LOCK_APP_AUDIO_QUEUE();
    if(LengthOfCQueue(&sbc_queue) < len)
        len = LengthOfCQueue(&sbc_queue);
    status = PeekCQueue(&sbc_queue, len, &e1, &len1, &e2, &len2);
    UNLOCK_APP_AUDIO_QUEUE();
    if(status != CQ_OK){
        return 0;
    }
    
    if (e1) {
        memcpy(frame, e1, len1);
    }
    if (e2) {
        memcpy(frame + len1, e2, len2);
    }    
    return len;
}

int decode_aac_frame(unsigned char *pcm_buffer, unsigned int pcm_len)
{
    int r = 0;
    unsigned char retry = 0;
    unsigned char *e1 = NULL, *e2 = NULL, *input_buf = NULL;
    int bytesLeft, nRead, err, outOfData, eofReached ,check_length = 0;
    uint32_t lock;
    static unsigned char aac_header[8];
#if 1
    if (hAACDecoder == NULL) {
        hAACDecoder = (HAACDecoder *)AACInitDecoder();
        AACSetLOASFormat(hAACDecoder);
    }  

    r = get_aac_frame(aac_input_mid_buf, AAC_READBUF_SIZE);
    if(r == 0) {
        return 0;
    }
   
    bytesLeft = r;
    input_buf = (unsigned char *)&aac_input_mid_buf;
    lock = int_lock();
    err = AACDecode(hAACDecoder, &input_buf, &bytesLeft, (short *)pcm_buffer);
    int_unlock(lock);
    if (err) {
        switch (err) {
            case ERR_AAC_INDATA_UNDERFLOW:
                if (eofReached || bytesLeft == AAC_READBUF_SIZE)
                    outOfData = 1;
                break;
            default:
                outOfData = 1;
                break;
        }
    }else{
        memcpy(aac_header, aac_input_mid_buf, sizeof(aac_header));
    }
    
    AACGetLastFrameInfo(hAACDecoder, &aacFrameInfo);
    //TRACE("aacdec err:%d bytesLeft %d, outputsamples %d, pcm_len %d\n", err,bytesLeft, aacFrameInfo.outputSamps, pcm_len);

    if (bytesLeft != r) {
        LOCK_APP_AUDIO_QUEUE();
        if(outOfData && (bytesLeft == r)){
            DeCQueue(&sbc_queue, 0, 8);
        }
        else 
            DeCQueue(&sbc_queue, 0, r - bytesLeft);
        UNLOCK_APP_AUDIO_QUEUE();
    }

    //check aac head, compare the first 8 bytes
    if((check_length = LengthOfCQueue(&sbc_queue)) >= CHECK_AAC_HEAD_LENGTH)
        check_length = CHECK_AAC_HEAD_LENGTH;

    if(check_length > sizeof(aac_header)){       
        if(get_aac_frame(aac_input_mid_buf, check_length) > 0){
            int u_offset =0;
            do{
                if(!memcmp(aac_input_mid_buf + u_offset, aac_header, sizeof(aac_header))){
                    if(u_offset > 0){
                        LOCK_APP_AUDIO_QUEUE();
                        DeCQueue(&sbc_queue, 0, u_offset);
                        UNLOCK_APP_AUDIO_QUEUE();
                    }
                    break;
                }
            }while(u_offset++ < (check_length - sizeof(aac_header)));
        }
    }
        
#endif    
    return pcm_len;
}
#endif

#if 0
int decode_sbc_frame(unsigned char *pcm_buffer, unsigned int pcm_len)
{
    static uint32_t g_preIrqTime = 0;
    uint8_t underflow = 0;
    int32_t stime,etime;

    unsigned char retry = 0;
    int r = 0;
    unsigned char *e1 = NULL, *e2 = NULL;
    unsigned int len1 = 0, len2 = 0;

    static SbcPcmData pcm_data;
    XaStatus ret = XA_STATUS_SUCCESS;
    unsigned short byte_decode = 0;


    stime = hal_sys_timer_get();
    pcm_data.data = (unsigned char*)pcm_buffer;

    LOCK_APP_AUDIO_QUEUE();
again:
    if(need_init_decoder) {
        pcm_data.data = (unsigned char*)pcm_buffer;
        pcm_data.dataLen = 0;
        SBC_InitDecoder(&sbc_decoder);
    }

get_again:
    len1 = len2 = 0;
    r = APP_AUDIO_PeekCQueue(&sbc_queue, SBC_TEMP_BUFFER_SIZE, &e1, &len1, &e2, &len2);

    if(r == CQ_ERR || len1 == 0) {
        int size;
        UNLOCK_APP_AUDIO_QUEUE();
        osDelay(2);
        LOCK_APP_AUDIO_QUEUE();
        if (retry++<8){
            goto get_again;
        }else{
            need_init_decoder = true;
            size = APP_AUDIO_LengthOfCQueue(&sbc_queue);
            underflow = 1;
//            a2dp_cache_status = APP_AUDIO_CACHE_CACHEING;
            TRACE("cache underflow size:%d retry:%d\n", size, retry);
            goto exit;
        }
    }
    {
        uint32_t lock = int_lock();
        ret = SBC_DecodeFrames(&sbc_decoder, (unsigned char *)e1, len1, &byte_decode,
        &pcm_data, pcm_len, sbc_eq_band_gain);
        int_unlock(lock);
    }
    if(ret == XA_STATUS_CONTINUE) {
        need_init_decoder = false;
        APP_AUDIO_DeCQueue(&sbc_queue, 0, len1);
        goto again;

        /* back again */
    }
    else if(ret == XA_STATUS_SUCCESS) {
        need_init_decoder = false;
        r = pcm_data.dataLen;
        pcm_data.dataLen = 0;

        APP_AUDIO_DeCQueue(&sbc_queue, 0, byte_decode);

        //TRACE("p %d\n", pcm_data.sampleFreq);

        /* leave */
    }
    else if(ret == XA_STATUS_FAILED) {
        need_init_decoder = true;
        r = pcm_data.dataLen;
        TRACE("err\n");

        APP_AUDIO_DeCQueue(&sbc_queue, 0, byte_decode);

        /* leave */
    }
    else if(ret == XA_STATUS_NO_RESOURCES) {
        need_init_decoder = false;

        TRACE("no\n");

        /* leav */
        r = 0;
    }

exit:
    etime = hal_sys_timer_get();
    if (underflow || need_init_decoder){
        int32_t a,b;
        a = TICKS_TO_MS(stime - g_preIrqTime);
        b = TICKS_TO_MS(etime - stime);
        TRACE( "sbc_decoder irqDur:%d Decode:%d diff:%d size:%d retry:%d\n ", a, b, (a-b), APP_AUDIO_LengthOfCQueue(&sbc_queue),retry);
    }
    g_preIrqTime = stime;
    UNLOCK_APP_AUDIO_QUEUE();
    return r;
}

#else
int decode_sbc_frame(unsigned char *pcm_buffer, unsigned int pcm_len)
{
    static uint32_t g_preIrqTime = 0;
    uint8_t underflow = 0;
    uint32_t retry_data_index = 0;
    int32_t stime,etime;

    static unsigned int sbc_next_frame_size;
    unsigned char retry = 0;
    int r = 0;
    unsigned char *e1 = NULL, *e2 = NULL;
    unsigned int len1 = 0, len2 = 0;

    unsigned int pcm_offset = 0;
    unsigned int pcm_processed = 0;

    static SbcPcmData pcm_data;
    XaStatus ret = XA_STATUS_SUCCESS;
    unsigned short byte_decode = 0;
#ifdef A2DP_AUDIO_SYNC_WITH_LOCAL
    int q_usedlen = 0;
#endif

    stime = hal_sys_timer_get();

    pcm_data.data = (unsigned char*)pcm_buffer;

    if (!sbc_next_frame_size){
        sbc_next_frame_size = sbc_frame_size;
    }

reinit:
    if(need_init_decoder) {
		sbc_next_frame_size = sbc_frame_size;
		pcm_offset = 0;		
        pcm_data.data = (unsigned char*)(pcm_buffer+pcm_offset);
        pcm_data.dataLen = 0;
#ifdef __SBC_FUNC_IN_ROM__
        SBC_ROM_FUNC.SBC_InitDecoder(sbc_decoder);
#else
        SBC_InitDecoder(sbc_decoder);
#endif
    }

get_again:
    LOCK_APP_AUDIO_QUEUE();
    len1 = len2 = 0;
    r = PeekCQueue(&sbc_queue, sbc_next_frame_size, &e1, &len1, &e2, &len2);
    UNLOCK_APP_AUDIO_QUEUE();   
#ifdef  __LOCK_AUDIO_THREAD__
    if(r == CQ_ERR) {
        pcm_processed = pcm_data.dataLen;
        TRACE("cache underflow retry:%d\n", retry);
        goto exit;
    }
#elif defined (A2DP_SYNC_WITH_PUT_MUTUX)
    if(r == CQ_ERR) {
        int size;
        A2DP_SYNC_WITH_PUT_MUTUX_ALLOC();
        A2DP_SYNC_WITH_PUT_MUTUX_WAIT();
        if (retry++<A2DP_SYNC_WITH_PUT_MUTUX_TIMEROUT_CNT){
            goto get_again;
        }else{
            LOCK_APP_AUDIO_QUEUE();
            size = LengthOfCQueue(&sbc_queue);
            UNLOCK_APP_AUDIO_QUEUE();
            pcm_processed = pcm_data.dataLen;
            underflow = 1;
//            a2dp_cache_status = APP_AUDIO_CACHE_CACHEING;
            TRACE("cache underflow size:%d retry:%d\n", size, retry);
            goto exit;
        }
    }else{
        retry = 0;
    }
#else
#if 1
    if(r == CQ_ERR) {
        pcm_processed = pcm_data.dataLen;
        TRACE("cache underflow retry:%d\n", retry);
        goto exit;
    }
#else
    if(r == CQ_ERR) {
        int size;
        osDelay(2);
        LOCK_APP_AUDIO_QUEUE();
        size = LengthOfCQueue(&sbc_queue);
        UNLOCK_APP_AUDIO_QUEUE();
        if (retry++<12){
            goto get_again;
        }else{
            pcm_processed = pcm_data.dataLen;
            underflow = 1;
//            a2dp_cache_status = APP_AUDIO_CACHE_CACHEING;
            TRACE("cache underflow size:%d retry:%d\n", size, retry);
            goto exit;
        }
    }
#endif
#endif
    if (!len1 && retry_data_index++ < 50){
        TRACE("len1 underflow %d/%d\n", len1, len2);
        osDelay(1);
        goto get_again;
    }

    if(retry_data_index >= 50)
        return 0;
        
    {
        uint32_t lock = int_lock();
#ifdef __SBC_FUNC_IN_ROM__                
        ret =  SBC_ROM_FUNC.SBC_DecodeFrames(sbc_decoder, (unsigned char *)e1, len1, &byte_decode,
        &pcm_data, pcm_len-pcm_offset, sbc_eq_band_gain);  
#else
        ret = SBC_DecodeFrames(sbc_decoder, (unsigned char *)e1, len1, &byte_decode,
        &pcm_data, pcm_len-pcm_offset, sbc_eq_band_gain);
#endif
        int_unlock(lock);
    }
    if(ret == XA_STATUS_CONTINUE) {
        need_init_decoder = false;
        LOCK_APP_AUDIO_QUEUE();
        DeCQueue(&sbc_queue, 0, byte_decode);
        UNLOCK_APP_AUDIO_QUEUE();
#ifdef A2DP_AUDIO_SYNC_WITH_LOCAL
        q_usedlen += byte_decode;
#endif
        sbc_next_frame_size = (sbc_frame_size-byte_decode)>0?(sbc_frame_size-byte_decode):sbc_frame_size;

        goto get_again;

        /* back again */
    }
    else if(ret == XA_STATUS_SUCCESS) {
        need_init_decoder = false;
        pcm_processed = pcm_data.dataLen;
        pcm_data.dataLen = 0;

        LOCK_APP_AUDIO_QUEUE();
        DeCQueue(&sbc_queue, 0, byte_decode);
        UNLOCK_APP_AUDIO_QUEUE();
#ifdef A2DP_AUDIO_SYNC_WITH_LOCAL
        q_usedlen += byte_decode;
#endif

        sbc_next_frame_size = sbc_frame_size;
        //TRACE("p %d\n", pcm_data.sampleFreq);

        /* leave */
    }
    else if(ret == XA_STATUS_FAILED) {
        need_init_decoder = true;
        pcm_processed = pcm_data.dataLen;
        pcm_offset += pcm_data.dataLen;
        TRACE("err mutelen:%d\n",pcm_processed);

        LOCK_APP_AUDIO_QUEUE();
        DeCQueue(&sbc_queue, 0, byte_decode);
        UNLOCK_APP_AUDIO_QUEUE();

        sbc_next_frame_size = (sbc_frame_size-byte_decode)>0?(sbc_frame_size-byte_decode):sbc_frame_size;

        if (pcm_offset<pcm_len)
            goto reinit;
        /* leave */
    }
    else if(ret == XA_STATUS_NO_RESOURCES) {
        need_init_decoder = false;
        pcm_processed = pcm_data.dataLen;
        pcm_offset += pcm_data.dataLen;
        TRACE("no_res mutelen:%d\n",pcm_processed);

        LOCK_APP_AUDIO_QUEUE();
        DeCQueue(&sbc_queue, 0, byte_decode);
        UNLOCK_APP_AUDIO_QUEUE();

        sbc_next_frame_size = (sbc_frame_size-byte_decode)>0?(sbc_frame_size-byte_decode):sbc_frame_size;
    }
exit:

    etime = hal_sys_timer_get();
    if (underflow || need_init_decoder)
        TRACE( "sbc_decoder %d/%d %d\n ", TICKS_TO_MS(stime - g_preIrqTime),TICKS_TO_MS(etime - stime),retry);
    g_preIrqTime = stime;

#ifdef A2DP_AUDIO_SYNC_WITH_LOCAL
    if(pcm_len == pcm_processed){
        int size;
        LOCK_APP_AUDIO_QUEUE();
        size = LengthOfCQueue(&sbc_queue);
        UNLOCK_APP_AUDIO_QUEUE();
//        TRACE("decode_sbc_frame Queue remain size:%d frame_size:%d\n",size, sbc_frame_size);
#ifdef __AUDIO_RESAMPLE__
        if(size>=g_sbc_queue_size*4/5){
            a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_SET, A2DP_AUDIO_SYNC_WITH_LOCAL_STEP);
        }else if(size<=g_sbc_queue_size*2/5){
            a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_SET, 0);
        }else{
            a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_PROC, 0);
        }
#else
        A2DP_AUDIO_SYNC_TRACE("sync status:%d qsize:%d/%d fsize:%d, thr:%d used:%d", sync_status,size, g_sbc_queue_size, sbc_frame_size, A2DPPLAY_CACHE_OK_THRESHOLD, q_usedlen);
/*
nor:
    if (size>7488+1170*2)
        goto inc
    if (size<7488-1170)
        goto dec
dec:
    if (size>=7488+1170)
        goto nor
inc:
    if (size<=7488)
        goto nor
*/
        switch (sync_status) {
            case A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_DEC:
                if (size >= (A2DPPLAY_CACHE_OK_THRESHOLD + q_usedlen)){        
                    a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_SET, A2DP_AUDIO_SYNC_WITH_LOCAL_SAMPLERATE_DEFAULT);                    
                    sync_status = A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_DEFAULT;
                    A2DP_AUDIO_SYNC_TRACE( "default pll freq");
                }else{
                    a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_PROC, 0);
                }
                break;
            case A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_INC:
                if (size <= (A2DPPLAY_CACHE_OK_THRESHOLD)){        
                    a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_SET, A2DP_AUDIO_SYNC_WITH_LOCAL_SAMPLERATE_DEFAULT);                    
                    sync_status = A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_DEFAULT;
                    A2DP_AUDIO_SYNC_TRACE( "default pll freq");
                }else{
                    a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_PROC, 0);
                }
                break;       
            case A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_DEFAULT:
            default:
                if (size > (A2DPPLAY_CACHE_OK_THRESHOLD + (q_usedlen<<1))){
                    a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_SET, A2DP_AUDIO_SYNC_WITH_LOCAL_SAMPLERATE_INC);
                    sync_status = A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_INC;
                    A2DP_AUDIO_SYNC_TRACE( "inc pll freq");
                }else if (size < (A2DPPLAY_CACHE_OK_THRESHOLD - q_usedlen)){
                    a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_SET, A2DP_AUDIO_SYNC_WITH_LOCAL_SAMPLERATE_DEC);
                    sync_status = A2DP_AUDIO_SYNC_STATUS_SAMPLERATE_DEC;
                    A2DP_AUDIO_SYNC_TRACE( "dec pll freq");
                }else{
                    a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_PROC, 0);
                }
                break;
        }
#endif
    }
#endif

    return pcm_processed;
}
#endif

#ifdef __AUDIO_RESAMPLE__
#if defined(A2DP_AAC_ON)
static short raw_aac_pcm_data[2][2048];
#endif

void* audio_resample_get_ext_buff(int size)
{
    uint8_t *pBuff = NULL;    
    if (size % 4){
        size = size + (4 - size % 4);
    }
    app_audio_mempool_get_buff(&pBuff, size);
    TRACE( "audio_resample_get_ext_buff len:%d", size);
    return (void*)pBuff;
}

enum resample_id_t bt_get_up_sample_id(enum AUD_SAMPRATE_T sample_rate)
{
    enum resample_id_t resample_id = RESAMPLE_ID_NUM;

    if(a2dp_sample_rate == AUD_SAMPRATE_44100)
    {
        resample_id = RESAMPLE_ID_44P1TO50P7;
    }
    else if(a2dp_sample_rate == AUD_SAMPRATE_48000)
    {
        resample_id = RESAMPLE_ID_48TO50P7;
    }
    else
    {
        TRACE("[%s] Do not support %d sample rate to resample", __func__, sample_rate);
    }

    return resample_id;
}

uint32_t a2dp_audio_more_data(uint8_t codec_type, uint8_t *buf, uint32_t len)
{
    uint32_t l = 0;
    short* tmp_pcm_data = NULL;
    if (!a2dp_audio_isrunning(A2DPPLAY_STRTEAM_GET, true)){        
        TRACE("%s not ready skip frame",__func__);
        memset(buf, 0, len);
        goto exit;
    }

    if (a2dp_cache_status == APP_AUDIO_CACHE_CACHEING){
        TRACE( "a2dp_audio_more_data cache not ready skip frame \n");
        memset(buf, 0, len);
        goto exit;
    }
#if defined(A2DP_AAC_ON)
    if(codec_type == AVDTP_CODEC_TYPE_MPEG2_4_AAC){
        if(g_aac_init_raw_pcm){
            if(g_aac_pcm_offset[g_aac_pcm_index] == 4096){
                int len = 0;
                len = decode_aac_frame((unsigned char *)raw_aac_pcm_data[g_aac_pcm_index], 4096);
                if(len == 4096){
                    g_aac_pcm_offset[g_aac_pcm_index] = 0;
                    g_aac_init_raw_pcm = 0;
                    l = 4096;
                }         
            }
        }else{
            int index  = (g_aac_pcm_index + 1)%2;
            if(g_aac_pcm_offset[index] == 4096){
                int len = 0;
                len = decode_aac_frame((unsigned char *)raw_aac_pcm_data[index], 4096);
                if(len == 4096){
                    g_aac_pcm_offset[index] = 0;
                    l = 4096;
                }
            }
        }    

        if (allow_resample){
            int res;
            //    int32_t stime,etime;
            if (audio_resample_cfg((short *)buf, len/2) != AUDIO_RESAMPLE_STATUS_SUCESS){
                TRACE("%s resample_cfg error",__func__);
                memset(buf, 0, len);        
                audio_resample_flush_data();
                goto exit;
            }
         
           do{          
            //            stime = hal_sys_timer_get();
            //            uint32_t lock;
            //            lock = int_lock();
                    {
                        if(g_aac_pcm_offset[g_aac_pcm_index] == 0){
                            tmp_pcm_data = raw_aac_pcm_data[g_aac_pcm_index];
                            //TRACE("raw_aac_pcm_data[%d] 0x%x\n",g_aac_pcm_index ,raw_aac_pcm_data[g_aac_pcm_index]);
                        }
                        else{
                            if(g_aac_pcm_offset[g_aac_pcm_index] < 4096){
                                tmp_pcm_data = raw_aac_pcm_data[g_aac_pcm_index] + g_aac_pcm_offset[g_aac_pcm_index]/2;
                            }else{
                                if(g_aac_pcm_offset[(g_aac_pcm_index + 1)%2] > 0)
                                    goto exit;
                                else{             
                                        g_aac_pcm_index = (g_aac_pcm_index + 1)%2;
                                        tmp_pcm_data = raw_aac_pcm_data[g_aac_pcm_index];
                                    }
                            }
                        }
                    }
           
                    res = audio_resample_run(tmp_pcm_data, SAMPLE_NUM*2);
                //            int_unlock(lock);
                //            etime = hal_sys_timer_get();
                //            TRACE( "resample: %d ", etime - stime);
                    if(res == AUDIO_RESAMPLE_STATUS_FAILED){            
                        TRACE("%s resample_run error",__func__);
                        memset(buf, 0, len);            
                        audio_resample_flush_data(); 
                        goto exit;
                    }

                    g_aac_pcm_offset[g_aac_pcm_index] += 512;                  
                    //TRACE("g_aac_pcm_offset[%d] %d %d\n", g_aac_pcm_index, g_aac_pcm_offset[g_aac_pcm_index], g_aac_pcm_offset[(g_aac_pcm_index + 1)%2] );
                    if(g_aac_pcm_offset[g_aac_pcm_index] == 4096){
                            if(g_aac_pcm_offset[(g_aac_pcm_index + 1)%2] == 0)
                                g_aac_pcm_index = (g_aac_pcm_index + 1)%2;
                    }
                }while(res == AUDIO_RESAMPLE_STATUS_CONTINUE);
            }else
            {
               if(g_aac_pcm_offset[g_aac_pcm_index] == 0){
                   tmp_pcm_data = raw_aac_pcm_data[g_aac_pcm_index];
                   l = 4096;
                   memcpy(buf, tmp_pcm_data, len);
                   g_aac_pcm_offset[g_aac_pcm_index] = 4096;

                   //TRACE("raw_aac_pcm_data1[%d] 0x%x\n",g_aac_pcm_index ,raw_aac_pcm_data[g_aac_pcm_index]);
                    if(g_aac_pcm_offset[(g_aac_pcm_index + 1)%2] == 0)
                        g_aac_pcm_index = (g_aac_pcm_index + 1)%2;
               }
               else
                    goto exit;
            }
        //TRACE("offset a2dp_audio_more_data %d %d\n", g_aac_pcm_index , g_aac_pcm_offset[g_aac_pcm_index]);
    }else
#endif        
    {
        if (allow_resample){
            int res;
            //    int32_t stime,etime;
            if (audio_resample_cfg((short *)buf, len/2) != AUDIO_RESAMPLE_STATUS_SUCESS){
                TRACE("%s resample_cfg error",__func__);
                memset(buf, 0, len);        
                audio_resample_flush_data();
                goto exit;
            }
            do{
                l = decode_sbc_frame((uint8_t *)raw_pcm_data, SAMPLE_NUM*2*2);
                if (l != SAMPLE_NUM*2*2){            
                    TRACE("%s error l=%d",__func__, l);
                    memset(buf, 0, len);
                    audio_resample_flush_data();
                    a2dp_cache_status = APP_AUDIO_CACHE_CACHEING;
                    TRACE( "set to APP_AUDIO_CACHE_CACHEING"); 
					a2dp_timestamp_parser_needsync();
                    goto exit;
                }
            //            stime = hal_sys_timer_get();
            //            uint32_t lock;
            //            lock = int_lock();
                res = audio_resample_run(raw_pcm_data, SAMPLE_NUM*2);
            //            int_unlock(lock);
            //            etime = hal_sys_timer_get();
            //            TRACE( "resample: %d ", etime - stime);
            
                if(res == AUDIO_RESAMPLE_STATUS_FAILED){            
                    TRACE("%s resample_run error",__func__);
                    memset(buf, 0, len);            
                    audio_resample_flush_data();                
                    goto exit;
                }
            }while(res == AUDIO_RESAMPLE_STATUS_CONTINUE);
        }else{
            l = decode_sbc_frame(buf, len);
            if (l!=len){
                memset(buf+l, 0, len-l);
                TRACE( "a2dp_audio_more_data decode err %d/%d", l, len);
                a2dp_cache_status = APP_AUDIO_CACHE_CACHEING;
                TRACE( "set to APP_AUDIO_CACHE_CACHEING");            
            	a2dp_timestamp_parser_needsync();
			}
        }
    }
exit:
    A2DP_SYNC_WITH_GET_MUTUX_SET();
    return len;
}

#else
uint32_t a2dp_audio_more_data(uint8_t codec_type, uint8_t *buf, uint32_t len)
{
    uint32_t l = 0;

    if (!a2dp_audio_isrunning(A2DPPLAY_STRTEAM_GET, true)){
    }

    if (a2dp_cache_status == APP_AUDIO_CACHE_CACHEING){
#if FPGA==0
        TRACE( "a2dp_audio_more_data cache not ready skip frame %d\n" ,codec_type);
#endif
        memset(buf, 0, len);
    }else{
#if defined(A2DP_AAC_ON)
	if(codec_type == AVDTP_CODEC_TYPE_MPEG2_4_AAC)
        {
            l = decode_aac_frame(buf, len);
        }else
#endif    
        l = decode_sbc_frame(buf, len);
        if (l!=len){
            memset(buf+l, 0, len-l);
            TRACE( "a2dp_audio_more_data decode err %d/%d", l, len);
            a2dp_cache_status = APP_AUDIO_CACHE_CACHEING;
            TRACE( "set to APP_AUDIO_CACHE_CACHEING");            
            a2dp_timestamp_parser_needsync();
        }
    }

    A2DP_SYNC_WITH_GET_MUTUX_SET();

    return len;
}
#endif
int a2dp_audio_init(void)
{
    const float EQLevel[25] = {
        0.0630957,  0.0794328, 0.1,       0.1258925, 0.1584893,
        0.1995262,  0.2511886, 0.3162278, 0.398107 , 0.5011872,
        0.6309573,  0.794328 , 1,         1.258925 , 1.584893 ,
        1.995262 ,  2.5118864, 3.1622776, 3.9810717, 5.011872 ,
        6.309573 ,  7.943282 , 10       , 12.589254, 15.848932
    };//-12~12
    uint8_t *buff = NULL;
    uint8_t i;

#ifdef __AUDIO_RESAMPLE__

#if defined(A2DP_AAC_ON)
        if(bt_sbc_player_get_codec_type() == AVDTP_CODEC_TYPE_MPEG2_4_AAC){
            void * aac_info_prt = NULL;
            app_audio_mempool_get_buff((uint8_t **)&aac_info_prt, GetAACinfoSize());
            SetAACInfoBase(aac_info_prt);
            TRACE("a2dp_audio_init raw_aac_pcm_data = 0x%x f=%d\n", raw_aac_pcm_data, app_audio_mempool_free_buff_size());
        }
#endif        

    if(hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_2 && hal_cmu_sys_get_freq() <= HAL_CMU_FREQ_52M)
    {
        enum resample_id_t resample_id;
        app_audio_mempool_get_buff((uint8_t **)&raw_pcm_data, SAMPLE_NUM*2*2);
 
        resample_id = bt_get_up_sample_id(a2dp_sample_rate);
        if (resample_id != RESAMPLE_ID_NUM)
        {
            audio_resample_open(resample_id, audio_resample_get_ext_buff);
        }
        allow_resample = true;
    }
#else
#if defined(A2DP_AAC_ON)
        if(bt_sbc_player_get_codec_type() == AVDTP_CODEC_TYPE_MPEG2_4_AAC){
            void * aac_info_prt = NULL;
            //app_audio_mempool_get_buff((uint8_t **)&raw_aac_pcm_data[0], 1024*2*2);
            //app_audio_mempool_get_buff((uint8_t **)&raw_aac_pcm_data[1], 1024*2*2);
            app_audio_mempool_get_buff((uint8_t **)&aac_info_prt, GetAACinfoSize());
            SetAACInfoBase(aac_info_prt);
            TRACE("a2dp_audio_init f=%d %d\n", app_audio_mempool_free_buff_size(), GetAACinfoSize());
        }
#endif
#endif

    for (i=0; i<sizeof(sbc_eq_band_gain)/sizeof(float); i++){
        sbc_eq_band_gain[i] = EQLevel[cfg_aud_eq_sbc_band_settings[i]+12];
    }

    A2DP_SYNC_WITH_GET_MUTUX_FREE();
    A2DP_SYNC_WITH_PUT_MUTUX_FREE();
#if defined(A2DP_AAC_ON)    
    if(bt_sbc_player_get_codec_type() == AVDTP_CODEC_TYPE_SBC)
#endif        
    app_audio_mempool_get_buff((uint8_t **)&sbc_decoder, sizeof(SbcDecoder));

#ifdef SBC_QUEUE_SIZE
#if defined(A2DP_AAC_ON)    
    if(bt_sbc_player_get_codec_type() == AVDTP_CODEC_TYPE_MPEG2_4_AAC)
    {
        g_sbc_queue_size = app_audio_mempool_free_buff_size();
    }else
#endif
    {
        g_sbc_queue_size = SBC_QUEUE_SIZE;
    }
#else
    g_sbc_queue_size = app_audio_mempool_free_buff_size();
#endif

    app_audio_mempool_get_buff(&buff, g_sbc_queue_size);
    memset(buff, 0, g_sbc_queue_size);
    a2dp_audio_isrunning(A2DPPLAY_STRTEAM_PUT, false);
    a2dp_audio_isrunning(A2DPPLAY_STRTEAM_GET, false);

    LOCK_APP_AUDIO_QUEUE();
    APP_AUDIO_InitCQueue(&sbc_queue, g_sbc_queue_size, buff);
    UNLOCK_APP_AUDIO_QUEUE();

#ifdef A2DP_AUDIO_SYNC_WITH_LOCAL
    a2dp_audio_sync_proc(A2DPPLAY_SYNC_STATUS_SET|A2DPPLAY_SYNC_STATUS_RESET, A2DP_AUDIO_SYNC_WITH_LOCAL_SAMPLERATE_DEFAULT);
#endif

    a2dp_cache_status = APP_AUDIO_CACHE_CACHEING;
    need_init_decoder =  true;

    sbc_frame_rev_len = 0;
    sbc_frame_num = 0;
    sbc_frame_size = SBC_TEMP_BUFFER_SIZE;
#if defined(A2DP_AAC_ON)    
    hAACDecoder = NULL;
    g_aac_pcm_index = 0;
    g_aac_pcm_offset[0] = 4096;
    g_aac_pcm_offset[1]  = 4096;
    g_aac_init_raw_pcm = 1;
#endif
    return 0;
}

int a2dp_audio_deinit(void)
{
#ifdef __AUDIO_RESAMPLE__
    if (allow_resample){
        audio_resample_close();
    }
#endif

    A2DP_SYNC_WITH_GET_MUTUX_SET();
    A2DP_SYNC_WITH_PUT_MUTUX_SET();

    A2DP_SYNC_WITH_GET_MUTUX_FREE();
    A2DP_SYNC_WITH_PUT_MUTUX_FREE();

    a2dp_audio_isrunning(A2DPPLAY_STRTEAM_PUT, false);
    a2dp_audio_isrunning(A2DPPLAY_STRTEAM_GET, false);

    a2dp_cache_status = APP_AUDIO_CACHE_QTY;
    need_init_decoder =  true;

    sbc_frame_rev_len = 0;
    sbc_frame_num = 0;
    sbc_frame_size = SBC_TEMP_BUFFER_SIZE;

    return 0;
}

