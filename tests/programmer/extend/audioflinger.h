#ifndef AUDIO_FLINGER_H
#define AUDIO_FLINGER_H

#include "plat_types.h"
#include "stdbool.h"
#include "hal_aud.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*AF_STREAM_HANDLER_T)(uint8_t *buf, uint32_t len);

//pingpong machine
enum AF_PP_T{
    PP_PING = 0,
    PP_PANG = 1
};

struct AF_STREAM_CONFIG_T {
    enum AUD_BITS_T bits;
    enum AUD_SAMPRATE_T sample_rate;
    enum AUD_CHANNEL_NUM_T channel_num;

    enum AUD_STREAM_USE_DEVICE_T device;

    enum AUD_IO_PATH_T io_path;

    AF_STREAM_HANDLER_T handler;

    uint8_t *data_ptr;
    uint32_t data_size;

    //should define type
    uint8_t vol;
};

//Should define return status
uint32_t af_open(void);
uint32_t af_stream_open(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream, struct AF_STREAM_CONFIG_T *cfg);
uint32_t af_stream_setup(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream, struct AF_STREAM_CONFIG_T *cfg);
uint32_t af_stream_get_cfg(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream, struct AF_STREAM_CONFIG_T **cfg, bool needlock);
uint32_t af_stream_start(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream);
uint32_t af_stream_stop(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream);
uint32_t af_stream_close(enum AUD_STREAM_ID_T id, enum AUD_STREAM_T stream);
uint32_t af_close(void);
int af_audio_mempool_init(uint8_t *buff, uint16_t len);
uint32_t af_audio_mempool_free_buff_size(void);
int af_audio_mempool_get_buff(uint8_t **buff, uint32_t size);
int af_audio_pcmbuff_init(uint8_t *buff, uint16_t len);
int af_audio_pcmbuff_length(void);
int af_audio_pcmbuff_put(uint8_t *buff, uint16_t len);
int af_audio_pcmbuff_get(uint8_t *buff, uint16_t len);
int af_audio_pcmbuff_discard(uint16_t len);
void af_copy_track_one_to_two_16bits(int16_t *dst_buf, int16_t *src_buf, uint32_t src_len);


#ifdef __cplusplus
}
#endif

#endif /* AUDIO_FLINGER_H */
