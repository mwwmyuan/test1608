#include "hal_sysfreq.h"
#include "hal_overlay.h"
#include "hal_cache.h"
#include "cmsis.h"

extern uint32_t __overlay_text_start__[];
extern uint32_t __overlay_text_exec_start__[];
extern uint32_t __load_start_overlay_text0[];
extern uint32_t __load_stop_overlay_text0[];
extern uint32_t __load_start_overlay_text1[];
extern uint32_t __load_stop_overlay_text1[];
extern uint32_t __load_start_overlay_text2[];
extern uint32_t __load_stop_overlay_text2[];
extern uint32_t __load_start_overlay_text3[];
extern uint32_t __load_stop_overlay_text3[];
extern uint32_t __load_start_overlay_text4[];
extern uint32_t __load_stop_overlay_text4[];
extern uint32_t __load_start_overlay_text5[];
extern uint32_t __load_stop_overlay_text5[];
extern uint32_t __load_start_overlay_text6[];
extern uint32_t __load_stop_overlay_text6[];
extern uint32_t __load_start_overlay_text7[];
extern uint32_t __load_stop_overlay_text7[];

extern uint32_t __overlay_data_start__[];
extern uint32_t __load_start_overlay_data0[];
extern uint32_t __load_stop_overlay_data0[];
extern uint32_t __load_start_overlay_data1[];
extern uint32_t __load_stop_overlay_data1[];
extern uint32_t __load_start_overlay_data2[];
extern uint32_t __load_stop_overlay_data2[];
extern uint32_t __load_start_overlay_data3[];
extern uint32_t __load_stop_overlay_data3[];
extern uint32_t __load_start_overlay_data4[];
extern uint32_t __load_stop_overlay_data4[];
extern uint32_t __load_start_overlay_data5[];
extern uint32_t __load_stop_overlay_data5[];
extern uint32_t __load_start_overlay_data6[];
extern uint32_t __load_stop_overlay_data6[];
extern uint32_t __load_start_overlay_data7[];
extern uint32_t __load_stop_overlay_data7[];

#ifdef BSS_OVERLAY
extern uint32_t __overlay_bss_start__[];
extern uint32_t __dummy_overlay_bss_end__[];
#endif
#ifdef _OVERLAY_MEM_AUDIO_SHARE__
extern uint32_t __audio_share_text_start[];
extern uint32_t __audio_share_text_start_flash[];
extern uint32_t __audio_share_text_end_flash[];
extern uint32_t __audio_share_data_start[];
extern uint32_t __audio_share_data_start_flash[];
extern uint32_t __audio_share_data_end_flash[];
extern uint32_t __audio_share_bss_start[];
extern uint32_t __audio_share_bss_stop[];
#endif
static enum HAL_OVERLAY_ID_T cur_overlay_id = HAL_OVERLAY_ID_QTY;

static uint32_t * const text_load_start[HAL_OVERLAY_ID_QTY] = {
    __load_start_overlay_text0,
    __load_start_overlay_text1,
    __load_start_overlay_text2,
    __load_start_overlay_text3,
    __load_start_overlay_text4,
    __load_start_overlay_text5,
    __load_start_overlay_text6,
    __load_start_overlay_text7,
};

static uint32_t * const text_load_stop[HAL_OVERLAY_ID_QTY] = {
    __load_stop_overlay_text0,
    __load_stop_overlay_text1,
    __load_stop_overlay_text2,
    __load_stop_overlay_text3,
    __load_stop_overlay_text4,
    __load_stop_overlay_text5,
    __load_stop_overlay_text6,
    __load_stop_overlay_text7,
};

static uint32_t * const data_load_start[HAL_OVERLAY_ID_QTY] = {
    __load_start_overlay_data0,
    __load_start_overlay_data1,
    __load_start_overlay_data2,
    __load_start_overlay_data3,
    __load_start_overlay_data4,
    __load_start_overlay_data5,
    __load_start_overlay_data6,
    __load_start_overlay_data7,
};

static uint32_t * const data_load_stop[HAL_OVERLAY_ID_QTY] = {
    __load_stop_overlay_data0,
    __load_stop_overlay_data1,
    __load_stop_overlay_data2,
    __load_stop_overlay_data3,
    __load_stop_overlay_data4,
    __load_stop_overlay_data5,
    __load_stop_overlay_data6,
    __load_stop_overlay_data7,
};

enum HAL_OVERLAY_RET_T hal_overlay_load(enum HAL_OVERLAY_ID_T id)
{
    enum HAL_OVERLAY_RET_T ret;
    uint32_t lock;
    uint32_t *dst, *src;

    if (id >= HAL_OVERLAY_ID_QTY) {
        return HAL_OVERLAY_RET_BAD_ID;
    }

    ret = HAL_OVERLAY_RET_OK;

    lock = int_lock();
    hal_sysfreq_req(HAL_SYSFREQ_USER_OVERLAY, HAL_CMU_FREQ_52M);
    if (cur_overlay_id == HAL_OVERLAY_ID_QTY) {
        cur_overlay_id = HAL_OVERLAY_ID_IN_CFG;
    } else if (cur_overlay_id == HAL_OVERLAY_ID_IN_CFG) {
        ret = HAL_OVERLAY_RET_IN_CFG;
    } else {
        ret = HAL_OVERLAY_RET_IN_USE;
    }

    if (ret) {
        goto exit;
    }

    for (dst = __overlay_text_start__, src = text_load_start[id];
            src < text_load_stop[id];
            dst++, src++) {
        *dst = *src;
    }

    for (dst = __overlay_data_start__, src = data_load_start[id];
            src < data_load_stop[id];
            dst++, src++) {
        *dst = *src;
    }
#ifdef BSS_OVERLAY
    for(dst= __overlay_bss_start__;  dst < __dummy_overlay_bss_end__;  dst ++){
        *dst = 0;
    }
#endif

#ifdef _OVERLAY_MEM_AUDIO_SHARE__
    if(id != HAL_OVERLAY_ID_0){
        for (dst = __audio_share_text_start, src = __audio_share_text_start_flash;
                src < __audio_share_text_end_flash;
                dst++, src++) {
            *dst = *src;
        }

        for (dst = __audio_share_data_start, src = __audio_share_data_start_flash;
                src < __audio_share_data_end_flash;
                dst++, src++) {
            *dst = *src;
        }
        for(dst= __audio_share_bss_start;  dst < __audio_share_bss_stop;  dst ++){
            *dst = 0;
        }
        
        hal_cache_invalidate(HAL_CACHE_ID_I_CACHE,
            (uint32_t)__audio_share_text_start,
            __audio_share_text_end_flash - __audio_share_text_start_flash);

        hal_cache_invalidate(HAL_CACHE_ID_D_CACHE,
            (uint32_t)__audio_share_data_start,
            __audio_share_data_end_flash - __audio_share_data_start_flash);
        
        hal_cache_invalidate(HAL_CACHE_ID_D_CACHE,
            (uint32_t)__audio_share_bss_start,
            __audio_share_bss_stop - __audio_share_bss_start);        
    }
#endif
    hal_cache_invalidate(HAL_CACHE_ID_I_CACHE,
        (uint32_t)__overlay_text_exec_start__,
        text_load_stop[id] - text_load_start[id]);

    hal_cache_invalidate(HAL_CACHE_ID_D_CACHE,
        (uint32_t)__overlay_data_start__,
        data_load_stop[id] - data_load_start[id]);
#ifdef BSS_OVERLAY
    hal_cache_invalidate(HAL_CACHE_ID_D_CACHE,
        (uint32_t)__overlay_bss_start__,
        __dummy_overlay_bss_end__ - __overlay_bss_start__);
#endif    
    cur_overlay_id = id;
    
exit:
    hal_sysfreq_req(HAL_SYSFREQ_USER_OVERLAY, HAL_CMU_FREQ_32K);
    int_unlock(lock);
    return HAL_OVERLAY_RET_OK;
}

enum HAL_OVERLAY_RET_T hal_overlay_unload(enum HAL_OVERLAY_ID_T id)
{
    enum HAL_OVERLAY_RET_T ret;
    uint32_t lock;

    if (id >= HAL_OVERLAY_ID_QTY) {
        return HAL_OVERLAY_RET_BAD_ID;
    }

    ret = HAL_OVERLAY_RET_OK;

    lock = int_lock();
    if (cur_overlay_id == id) {
        cur_overlay_id = HAL_OVERLAY_ID_QTY;
    } else if (cur_overlay_id == HAL_OVERLAY_ID_IN_CFG) {
        ret = HAL_OVERLAY_RET_IN_CFG;
    } else {
        ret = HAL_OVERLAY_RET_IN_USE;
    }
    int_unlock(lock);

    return ret;
}

