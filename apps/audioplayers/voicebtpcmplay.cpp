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
#ifdef __MBED__
#include "SDFileSystem.h"
#endif
#include "hal_uart.h"
#include "hal_timer.h"
#include "hal_trace.h"
#include "cqueue.h"
#include "app_audio.h"
#include "tgt_hardware.h"
//#define SPEAKER_ALGORITHMGAIN (2.0f)
//#define SPEECH_ALGORITHMGAIN (4.0f)

#if defined(SPEECH_WEIGHTING_FILTER_SUPPRESS) || defined(SPEAKER_WEIGHTING_FILTER_SUPPRESS) || defined(SPEAKER_ALGORITHMGAIN) || defined(SPEECH_ALGORITHMGAIN) || defined(HFP_1_6_ENABLE)
#include "iir_process.h"
#endif

// BT
extern "C" {
#include "eventmgr.h"
#include "me.h"
#include "sec.h"
#include "avdtp.h"
#include "avctp.h"
#include "avrcp.h"
#include "hf.h"
#include "plc.h"
#ifdef  VOICE_DETECT
#include "webrtc_vad_ext.h"
#endif
#if defined(HFP_1_6_ENABLE)
#include "sbc.h"
#endif
#if defined(MSBC_PLC_ENABLE)
#include "sbcplc.h"
#endif
#if defined(_CVSD_BYPASS_)
#include "Pcm8k_Cvsd.h"
#endif
}

//#define SPEECH_PLC


#define VOICEBTPCM_TRACE(s,...)
//TRACE(s, ##__VA_ARGS__)

/* voicebtpcm_pcm queue */
#define VOICEBTPCM_SPEEX_8KHZ_FRAME_LENGTH (120)
#define VOICEBTPCM_PCM_TEMP_BUFFER_SIZE (VOICEBTPCM_SPEEX_8KHZ_FRAME_LENGTH*2)
#define VOICEBTPCM_PCM_QUEUE_SIZE (VOICEBTPCM_PCM_TEMP_BUFFER_SIZE*3)
CQueue voicebtpcm_p2m_queue;
CQueue voicebtpcm_m2p_queue;

static enum APP_AUDIO_CACHE_T voicebtpcm_cache_m2p_status = APP_AUDIO_CACHE_QTY;
static enum APP_AUDIO_CACHE_T voicebtpcm_cache_p2m_status = APP_AUDIO_CACHE_QTY;
#if defined(HFP_1_6_ENABLE)
extern "C" int app_audio_manager_get_scocodecid(void);
#define MSBC_FRAME_SIZE (60)
static uint8_t *msbc_buf_before_decode;
static uint8_t *msbc_buf_before_plc;
static char need_init_decoder = true;
static SbcDecoder msbc_decoder;
#if FPGA==1
#define CFG_HW_AUD_EQ_NUM_BANDS (8)
const int8_t cfg_hw_aud_eq_band_settings[CFG_HW_AUD_EQ_NUM_BANDS] = {0, 0, 0, 0, 0, 0, 0, 0};
#endif
static float msbc_eq_band_gain[CFG_HW_AUD_EQ_NUM_BANDS]={0,0,0,0,0,0,0,0};
static uint16_t msbc_frame_size = MSBC_FRAME_SIZE;

#define MSBC_ENCODE_PCM_LEN (480)
unsigned char *temp_msbc_buf;
unsigned char *temp_msbc_buf1;
static char need_init_encoder = true;
#if defined(MSBC_PLC_ENABLE)
static char msbc_need_check_sync_header = 0;
PLC_State msbc_plc_state;
static uint8_t msbc_recv_seq_no = 0;
static char msbc_can_plc = true;
#endif
static SbcEncoder *msbc_encoder;
static SbcPcmData msbc_encoder_pcmdata;
static unsigned char msbc_counter = 0x08;
#ifndef MSBC_16K_SAMPLE_RATE
static uint16_t * msbc_pcm_resample_cache = NULL;
static uint16_t * msbc_pcm_resample_cache_m = NULL;
static IIR_MONO_CFG_T msbc_mic_cfg[] = {
    {1.f,  
    {1   ,0    ,0.171572875253810,
    0.292893218813452, 0.585786437626905, 0.292893218813452},
    {0.f, 0.f, 0.f, 0.f}
    }
 };

static IIR_MONO_CFG_T msbc_spk_cfg[] = {
    {1.f,  
    {1   ,0    ,0.171572875253810,
    0.292893218813452, 0.585786437626905, 0.292893218813452},
    {0.f, 0.f, 0.f, 0.f}
    }
 };
#endif
#endif

#ifdef VOICE_DETECT
#define VAD_SIZE (VOICEBTPCM_SPEEX_8KHZ_FRAME_LENGTH*2)
#define VAD_CNT_MAX (30)
#define VAD_CNT_MIN (-10)
#define VAD_CNT_INCREMENT (1)
#define VAD_CNT_DECREMENT (1)

static VadInst  *g_vadmic = NULL;
static uint16_t *vad_buff;
static uint16_t vad_buf_size = 0;
static int vdt_cnt = 0;
#endif


#if defined (SPEECH_WEIGHTING_FILTER_SUPPRESS)
static IIR_MONO_CFG_T mic_cfg[] = {
                                     {1.f,
                                     {1.00000000, -1.86616866, 0.87658642,
                                     0.94810458, -1.87117017, 0.92348033},
                                     {0.f, 0.f, 0.f}},
                                    };
/*
HP
a= 1.00000000, -1.86616866, 0.87658642, b= 0.94810458, -1.87117017, 0.92348033, Q=8.000000e-01, f0=60
a= 1.00000000, -1.79827159, 0.82094837, b= 0.92289343, -1.80915859, 0.88716794, Q=8.000000e-01, f0=90
a= 1.00000000, -1.73010321, 0.76910598, b= 0.89822518, -1.74882823, 0.85215577, Q=8.000000e-01, f0=120
a= 1.00000000, -1.66190643, 0.72086961, b= 0.87412777, -1.69021434, 0.81843393, Q=8.000000e-01, f0=150 
a= 1.00000000, -1.54874191, 0.64797877, b= 0.83528658, -1.59638499, 0.76504911, Q=8.000000e-01, f0=200
a= 1.00000000, -1.43687641, 0.58373222, b= 0.79814194, -1.50738110, 0.71508559, Q=8.000000e-01, f0=250 
a= 1.00000000, -1.37064339, 0.54899424, b= 0.77667822, -1.45626868, 0.68669074, Q=8.000000e-01, f0=280 
a= 1.00000000, -1.32692505, 0.52731913, b= 0.76271162, -1.42313318, 0.66839939, Q=8.000000e-01, f0=300 
a= 1.00000000, -1.11447076, 0.43500626, b= 0.69692498, -1.26835814, 0.58419391, Q=8.000000e-01, f0=400 
a= 1.00000000, -0.91365056, 0.36563348, b= 0.63760583, -1.13064514, 0.51103307, Q=8.000000e-01, f0=500 
a= 1.00000000, -0.72537664, 0.31470058, b= 0.58425226, -1.00830791, 0.44751705, Q=8.000000e-01, f0=600
a= 1.00000000, -0.54971399, 0.27854201, b= 0.53629269, -0.89962041, 0.39234289, Q=8.000000e-01, f0=700 
a= 1.00000000, -0.38622682, 0.25421117, b= 0.49315048, -0.80294146, 0.34434605, Q=8.000000e-01, f0=800 
*/
#endif

#if defined (SPEAKER_WEIGHTING_FILTER_SUPPRESS)
static IIR_MONO_CFG_T spk_cfg[] = {1,
                         {1.000000000000000, -1.849861221773838, 0.860188009219491,
                          0.952640907278933, -1.854374582739788, 0.903033740974608},
                         {0.f, 0.f, 0.f}};
#endif

/*
{1.f,
 {1.000000000000000, -1.749673879263390, 0.849865759304125,
  0.902579925106905, -1.799706752231840, 0.897252961228774},
 {0.f, 0.f, 0.f}};
*/

static void copy_one_track_to_two_track_16bits(uint16_t *src_buf, uint16_t *dst_buf, uint32_t src_len)
{
    uint32_t i = 0;
    for (i = 0; i < src_len; ++i) {
        //dst_buf[i*2 + 0] = dst_buf[i*2 + 1] = ((unsigned short)(src_buf[i])<<1);
        dst_buf[i*2 + 0] = dst_buf[i*2 + 1] = (src_buf[i]);
    }
}
void merge_two_track_to_one_track_16bits(uint8_t chnlsel, uint16_t *src_buf, uint16_t *dst_buf,  uint32_t src_len)
{
    uint32_t i = 0;
    for (i = 0; i < src_len; i+=2) {
        dst_buf[i/2] = src_buf[i + chnlsel];
    }
}

//playback flow
//bt-->store_voicebtpcm_m2p_buffer-->decode_voicebtpcm_m2p_frame-->audioflinger playback-->speaker
//used by playback, store data from bt to memory
int store_voicebtpcm_m2p_buffer(unsigned char *buf, unsigned int len)
{
    int size;
    unsigned int avail_size;

    LOCK_APP_AUDIO_QUEUE();
    avail_size = APP_AUDIO_AvailableOfCQueue(&voicebtpcm_m2p_queue);
    if (len <= avail_size){
        APP_AUDIO_EnCQueue(&voicebtpcm_m2p_queue, buf, len);
    }else{
        VOICEBTPCM_TRACE( "spk buff overflow %d/%d", len, avail_size);
#if defined(HFP_1_6_ENABLE)
        if (app_audio_manager_get_scocodecid() == HF_SCO_CODEC_MSBC) {
            TRACE( "drop frame");
        }
        else
#endif
        {
            APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, len - avail_size);
            APP_AUDIO_EnCQueue(&voicebtpcm_m2p_queue, buf, len);
        }
    }    
    size = APP_AUDIO_LengthOfCQueue(&voicebtpcm_m2p_queue);
    UNLOCK_APP_AUDIO_QUEUE();

    if (size > (VOICEBTPCM_PCM_TEMP_BUFFER_SIZE)) {
        voicebtpcm_cache_m2p_status = APP_AUDIO_CACHE_OK;
    }

    VOICEBTPCM_TRACE("m2p :%d/%d", len, size);

    return 0;
}

//used by playback, decode data from memory to speaker
int decode_voicebtpcm_m2p_frame(unsigned char *pcm_buffer, unsigned int pcm_len)
{
    int r = 0, got_len = 0;
    unsigned char *e1 = NULL, *e2 = NULL;
    unsigned int len1 = 0, len2 = 0;

    LOCK_APP_AUDIO_QUEUE();
    r = APP_AUDIO_PeekCQueue(&voicebtpcm_m2p_queue, pcm_len - got_len, &e1, &len1, &e2, &len2);
    UNLOCK_APP_AUDIO_QUEUE();

    if (r){
        VOICEBTPCM_TRACE( "spk buff underflow");
    }

    if(r == CQ_OK) {
        //app_audio_memcpy_16bit(pcm_buffer + got_len, e1, len1/2);
        if (len1){
//            copy_one_trace_to_two_track_16bits((uint16_t *)e1, (uint16_t *)(pcm_buffer + got_len), len1/2);
            app_audio_memcpy_16bit((short *)(pcm_buffer), (short *)e1, len1/2);
            LOCK_APP_AUDIO_QUEUE();
            APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, len1);
            UNLOCK_APP_AUDIO_QUEUE();
            got_len += len1;
        }
        if (len2) {
            //app_audio_memcpy_16bit(pcm_buffer + got_len, e2, len2);
//            copy_one_trace_to_two_track_16bits((uint16_t *)e2, (uint16_t *)(pcm_buffer + got_len), len2/2);
            app_audio_memcpy_16bit((short *)(pcm_buffer + len1), (short *)e2, len2/2);
            LOCK_APP_AUDIO_QUEUE();
            APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, len2);
            UNLOCK_APP_AUDIO_QUEUE();
            got_len += len2;
        }
    }

    return got_len;
}

#if defined(_CVSD_BYPASS_)
#define VOICECVSD_TEMP_BUFFER_SIZE 120
#define VOICECVSD_ENC_SIZE 60
static short cvsd_decode_buff[VOICECVSD_TEMP_BUFFER_SIZE*2];
static void copy_one_trace_to_two_track_16bits(uint16_t *src_buf, uint16_t *dst_buf, uint32_t src_len)
{
    uint32_t i = 0;
    for (i = 0; i < src_len; ++i) {
        dst_buf[i*2 + 0] = dst_buf[i*2 + 1] = src_buf[i];
    }
}
int decode_cvsd_frame(unsigned char *pcm_buffer, unsigned int pcm_len)
{
    uint32_t r = 0, decode_len = 0;
    unsigned char *e1 = NULL, *e2 = NULL;
    unsigned int len1 = 0, len2 = 0;

    while (decode_len < pcm_len) {
        LOCK_APP_AUDIO_QUEUE();
        len1 = len2 = 0;
        e1 = e2 = 0;
        r = APP_AUDIO_PeekCQueue(&voicebtpcm_m2p_queue, VOICECVSD_TEMP_BUFFER_SIZE, &e1, &len1, &e2, &len2);
        UNLOCK_APP_AUDIO_QUEUE();  

        if (r == CQ_ERR){
            memset(pcm_buffer, 0, pcm_len);
            TRACE( "cvsd spk buff underflow");
            return 0;
        }

        if (len1 != 0) {
            CvsdToPcm8k(e1, (short *)(cvsd_decode_buff), len1, 0);
            //copy_one_trace_to_two_track_16bits((uint16_t *)cvsd_decode_buff, (uint16_t *)(pcm_buffer + decode_len), len1);

            LOCK_APP_AUDIO_QUEUE();
            DeCQueue(&voicebtpcm_m2p_queue, 0, len1);
            UNLOCK_APP_AUDIO_QUEUE();

            //decode_len += len1*4;
            decode_len += len1*2;
        }

        if (len2 != 0) {
            CvsdToPcm8k(e2, (short *)(cvsd_decode_buff), len2, 0);
            //copy_one_trace_to_two_track_16bits((uint16_t *)cvsd_decode_buff, (uint16_t *)(pcm_buffer + decode_len), len2);

            LOCK_APP_AUDIO_QUEUE();
            DeCQueue(&voicebtpcm_m2p_queue, 0, len2);
            UNLOCK_APP_AUDIO_QUEUE();

            //decode_len += len2*4;
            decode_len += len2*2;
        }
    }

    memcpy(pcm_buffer, cvsd_decode_buff, decode_len);

    return decode_len;
}
int encode_cvsd_frame(unsigned char *pcm_buffer, unsigned int pcm_len)
{
    uint32_t r = 0;
    unsigned char *e1 = NULL, *e2 = NULL;
    unsigned int len1 = 0, len2 = 0;
    uint32_t processed_len = 0;
    uint32_t remain_len = 0, enc_len = 0;

    while (processed_len < pcm_len) {
        remain_len = pcm_len-processed_len;

        if (remain_len>(VOICECVSD_ENC_SIZE*2)){
            enc_len = VOICECVSD_ENC_SIZE*2;
        }else{
            enc_len = remain_len*2;
        }

        LOCK_APP_AUDIO_QUEUE();
        len1 = len2 = 0;
        e1 = e2 = 0;
        r = APP_AUDIO_PeekCQueue(&voicebtpcm_p2m_queue, enc_len, &e1, &len1, &e2, &len2);
        UNLOCK_APP_AUDIO_QUEUE();  

        if (r == CQ_ERR){
            memset(pcm_buffer, 0, pcm_len);
            TRACE( "cvsd spk buff underflow");
            return 0;
        }

        if (e1) {
            Pcm8kToCvsd((short *)e1, (unsigned char *)(pcm_buffer + processed_len), len1/2);
            LOCK_APP_AUDIO_QUEUE();
            DeCQueue(&voicebtpcm_p2m_queue, NULL, len1);
            UNLOCK_APP_AUDIO_QUEUE();
            processed_len += len1/2;
        }
        if (e2) {
            Pcm8kToCvsd((short *)e2, (unsigned char *)(pcm_buffer + processed_len), len2/2);
            LOCK_APP_AUDIO_QUEUE();
            DeCQueue(&voicebtpcm_p2m_queue, NULL, len2);
            UNLOCK_APP_AUDIO_QUEUE();
            processed_len += len2/2;
        }
    }

#if 0
    for (int cc = 0; cc < 32; ++cc) {
        TRACE("%x-", e1[cc]);
    }
#endif

    TRACE("%s: processed_len %d, pcm_len %d", __func__, processed_len, pcm_len);

    return processed_len;
}
#endif
#if defined(HFP_1_6_ENABLE)
int decode_msbc_frame(unsigned char *pcm_buffer, unsigned int pcm_len)
{
    int ttt = 0;
    int t = 0;
    uint8_t underflow = 0;
#if defined(MSBC_PLC_ENABLE)
    uint8_t fixed_sync_success = 0;
    uint8_t plc_type = 0;
    uint8_t need_check_pkt = 1;
    uint8_t cur_pkt_seqno = 0;
    uint8_t cur_pkt_sync_byte = 0; //sbc sync
#endif
    int r = 0;
    unsigned char *e1 = NULL, *e2 = NULL, *msbc_buff = NULL;
    unsigned int len1 = 0, len2 = 0;
    static SbcPcmData pcm_data;
    static unsigned int msbc_next_frame_size;
    XaStatus ret = XA_STATUS_SUCCESS;
    unsigned short byte_decode = 0;

    unsigned int pcm_offset = 0;
    unsigned int pcm_processed = 0;

#if defined(MSBC_PLC_ENABLE)
    pcm_data.data = (unsigned char*)msbc_buf_before_plc;
#else
    pcm_data.data = (unsigned char*)pcm_buffer;
#endif
    if (!msbc_next_frame_size){
        msbc_next_frame_size = msbc_frame_size; 
    }

reinit:
    if(need_init_decoder) {
        TRACE("init msbc decoder\n");
        pcm_data.data = (unsigned char*)(pcm_buffer + pcm_offset);
        pcm_data.dataLen = 0;
#ifdef __SBC_FUNC_IN_ROM__
        SBC_ROM_FUNC.SBC_InitDecoder(&msbc_decoder);
#else
        SBC_InitDecoder(&msbc_decoder);
#endif

        msbc_decoder.streamInfo.mSbcFlag = 1;
        msbc_decoder.streamInfo.bitPool = 26;
        msbc_decoder.streamInfo.sampleFreq = SBC_CHNL_SAMPLE_FREQ_16;
        msbc_decoder.streamInfo.channelMode = SBC_CHNL_MODE_MONO;
        msbc_decoder.streamInfo.allocMethod = SBC_ALLOC_METHOD_LOUDNESS;
        /* Number of blocks used to encode the stream (4, 8, 12, or 16) */
        msbc_decoder.streamInfo.numBlocks = MSBC_BLOCKS;
        /* The number of subbands in the stream (4 or 8) */
        msbc_decoder.streamInfo.numSubBands = 8;
        msbc_decoder.streamInfo.numChannels = 1;
#if defined(MSBC_PLC_ENABLE)
        InitPLC(&msbc_plc_state);
        msbc_need_check_sync_header = 0;
#endif
    }

#if defined(MSBC_PLC_ENABLE)
    need_check_pkt = 1;
#endif
    msbc_buff = msbc_buf_before_decode;

get_again:
    LOCK_APP_AUDIO_QUEUE();
    len1 = len2 = 0;
    e1 = e2 = 0;
    r = APP_AUDIO_PeekCQueue(&voicebtpcm_m2p_queue, msbc_next_frame_size, &e1, &len1, &e2, &len2);
    UNLOCK_APP_AUDIO_QUEUE();  

    if (r == CQ_ERR){
        pcm_processed = pcm_len;
        memset(pcm_buffer, 0, pcm_len);
        TRACE( "msbc spk buff underflow");
        goto exit;	
    }

    if (!len1){
        TRACE("len1 underflow %d/%d\n", len1, len2);
        goto get_again;
    }

    if (len1 > 0 && e1) {
        memcpy(msbc_buff, e1, len1);
    }
    if (len2 > 0 && e2) {
        memcpy(msbc_buff + len1, e2, len2);
    }

    //DUMP8("%02x ", msbc_buf_before_decode, 8);
#if defined(MSBC_PLC_ENABLE)
    // [0]-1,msbc sync byte
    // [1]-seqno
    // code_frame(57 bytes)
    // padding(1 byte)
    if (need_check_pkt) {
        if(msbc_need_check_sync_header){
            unsigned int sync_offset = 0;
            do{
                if((msbc_buff[sync_offset] == 0x01 || msbc_buff[sync_offset] == 0x00) 
                            && (msbc_buff[sync_offset + 2] == 0xad)){
                    break;
                }
                sync_offset ++;
            }while(sync_offset < (MSBC_FRAME_SIZE - 3));
            
            msbc_need_check_sync_header = 0;
            if(sync_offset > 0 && sync_offset != (MSBC_FRAME_SIZE - 3)){
                memmove(msbc_buff, msbc_buff + sync_offset, (MSBC_FRAME_SIZE - sync_offset));      
                msbc_next_frame_size = sync_offset;
                msbc_buff += (MSBC_FRAME_SIZE - sync_offset);
                fixed_sync_success = 1;
                APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, sync_offset);
                //TRACE("***msbc_need_check_sync %d :%x %x %x\n", sync_offset, msbc_buf_before_decode[0],
                //        msbc_buf_before_decode[1],
                //        msbc_buf_before_decode[2]);
                goto get_again;
            }else if(sync_offset == (MSBC_FRAME_SIZE - 3)){
                //APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, sync_offset);
                //just jump sync length + padding length(1byte)
                //will in plc again, so can check sync again
                //TRACE("***msbc_need_check_sync %d :%x %x %x\n", sync_offset, msbc_buf_before_decode[0],
               //         msbc_buf_before_decode[1],
               //         msbc_buf_before_decode[2]);
                msbc_next_frame_size = (MSBC_FRAME_SIZE - 3) - 1;
            }
        }        
        // 0 - normal decode without plc proc, 1 - normal decode with plc proc, 2 - special decode with plc proc
        plc_type = 0;

        cur_pkt_seqno = msbc_buf_before_decode[1];
        cur_pkt_sync_byte = msbc_buf_before_decode[2];
        
        if (msbc_can_plc) {
            if ((cur_pkt_seqno != 0x08 && cur_pkt_seqno != 0x38 && cur_pkt_seqno != 0xc8 && cur_pkt_seqno != 0xf8) 
                    || (cur_pkt_sync_byte != 0xad)) {
                plc_type = 2;
                msbc_need_check_sync_header = 1;
            }
            else
                plc_type = 1;
        }
        else {
            plc_type = 0;
        }

        if(fixed_sync_success){
            msbc_next_frame_size = MSBC_FRAME_SIZE;
        }
        need_check_pkt = 0;
    }

    //TRACE("type %d, seqno 0x%x, q_space %d\n", plc_type, cur_pkt_seqno, APP_AUDIO_AvailableOfCQueue(&voicebtpcm_m2p_queue));
#endif

    //DUMP8("%02x ", msbc_buf_before_decode, msbc_next_frame_size);
    //TRACE("\n");

#if defined(MSBC_PLC_ENABLE)
    if (plc_type == 1) {
#ifdef __SBC_FUNC_IN_ROM__                
        ret = SBC_ROM_FUNC.SBC_DecodeFrames(&msbc_decoder, (unsigned char *)msbc_buf_before_decode, msbc_next_frame_size, &byte_decode,
                &pcm_data, pcm_len - pcm_offset, msbc_eq_band_gain);
#else
        ret = SBC_DecodeFrames(&msbc_decoder, (unsigned char *)msbc_buf_before_decode, msbc_next_frame_size, &byte_decode,
                &pcm_data, pcm_len - pcm_offset, msbc_eq_band_gain);
#endif
        ttt = hal_sys_timer_get();
        PLC_good_frame(&msbc_plc_state, (short *)pcm_data.data, (short *)pcm_buffer);
        //TRACE("g %d\n", (hal_sys_timer_get()-ttt));
    }
    else if (plc_type == 2) {
#ifdef __SBC_FUNC_IN_ROM__                
        ret = SBC_ROM_FUNC.SBC_DecodeFrames(&msbc_decoder, (unsigned char *)indices0, msbc_frame_size-3, &byte_decode,
                &pcm_data, pcm_len - pcm_offset, msbc_eq_band_gain);
#else
        ret = SBC_DecodeFrames(&msbc_decoder, (unsigned char *)indices0, msbc_frame_size-3, &byte_decode,
                &pcm_data, pcm_len - pcm_offset, msbc_eq_band_gain);
#endif
        ttt = hal_sys_timer_get();
        PLC_bad_frame(&msbc_plc_state, (short *)pcm_data.data, (short *)pcm_buffer);
        TRACE("b %d\n", (hal_sys_timer_get()-ttt));
    }
    else
#endif
    {
#ifdef __SBC_FUNC_IN_ROM__                
        ret = SBC_ROM_FUNC.SBC_DecodeFrames(&msbc_decoder, (unsigned char *)msbc_buf_before_decode, msbc_next_frame_size, &byte_decode,
                &pcm_data, pcm_len - pcm_offset, msbc_eq_band_gain);
#else
        ret = SBC_DecodeFrames(&msbc_decoder, (unsigned char *)msbc_buf_before_decode, msbc_next_frame_size, &byte_decode,
                &pcm_data, pcm_len - pcm_offset, msbc_eq_band_gain);
#endif
    }

#if 0
   TRACE("[0] %x", msbc_buf_before_decode[0]);
   TRACE("[1] %x", msbc_buf_before_decode[1]);
   TRACE("[2] %x", msbc_buf_before_decode[2]);
   TRACE("[3] %x", msbc_buf_before_decode[3]);
   TRACE("[4] %x", msbc_buf_before_decode[4]);
#endif

    //TRACE("sbcd ret %d %d\n", ret, byte_decode);

    if(ret == XA_STATUS_CONTINUE) {
        need_init_decoder = false;
        LOCK_APP_AUDIO_QUEUE();
        VOICEBTPCM_TRACE("000000 byte_decode =%d, current_len1 =%d",byte_decode, msbc_next_frame_size);
        APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, byte_decode);
        UNLOCK_APP_AUDIO_QUEUE();

        msbc_next_frame_size = (msbc_frame_size -byte_decode)>0?(msbc_frame_size -byte_decode):msbc_frame_size;
        goto get_again;
    }

    else if(ret == XA_STATUS_SUCCESS) {
        need_init_decoder = false;
        pcm_processed = pcm_data.dataLen;
        pcm_data.dataLen = 0;
#if 1
        VOICEBTPCM_TRACE("111111 msbc_next_frame_size =%d ,  byte_decode =%d, curr_len1 =%d",msbc_next_frame_size,byte_decode,msbc_next_frame_size);
#endif

        LOCK_APP_AUDIO_QUEUE();
#if defined(MSBC_PLC_ENABLE)
        if (plc_type == 0) {
            byte_decode += 1;//padding
            APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, byte_decode);
        }
        else {
            if(msbc_next_frame_size < msbc_frame_size)
                msbc_next_frame_size += 1; //padding
            APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, msbc_next_frame_size);
        }
#else
        if(msbc_next_frame_size < msbc_frame_size)
            msbc_next_frame_size += 1; //padding
        APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, msbc_next_frame_size);
#endif
        UNLOCK_APP_AUDIO_QUEUE();

        msbc_next_frame_size = msbc_frame_size;

#if defined(MSBC_PLC_ENABLE)
        // plc after a good frame
        if (!msbc_can_plc) {
            msbc_can_plc = true;
        }
#endif
    }
    else if(ret == XA_STATUS_FAILED) {
        need_init_decoder = true;
        pcm_processed = pcm_len;
        pcm_data.dataLen = 0;

        memset(pcm_buffer, 0, pcm_len);
        TRACE("err mutelen:%d\n",pcm_processed);

        LOCK_APP_AUDIO_QUEUE();
        APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, byte_decode);
        UNLOCK_APP_AUDIO_QUEUE();

        msbc_next_frame_size = msbc_frame_size;

        /* leave */
    }
    else if(ret == XA_STATUS_NO_RESOURCES) {
        need_init_decoder = true;
        pcm_processed = pcm_len;
        pcm_data.dataLen = 0;

        memset(pcm_buffer, 0, pcm_len);
        TRACE("no_res mutelen:%d\n",pcm_processed);

        LOCK_APP_AUDIO_QUEUE();
        APP_AUDIO_DeCQueue(&voicebtpcm_m2p_queue, 0, byte_decode);
        UNLOCK_APP_AUDIO_QUEUE();

        msbc_next_frame_size = msbc_frame_size;
    }

exit:
    if (underflow||need_init_decoder){
        TRACE( "media_msbc_decoder underflow len:%d\n ", pcm_len);
    }

//    TRACE("pcm_processed %d", pcm_processed);

    return pcm_processed;
}
#endif
//capture flow
//mic-->audioflinger capture-->store_voicebtpcm_p2m_buffer-->get_voicebtpcm_p2m_frame-->bt
//used by capture, store data from mic to memory
int store_voicebtpcm_p2m_buffer(unsigned char *buf, unsigned int len)
{
    int size;
    unsigned int avail_size = 0;
    LOCK_APP_AUDIO_QUEUE();
//    merge_two_trace_to_one_track_16bits(0, (uint16_t *)buf, (uint16_t *)buf, len>>1);
//    r = APP_AUDIO_EnCQueue(&voicebtpcm_p2m_queue, buf, len>>1);
    avail_size = APP_AUDIO_AvailableOfCQueue(&voicebtpcm_p2m_queue);
    if (len <= avail_size){
        APP_AUDIO_EnCQueue(&voicebtpcm_p2m_queue, buf, len);
    }else{
        VOICEBTPCM_TRACE( "mic buff overflow %d/%d", len, avail_size);
        APP_AUDIO_DeCQueue(&voicebtpcm_p2m_queue, 0, len - avail_size);
        APP_AUDIO_EnCQueue(&voicebtpcm_p2m_queue, buf, len);
    }
    size = APP_AUDIO_LengthOfCQueue(&voicebtpcm_p2m_queue);
    UNLOCK_APP_AUDIO_QUEUE();

    VOICEBTPCM_TRACE("p2m :%d/%d", len, size);

    return 0;
}

#if defined(HFP_1_6_ENABLE)
unsigned char get_msbc_counter(void)
{
    if (msbc_counter == 0x08) {
        msbc_counter = 0x38;
    }
    else if (msbc_counter == 0x38) {
        msbc_counter = 0xC8;
    }
    else if (msbc_counter == 0xC8) {
        msbc_counter = 0xF8;
    }
    else if (msbc_counter == 0xF8) {
        msbc_counter = 0x08;
    }

    return msbc_counter;
}
#endif

//used by capture, get the memory data which has be stored by store_voicebtpcm_p2m_buffer()
int get_voicebtpcm_p2m_frame(unsigned char *buf, unsigned int len)
{
    int r = 0, got_len = 0;
    unsigned char *e1 = NULL, *e2 = NULL;
    unsigned int len1 = 0, len2 = 0;
//    int size;

    if (voicebtpcm_cache_p2m_status == APP_AUDIO_CACHE_CACHEING){
        app_audio_memset_16bit((short *)buf, 0, len/2);
        got_len = len;
    }else{
#if defined(HFP_1_6_ENABLE)
    if (app_audio_manager_get_scocodecid() == HF_SCO_CODEC_MSBC) {
        uint16_t bytes_encoded = 0, buf_len = len;
        uint16_t *dest_buf = 0, offset = 0;

        dest_buf = (uint16_t *)buf;

reinit_encoder:
        if(need_init_encoder) {
            SBC_InitEncoder(msbc_encoder);
            msbc_encoder->streamInfo.mSbcFlag = 1;
            msbc_encoder->streamInfo.numChannels = 1;
            msbc_encoder->streamInfo.channelMode = SBC_CHNL_MODE_MONO;

            msbc_encoder->streamInfo.bitPool     = 26;
            msbc_encoder->streamInfo.sampleFreq  = SBC_CHNL_SAMPLE_FREQ_16;
            msbc_encoder->streamInfo.allocMethod = SBC_ALLOC_METHOD_LOUDNESS;
            msbc_encoder->streamInfo.numBlocks   = MSBC_BLOCKS;
            msbc_encoder->streamInfo.numSubBands = 8;
            need_init_encoder = 0;
        }
        LOCK_APP_AUDIO_QUEUE();
        r = APP_AUDIO_PeekCQueue(&voicebtpcm_p2m_queue, MSBC_ENCODE_PCM_LEN, &e1, &len1, &e2, &len2);
        UNLOCK_APP_AUDIO_QUEUE();

        if(r == CQ_OK) {
            if (len1){
                memcpy(temp_msbc_buf, e1, len1);
                LOCK_APP_AUDIO_QUEUE();
                APP_AUDIO_DeCQueue(&voicebtpcm_p2m_queue, 0, len1);
                UNLOCK_APP_AUDIO_QUEUE();
                got_len += len1;
            }
            if (len2 != 0) {
                memcpy(temp_msbc_buf+got_len, e2, len2);
                got_len += len2;
                LOCK_APP_AUDIO_QUEUE();
                APP_AUDIO_DeCQueue(&voicebtpcm_p2m_queue, 0, len2);
                UNLOCK_APP_AUDIO_QUEUE();
            }

            int t = 0;
            t = hal_sys_timer_get();
            msbc_encoder_pcmdata.data = temp_msbc_buf;
            msbc_encoder_pcmdata.dataLen = MSBC_ENCODE_PCM_LEN/2;
            memset(temp_msbc_buf1, 0, MSBC_ENCODE_PCM_LEN);
            SBC_EncodeFrames(msbc_encoder, &msbc_encoder_pcmdata, &bytes_encoded, temp_msbc_buf1, (uint16_t *)&buf_len, 0xFFFF);
            //TRACE("enc msbc %d t\n", hal_sys_timer_get()-t);
            //TRACE("encode len %d, out len %d\n", bytes_encoded, buf_len);

            dest_buf[offset++] = 1<<8;
            dest_buf[offset++] = get_msbc_counter()<<8;

            for (int i = 0; i < buf_len; ++i) {
                dest_buf[offset++] = temp_msbc_buf1[i]<<8;
            }

            dest_buf[offset++] = 0; //padding

            msbc_encoder_pcmdata.data = temp_msbc_buf + MSBC_ENCODE_PCM_LEN/2;
            msbc_encoder_pcmdata.dataLen = MSBC_ENCODE_PCM_LEN/2;
            memset(temp_msbc_buf1, 0, MSBC_ENCODE_PCM_LEN);
            SBC_EncodeFrames(msbc_encoder, &msbc_encoder_pcmdata, &bytes_encoded, temp_msbc_buf1, (uint16_t *)&buf_len, 0xFFFF);
            //TRACE("encode len %d, out len %d\n", bytes_encoded, buf_len);

            dest_buf[offset++] = 1<<8;
            dest_buf[offset++] = get_msbc_counter()<<8;

            for (int i = 0; i < buf_len; ++i) {
                dest_buf[offset++] = temp_msbc_buf1[i]<<8;
            }
            dest_buf[offset++] = 0; //padding

            got_len = len;
        }
    }
    else
#endif
    {
#if defined(_CVSD_BYPASS_)
        got_len = encode_cvsd_frame(buf, len);
#else
        LOCK_APP_AUDIO_QUEUE();
        //        size = APP_AUDIO_LengthOfCQueue(&voicebtpcm_p2m_queue);
        r = APP_AUDIO_PeekCQueue(&voicebtpcm_p2m_queue, len - got_len, &e1, &len1, &e2, &len2);
        UNLOCK_APP_AUDIO_QUEUE();

        //        VOICEBTPCM_TRACE("p2m :%d/%d", len, APP_AUDIO_LengthOfCQueue(&voicebtpcm_p2m_queue));

        if(r == CQ_OK) {
            if (len1){
                app_audio_memcpy_16bit((short *)buf, (short *)e1, len1/2);
                LOCK_APP_AUDIO_QUEUE();
                APP_AUDIO_DeCQueue(&voicebtpcm_p2m_queue, 0, len1);
                UNLOCK_APP_AUDIO_QUEUE();
                got_len += len1;
            }
            if (len2 != 0) {
                app_audio_memcpy_16bit((short *)(buf+got_len), (short *)e2, len2/2);
                got_len += len2;
                LOCK_APP_AUDIO_QUEUE();
                APP_AUDIO_DeCQueue(&voicebtpcm_p2m_queue, 0, len2);
                UNLOCK_APP_AUDIO_QUEUE();
            }
        }else{
            VOICEBTPCM_TRACE( "mic buff underflow");
            app_audio_memset_16bit((short *)buf, 0, len/2);
            got_len = len;
            voicebtpcm_cache_p2m_status = APP_AUDIO_CACHE_CACHEING;
        }
#endif
      }
    }

    return got_len;
}

#if defined (SPEECH_PLC)
static struct PlcSt *speech_lc;
#endif

#if defined( SPEECH_ECHO_CANCEL ) || defined( SPEECH_NOISE_SUPPRESS ) || defined(SPEAKER_NOISE_SUPPRESS )

#include "speex/speex_echo.h"
#include "speex/speex_preprocess.h"

#define SPEEX_BUFF_SIZE (VOICEBTPCM_SPEEX_8KHZ_FRAME_LENGTH)

static short *e_buf;

#if defined( SPEECH_ECHO_CANCEL )
static SpeexEchoState *st=NULL;
static short *ref_buf;
static short *echo_buf;
#endif

#if defined( SPEECH_NOISE_SUPPRESS )
static SpeexPreprocessState *den=NULL;
#endif

#if defined( SPEAKER_NOISE_SUPPRESS )
static SpeexPreprocessState *den_voice = NULL;
#endif

#endif

#if 0
void get_mic_data_max(short *buf, uint32_t len)
{
    int max0 = -32768, min0 = 32767, diff0 = 0;
    int max1 = -32768, min1 = 32767, diff1 = 0;

    for(uint32_t i=0; i<len/2;i+=2)
    {
        if(buf[i+0]>max0)
        {
            max0 = buf[i+0];
        }

        if(buf[i+0]<min0)
        {
            min0 = buf[i+0];
        }

        if(buf[i+1]>max1)
        {
            max1 = buf[i+1];
        }

        if(buf[i+1]<min1)
        {
            min1 = buf[i+1];
        }
    }
    TRACE("min0 = %d, max0 = %d, diff0 = %d, min1 = %d, max1 = %d, diff1 = %d", min0, max0, max0 - min0, min1, max1, max1 - min1);
}
#endif

#ifdef VOICE_DETECT
int16_t voicebtpcm_vdt(uint8_t *buf, uint32_t len)
{
        int16_t ret = 0;
        uint16_t buf_size = 0;
        uint16_t *p_buf = NULL;
         if(len == 120){
            	vad_buf_size += VAD_SIZE/2; //VAD BUFF IS UINT16
            	if(vad_buf_size < VAD_SIZE){
            		app_audio_memcpy_16bit((short *)vad_buff , buf, VAD_SIZE/2);
            		return ret;
            	}
                	app_audio_memcpy_16bit((short *)(vad_buff + VAD_SIZE), buf, VAD_SIZE/2);
                 p_buf = vad_buff;
                 buf_size = VAD_SIZE;
         }else if(len == 160){
                 p_buf = (uint16_t *)buf;
                 buf_size = VOICEBTPCM_SPEEX_8KHZ_FRAME_LENGTH;
         }else
                 return 0;

	ret = WebRtcVad_Process((VadInst*)g_vadmic, 8000, (WebRtc_Word16 *)p_buf, buf_size);
	vad_buf_size = 0;
         return ret;
}
#endif	

//used by capture, store data from mic to memory
uint32_t voicebtpcm_pcm_audio_data_come(uint8_t *buf, uint32_t len)
{
	int16_t ret = 0;
	bool vdt = false;
//	int32_t loudness,gain,max1,max2;	
//    uint32_t stime, etime;
//    static uint32_t preIrqTime = 0;

//  VOICEBTPCM_TRACE("%s enter", __func__);
//    stime = hal_sys_timer_get();
    //get_mic_data_max((short *)buf, len);
    int size = 0;

#if defined( SPEECH_ECHO_CANCEL ) || defined( SPEECH_NOISE_SUPPRESS )
    {

 //       int i;
        short *buf_p=(short *)buf;

//        uint32_t lock = int_lock();
#if defined (SPEECH_ECHO_CANCEL)
    //       for(i=0;i<120;i++)
    //       {
    //           ref_buf[i]=buf_p[i];
    //       }
 //       app_audio_memcpy_16bit((short *)ref_buf, buf_p, SPEEX_BUFF_SIZE);
        speex_echo_cancellation(st, buf_p, echo_buf, e_buf);
//#else
    //        for(i=0;i<120;i++)
    //        {
    //            e_buf[i]=buf_p[i];
    //        }
//        app_audio_memcpy_16bit((short *)e_buf, buf_p, SPEEX_BUFF_SIZE);
#else
    e_buf = buf_p;
#endif
#ifdef VOICE_DETECT
    ret = voicebtpcm_vdt((uint8_t*)e_buf,  len);
	if(ret){
	if(vdt_cnt < 0)
		vdt_cnt = 3;
	else
    		vdt_cnt += ret*VAD_CNT_INCREMENT*3;
    }else{
        // not detect voice
    	vdt_cnt -=VAD_CNT_DECREMENT;
    }
	if (vdt_cnt > VAD_CNT_MAX)
		vdt_cnt = VAD_CNT_MAX;
	if (vdt_cnt < VAD_CNT_MIN)
		vdt_cnt = VAD_CNT_MIN;
	if (vdt_cnt > 0)
		vdt =  true;
#endif
#if defined (SPEECH_NOISE_SUPPRESS)
#ifdef VOICE_DETECT
		float gain;
		if (vdt){
			gain = 31.f;
			speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_AGC_FIX_GAIN, &gain);
		}else{
			gain = 1.f;
			speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_AGC_FIX_GAIN, &gain);
		}
#endif
#ifdef NOISE_SUPPRESS_ADAPTATION
{
    int32_t l2;
    float gain;
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_GET_AGC_GAIN_RAW, &gain);
    if (gain>3.f){        
        l2 = -21;
        speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &l2);
    }else{  
        l2 = -9;
        speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &l2);
    }
}
#endif
    speex_preprocess_run(den, e_buf);
#endif
//        int_unlock(lock);
    //        for(i=0;i<120;i++)
    //        {
    //            buf_p[i]=e_buf[i];
    //        }
    //    app_audio_memcpy_16bit((short *)buf_p, e_buf, sizeof(e_buf)/2);


#if defined (SPEECH_WEIGHTING_FILTER_SUPPRESS)
        for (uint8_t i=0; i<sizeof(mic_cfg)/sizeof(IIR_MONO_CFG_T);i++){
            iir_run_mono_16bits(&mic_cfg[i], (int16_t *)e_buf, len/2);
        }
#endif

        buf = (uint8_t *)e_buf;

#if defined(HFP_1_6_ENABLE)
#ifndef MSBC_16K_SAMPLE_RATE
        //resample 8k--->16k
        if (app_audio_manager_get_scocodecid() == HF_SCO_CODEC_MSBC){
            for(int index = 0;  index < MSBC_ENCODE_PCM_LEN/4; index ++){
                msbc_pcm_resample_cache[2*index] = ((uint16_t *)buf)[index];
                msbc_pcm_resample_cache[2*index + 1]  = ((uint16_t *)buf)[index];
            }
            
            for (uint8_t i=0; i<sizeof(msbc_mic_cfg)/sizeof(IIR_MONO_CFG_T);i++){
                iir_run_mono_16bits(&msbc_mic_cfg[i], (int16_t *)msbc_pcm_resample_cache, MSBC_ENCODE_PCM_LEN/2);
            }
            buf = (uint8_t *)msbc_pcm_resample_cache;
            len = MSBC_ENCODE_PCM_LEN;
        }
#endif  
#endif  
#if defined (SPEECH_ALGORITHMGAIN)
        iir_run_mono_algorithmgain(SPEECH_ALGORITHMGAIN, (int16_t *)buf, len/2);
#endif
        LOCK_APP_AUDIO_QUEUE();
        store_voicebtpcm_p2m_buffer((unsigned char *)buf, len);
        size = APP_AUDIO_LengthOfCQueue(&voicebtpcm_p2m_queue);
        UNLOCK_APP_AUDIO_QUEUE();
    }
#else
//       etime = hal_sys_timer_get();
//  VOICEBTPCM_TRACE("\n******total Spend:%d Len:%dn", TICKS_TO_MS(etime - stime), len);
#if defined (SPEECH_WEIGHTING_FILTER_SUPPRESS)
#if defined(HFP_1_6_ENABLE)
    if (app_audio_manager_get_scocodecid() != HF_SCO_CODEC_MSBC)
#endif
    {
        for (uint8_t i=0; i<sizeof(mic_cfg)/sizeof(IIR_MONO_CFG_T);i++){
            iir_run_mono_16bits(&mic_cfg[i], (int16_t *)buf, len/2);
        }
    }
#endif
#if defined(HFP_1_6_ENABLE)
#ifndef MSBC_16K_SAMPLE_RATE
    //resample 8k--->16k
    if (app_audio_manager_get_scocodecid() == HF_SCO_CODEC_MSBC){
        for(int index = 0;  index < MSBC_ENCODE_PCM_LEN/4; index ++){
            msbc_pcm_resample_cache[2*index] = ((uint16_t *)buf)[index];
            msbc_pcm_resample_cache[2*index + 1]  = ((uint16_t *)buf)[index];
        }
        
        for (uint8_t i=0; i<sizeof(msbc_mic_cfg)/sizeof(IIR_MONO_CFG_T);i++){
            iir_run_mono_16bits(&msbc_mic_cfg[i], (int16_t *)msbc_pcm_resample_cache, MSBC_ENCODE_PCM_LEN/2);
        }
        buf = (uint8_t *)msbc_pcm_resample_cache;
        len = MSBC_ENCODE_PCM_LEN;
    }
#endif  
#endif  
#if defined (SPEECH_ALGORITHMGAIN)
    iir_run_mono_algorithmgain(SPEECH_ALGORITHMGAIN, (int16_t *)buf, len/2);
#endif
    LOCK_APP_AUDIO_QUEUE();
    store_voicebtpcm_p2m_buffer(buf, len);
    size = APP_AUDIO_LengthOfCQueue(&voicebtpcm_p2m_queue);
    UNLOCK_APP_AUDIO_QUEUE();
#endif

    if (size > (VOICEBTPCM_PCM_TEMP_BUFFER_SIZE)) {
        voicebtpcm_cache_p2m_status = APP_AUDIO_CACHE_OK;
    }

//    preIrqTime = stime;

    return len;
}


//used by playback, play data from memory to speaker
uint32_t voicebtpcm_pcm_audio_more_data(uint8_t *buf, uint32_t len)
{
    uint32_t l = 0;
//    uint32_t stime, etime;
//    static uint32_t preIrqTime = 0;

    if (voicebtpcm_cache_m2p_status == APP_AUDIO_CACHE_CACHEING){
        app_audio_memset_16bit((short *)buf, 0, len/2);
        l = len;
    }else{
#if defined (SPEECH_ECHO_CANCEL)
        {
//            int i;
        short *buf_p=(short *)buf;
//            for(i=0;i<120;i++)
//            {
//                echo_buf[i]=buf_p[i];
//            }
        app_audio_memcpy_16bit((short *)echo_buf, buf_p, SPEEX_BUFF_SIZE);

        }
#endif

/*
         for(int i=0;i<120;i=i+2)
            {
               short *buf_p=(short *)buf;
                TRACE("%5d,%5d,", buf_p[i],buf_p[i+1]);
            hal_sys_timer_delay(2);
            }
*/


// stime = hal_sys_timer_get();
#if defined(HFP_1_6_ENABLE)
    if (app_audio_manager_get_scocodecid() == HF_SCO_CODEC_MSBC)
    {
#ifdef MSBC_16K_SAMPLE_RATE
        l = decode_msbc_frame(buf, len);
#else
        //resample 16k-->8k
        l = decode_msbc_frame((uint8_t *)msbc_pcm_resample_cache_m,  len);
        if(l == len){
            l = decode_msbc_frame((uint8_t *)msbc_pcm_resample_cache_m + len,  len);
             if(l == len){
                for (uint8_t i=0; i<sizeof(msbc_spk_cfg)/sizeof(IIR_MONO_CFG_T);i++){
                    iir_run_mono_16bits(&msbc_spk_cfg[i], (int16_t *)msbc_pcm_resample_cache_m, MSBC_ENCODE_PCM_LEN/2);
                }

               for(int index = 0;  index < MSBC_ENCODE_PCM_LEN/4; index ++){
                    ((uint16_t *) buf)[index] = msbc_pcm_resample_cache_m[2*index];
               }
            }
        }
#endif            
    } 
    else
#endif
    {
#if defined(_CVSD_BYPASS_)
        l = decode_cvsd_frame(buf, len);
#else
        l = decode_voicebtpcm_m2p_frame(buf, len);
#endif
    }
    if (l != len){
        app_audio_memset_16bit((short *)buf, 0, len/2);
        voicebtpcm_cache_m2p_status = APP_AUDIO_CACHE_CACHEING;
    }

#if defined(HFP_1_6_ENABLE)
    if (app_audio_manager_get_scocodecid() != HF_SCO_CODEC_MSBC)
#endif
    {
#if defined (SPEECH_PLC)
//stime = hal_sys_timer_get();
    speech_plc(speech_lc,(short *)buf,len);
//etime = hal_sys_timer_get();
//TRACE( "plc cal ticks:%d", etime-stime);
#endif
    }

#ifdef AUDIO_OUTPUT_LR_BALANCE
    app_audio_lr_balance(buf, len, AUDIO_OUTPUT_LR_BALANCE);
#endif


    //    etime = hal_sys_timer_get();
    //  VOICEBTPCM_TRACE("%s irqDur:%03d Spend:%03d Len:%d ok:%d", __func__, TICKS_TO_MS(stime - preIrqTime), TICKS_TO_MS(etime - stime), len>>1, voicebtpcm_cache_m2p_status);

    //    preIrqTime = stime;
    }
#if defined (SPEAKER_WEIGHTING_FILTER_SUPPRESS)    
#if defined(HFP_1_6_ENABLE)
    if (app_audio_manager_get_scocodecid() != HF_SCO_CODEC_MSBC)
#endif
    {
        for (uint8_t i=0; i<sizeof(spk_cfg)/sizeof(IIR_MONO_CFG_T);i++){
            iir_run_mono_16bits(&spk_cfg[i], (int16_t *)buf, len/2);
        }
    }
#endif

#if defined (SPEAKER_ALGORITHMGAIN)
    iir_run_mono_algorithmgain(SPEAKER_ALGORITHMGAIN, (int16_t *)buf, len/2);
#endif

#if defined ( SPEAKER_NOISE_SUPPRESS )
    speex_preprocess_run(den_voice, (short *)buf);
#endif

    return l;
}

void* speex_get_ext_buff(int size)
{
    uint8_t *pBuff = NULL;
    if (size % 4){
        size = size + (4 - size % 4);
    }
    app_audio_mempool_get_buff(&pBuff, size);
    VOICEBTPCM_TRACE( "speex_get_ext_buff len:%d", size);
    return (void*)pBuff;
}

int voicebtpcm_pcm_audio_init(void)
{
    uint8_t *p2m_buff = NULL;
    uint8_t *m2p_buff = NULL;

#if defined( SPEECH_NOISE_SUPPRESS ) || defined( SPEAKER_NOISE_SUPPRESS )
    float gain, l;
    spx_int32_t l2;
    spx_int32_t max_gain;
#endif

    app_audio_mempool_get_buff(&p2m_buff, VOICEBTPCM_PCM_QUEUE_SIZE);
    app_audio_mempool_get_buff(&m2p_buff, VOICEBTPCM_PCM_QUEUE_SIZE);
#if defined(HFP_1_6_ENABLE)
    const float EQLevel[25] = {
        0.0630957,  0.0794328, 0.1,       0.1258925, 0.1584893,
        0.1995262,  0.2511886, 0.3162278, 0.398107 , 0.5011872,
        0.6309573,  0.794328 , 1,         1.258925 , 1.584893 ,
        1.995262 ,  2.5118864, 3.1622776, 3.9810717, 5.011872 ,
        6.309573 ,  7.943282 , 10       , 12.589254, 15.848932
    };//-12~12
    uint8_t i;

    for (i=0; i<sizeof(msbc_eq_band_gain)/sizeof(float); i++){
        msbc_eq_band_gain[i] = EQLevel[cfg_aud_eq_sbc_band_settings[i]+12];
    }
	
    need_init_decoder = true;
    need_init_encoder = true;
    msbc_counter = 0x08;
#if defined(MSBC_PLC_ENABLE)
    msbc_can_plc = false;
    msbc_recv_seq_no = 0x00;
#endif

    if (app_audio_manager_get_scocodecid() == HF_SCO_CODEC_MSBC) {
        app_audio_mempool_get_buff((uint8_t **)&msbc_encoder, sizeof(SbcEncoder));
#if defined(MSBC_PLC_ENABLE)
        app_audio_mempool_get_buff(&msbc_buf_before_plc, MSBC_ENCODE_PCM_LEN/2);
#endif
        app_audio_mempool_get_buff(&msbc_buf_before_decode, MSBC_FRAME_SIZE);
        app_audio_mempool_get_buff(&temp_msbc_buf, MSBC_ENCODE_PCM_LEN);
        app_audio_mempool_get_buff(&temp_msbc_buf1, MSBC_ENCODE_PCM_LEN);
#ifndef MSBC_16K_SAMPLE_RATE
        app_audio_mempool_get_buff((uint8_t **)&msbc_pcm_resample_cache, MSBC_ENCODE_PCM_LEN);
        app_audio_mempool_get_buff((uint8_t **)&msbc_pcm_resample_cache_m, MSBC_ENCODE_PCM_LEN);   
#endif	        
    }
    
#ifndef MSBC_16K_SAMPLE_RATE
    for (uint8_t i=0; i<sizeof(msbc_mic_cfg)/sizeof(IIR_MONO_CFG_T);i++){
        msbc_mic_cfg[i].history[0] = 0;
        msbc_mic_cfg[i].history[1] = 0;
        msbc_mic_cfg[i].history[2] = 0;
        msbc_mic_cfg[i].history[3] = 0;
    }

    for (uint8_t i=0; i<sizeof(msbc_spk_cfg)/sizeof(IIR_MONO_CFG_T);i++){
        msbc_spk_cfg[i].history[0] = 0;
        msbc_spk_cfg[i].history[1] = 0;
        msbc_spk_cfg[i].history[2] = 0;
        msbc_spk_cfg[i].history[3] = 0;
    }     
#endif    
#endif	

#if defined(_CVSD_BYPASS_)
    Pcm8k_CvsdInit();
#endif

    LOCK_APP_AUDIO_QUEUE();
    /* voicebtpcm_pcm queue*/
    APP_AUDIO_InitCQueue(&voicebtpcm_p2m_queue, VOICEBTPCM_PCM_QUEUE_SIZE, p2m_buff);
    APP_AUDIO_InitCQueue(&voicebtpcm_m2p_queue, VOICEBTPCM_PCM_QUEUE_SIZE, m2p_buff);
    UNLOCK_APP_AUDIO_QUEUE();

    voicebtpcm_cache_m2p_status = APP_AUDIO_CACHE_CACHEING;
    voicebtpcm_cache_p2m_status = APP_AUDIO_CACHE_CACHEING;
#if defined( SPEECH_ECHO_CANCEL ) || defined( SPEECH_NOISE_SUPPRESS ) || defined ( SPEAKER_NOISE_SUPPRESS )
    {
       #define NN VOICEBTPCM_SPEEX_8KHZ_FRAME_LENGTH
       #define TAIL VOICEBTPCM_SPEEX_8KHZ_FRAME_LENGTH
        int sampleRate;
#if defined(HFP_1_6_ENABLE) && defined(MSBC_16K_SAMPLE_RATE)
        if (app_audio_manager_get_scocodecid() == HF_SCO_CODEC_MSBC) {
            sampleRate = 16000;
        }
        else
#endif
        {
            sampleRate = 8000;
        }
       int leak_estimate = 16383;//Better 32767/2^n

#if defined( SPEECH_ECHO_CANCEL )
       e_buf = (short *)speex_get_ext_buff(SPEEX_BUFF_SIZE<<1);
       ref_buf = (short *)speex_get_ext_buff(SPEEX_BUFF_SIZE<<1);
       echo_buf = (short *)speex_get_ext_buff(SPEEX_BUFF_SIZE<<1);
       st = speex_echo_state_init(NN, TAIL, speex_get_ext_buff);
       speex_echo_ctl(st, SPEEX_ECHO_SET_SAMPLING_RATE, &sampleRate);
       speex_echo_ctl(st, SPEEX_ECHO_SET_FIXED_LEAK_ESTIMATE, &leak_estimate);
#endif

#if defined( SPEECH_NOISE_SUPPRESS )
       den = speex_preprocess_state_init(NN, sampleRate, speex_get_ext_buff);
#endif

#if defined ( SPEAKER_NOISE_SUPPRESS )
       den_voice = speex_preprocess_state_init(NN, sampleRate, speex_get_ext_buff);
#endif

#if defined( SPEECH_ECHO_CANCEL ) && defined( SPEECH_NOISE_SUPPRESS )
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_ECHO_STATE, st);
#endif
    }
#endif

#if defined( SPEECH_NOISE_SUPPRESS )
    l= 32000;
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_AGC_LEVEL, &l);

    l2 = 0;
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_AGC, &l2);

    gain = 0;
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_AGC_FIX_GAIN, &gain);

    max_gain = 24;
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &max_gain);
    max_gain /=2;
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_INIT_MAX, &max_gain);
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_VOC_DEC_RST_GAIN, &max_gain);

    l2 = -12;
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &l2);

#endif

#if defined( SPEECH_ECHO_CANCEL )
    l2 = -39;
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &l2);

    l2 = -6;
    speex_preprocess_ctl(den, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE, &l2);
#endif

#if defined ( SPEAKER_NOISE_SUPPRESS )
    l= 32000;
    speex_preprocess_ctl(den_voice, SPEEX_PREPROCESS_SET_AGC_LEVEL, &l);

    l2 = 0;
    speex_preprocess_ctl(den_voice, SPEEX_PREPROCESS_SET_AGC, &l2);

    gain = 0;
    speex_preprocess_ctl(den_voice, SPEEX_PREPROCESS_SET_AGC_FIX_GAIN, &gain);

    max_gain = 20;
    speex_preprocess_ctl(den_voice, SPEEX_PREPROCESS_SET_AGC_MAX_GAIN, &max_gain);

    l2 = -20;
    speex_preprocess_ctl(den_voice, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &l2);

    l = 0.3f;
    speex_preprocess_ctl(den_voice, SPEEX_PREPROCESS_SET_NOISE_PROB, &l);

#endif


#if defined (SPEECH_PLC)
    speech_lc = speech_plc_init(speex_get_ext_buff);
#endif

#ifdef VOICE_DETECT
    {
    	WebRtcVad_AssignSize(&vdt_cnt);
    	g_vadmic = (VadInst*)speex_get_ext_buff(vdt_cnt + 4);
    	WebRtcVad_Init((VadInst*)g_vadmic);
    	WebRtcVad_set_mode((VadInst*)g_vadmic, 2);
    	//one channel 320*2
    	vad_buff = (uint16_t *)speex_get_ext_buff(VAD_SIZE*2);
    	vad_buf_size = 0;
    	vdt_cnt = 0;
    }    
#endif	

#if defined (SPEECH_WEIGHTING_FILTER_SUPPRESS)
    for (uint8_t i=0; i<sizeof(mic_cfg)/sizeof(IIR_MONO_CFG_T);i++){
        mic_cfg[i].history[0] = 0;
        mic_cfg[i].history[1] = 0;
        mic_cfg[i].history[2] = 0;
        mic_cfg[i].history[3] = 0;
    }
#endif

#if defined (SPEAKER_WEIGHTING_FILTER_SUPPRESS)    
    for (uint8_t i=0; i<sizeof(spk_cfg)/sizeof(IIR_MONO_CFG_T);i++){
        spk_cfg[i].history[0] = 0;
        spk_cfg[i].history[1] = 0;
        spk_cfg[i].history[2] = 0;
        spk_cfg[i].history[3] = 0;
    }
#endif

    return 0;
}

