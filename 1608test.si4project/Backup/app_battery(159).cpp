#include "cmsis_os.h"
#include "cmsis_nvic.h"
#include "tgt_hardware.h"
#include "pmu.h"
#include "hal_timer.h"
#include "hal_gpadc.h"
#include "hal_trace.h"
#include "hal_gpio.h"
#include "hal_iomux.h"
#include "hal_chipid.h"
#include "app_thread.h"
#include "app_battery.h"
#include "apps.h"
#include "app_status_ind.h"
#include "tgt_hardware.h"
#include "nvrecord_env.h"

#include "app_bt_stream.h"
#include "app_audio.h"







#define APP_BATTERY_TRACE(s,...)
// TRACE(s, ##__VA_ARGS__)

#ifndef APP_BATTERY_MIN_MV
#define APP_BATTERY_MIN_MV (3200)
#endif

#ifndef APP_BATTERY_MAX_MV
#define APP_BATTERY_MAX_MV (4200)
#endif

#ifndef APP_BATTERY_PD_MV
#define APP_BATTERY_PD_MV   (3100)
#endif

#ifndef APP_BATTERY_CHARGE_TIMEOUT_MIN
#define APP_BATTERY_CHARGE_TIMEOUT_MIN (90)
#endif

#ifndef APP_BATTERY_CHARGE_OFFSET_MV
#define APP_BATTERY_CHARGE_OFFSET_MV (20)
#endif

#define APP_BATTERY_CHARGING_PLUGOUT_DEDOUNCE_CNT (APP_BATTERY_CHARGING_PERIODIC_MS<500?3:1)

#define APP_BATTERY_CHARGING_EXTPIN_MEASURE_CNT (APP_BATTERY_CHARGING_PERIODIC_MS<2*1000?2*1000/APP_BATTERY_CHARGING_PERIODIC_MS:1)
#define APP_BATTERY_CHARGING_EXTPIN_DEDOUNCE_CNT (6)

#define APP_BATTERY_CHARGING_OVERVOLT_MEASURE_CNT (APP_BATTERY_CHARGING_PERIODIC_MS<2*1000?2*1000/APP_BATTERY_CHARGING_PERIODIC_MS:1)
#define APP_BATTERY_CHARGING_OVERVOLT_DEDOUNCE_CNT (3)

#define APP_BATTERY_CHARGING_SLOPE_MEASURE_CNT (APP_BATTERY_CHARGING_PERIODIC_MS<20*1000?20*1000/APP_BATTERY_CHARGING_PERIODIC_MS:1)
#define APP_BATTERY_CHARGING_SLOPE_TABLE_COUNT (6)


#define APP_BATTERY_REPORT_INTERVAL (5)

#define APP_BATTERY_LEVEL_MIN (0)
#define APP_BATTERY_LEVEL_MAX (9)
#define APP_BATTERY_LEVEL_NUM (APP_BATTERY_LEVEL_MAX-APP_BATTERY_LEVEL_MIN+1)

#define APP_BATTERY_MV_BASE ((APP_BATTERY_MAX_MV-APP_BATTERY_PD_MV)/(APP_BATTERY_LEVEL_NUM))

#define APP_BATTERY_STABLE_COUNT (5)
#define APP_BATTERY_MEASURE_PERIODIC_FAST_MS (200)
#define APP_BATTERY_MEASURE_PERIODIC_NORMAL_MS (10000)
#define APP_BATTERY_CHARGING_PERIODIC_MS (200)
#define APP_LINEOUT_PERIODIC_MS (1200)


enum APP_BATTERY_MEASURE_PERIODIC_T {
    APP_BATTERY_MEASURE_PERIODIC_FAST = 0,
    APP_BATTERY_MEASURE_PERIODIC_NORMAL,

    APP_BATTERY_MEASURE_PERIODIC_QTY,
};

enum APP_BATTERY_CHARGER_T {
    APP_BATTERY_CHARGER_PLUGOUT = 0,
    APP_BATTERY_CHARGER_PLUGIN,

    APP_BATTERY_CHARGER_QTY,
};

struct APP_BATTERY_MEASURE_CHARGER_STATUS_T {
	HAL_GPADC_MV_T prevolt;
	int32_t slope_1000[APP_BATTERY_CHARGING_SLOPE_TABLE_COUNT];
	int slope_1000_index;	
	int cnt;	
};

enum BT_DEVICE_SRC_SNK_T{
	BT_DEVICE_SNK=0,
	BT_DEVICE_SRC
};


struct APP_BATTERY_MEASURE_T {
	uint32_t start_tim;
    enum APP_BATTERY_STATUS_T status;
    uint8_t currlevel;
    APP_BATTERY_MV_T currvolt;
    APP_BATTERY_MV_T lowvolt;
    APP_BATTERY_MV_T highvolt;
    APP_BATTERY_MV_T pdvolt;
	uint32_t chargetimeout;
    enum APP_BATTERY_MEASURE_PERIODIC_T periodic;
    HAL_GPADC_MV_T voltage[APP_BATTERY_STABLE_COUNT];
    uint16_t index;
	struct APP_BATTERY_MEASURE_CHARGER_STATUS_T	charger_status;
    APP_BATTERY_EVENT_CB_T cb;
};



static void app_battery_timehandler(void const *param);
static void app_lineout_timehandler(void const *param);

osTimerDef (APP_BATTERY, app_battery_timehandler);
osTimerDef(APP_LINEOUT,app_lineout_timehandler);

#if defined(APP_LINEIN_A2DP_SOURCE)||defined(APP_I2S_A2DP_SOURCE)
		struct nvrecord_env_t *nvrecord_env=NULL;
#endif


static osTimerId app_batter_timer = NULL;
static struct APP_BATTERY_MEASURE_T app_battery_measure;
osTimerId app_lineout_timer = NULL;

extern struct BT_DEVICE_T  app_bt_device;


uint8_t lineout_index = 0;


static int app_battery_charger_handle_process(void);

void app_battery_irqhandler(void * ptr)
{
    uint8_t i;
    uint32_t meanBattVolt = 0;
    HAL_GPADC_MV_T vbat =  *(HAL_GPADC_MV_T *)ptr;
    APP_BATTERY_TRACE("%s %d",__func__, vbat);
    if (vbat == HAL_GPADC_BAD_VALUE){
        app_battery_measure.cb(APP_BATTERY_STATUS_INVALID, vbat);
        return;
    }
    
    app_battery_measure.voltage[app_battery_measure.index++%APP_BATTERY_STABLE_COUNT] = vbat<<2;
    
    if (app_battery_measure.index > APP_BATTERY_STABLE_COUNT){
        for (i=0;i<APP_BATTERY_STABLE_COUNT;i++){
            meanBattVolt += app_battery_measure.voltage[i];
        }
        meanBattVolt /= APP_BATTERY_STABLE_COUNT;
        if (app_battery_measure.cb){
            if (meanBattVolt>app_battery_measure.highvolt){
                app_battery_measure.cb(APP_BATTERY_STATUS_OVERVOLT, meanBattVolt);
            }else if((meanBattVolt>app_battery_measure.pdvolt) && (meanBattVolt<app_battery_measure.lowvolt)){
                app_battery_measure.cb(APP_BATTERY_STATUS_UNDERVOLT, meanBattVolt);
            }else if(meanBattVolt<app_battery_measure.pdvolt){
                app_battery_measure.cb(APP_BATTERY_STATUS_PDVOLT, meanBattVolt);
            }else{
                app_battery_measure.cb(APP_BATTERY_STATUS_NORMAL, meanBattVolt);
            }
        }
    }
}

static void app_battery_timehandler(void const *param)
{
    hal_gpadc_open(HAL_GPADC_CHAN_BATTERY, HAL_GPADC_ATP_ONESHOT, app_battery_irqhandler);
}


static void app_lineout_timehandler(void const *param)
{
	if (app_line_out_detect_cfg.pin != HAL_IOMUX_PIN_NUM){
		if (!hal_gpio_pin_get_val((enum HAL_GPIO_PIN_T)app_line_out_detect_cfg.pin)){
			TRACE("EXT PIN-->LINEOUT");
			lineout_index = 1;
			TRACE("line_out_flag = %d",lineout_index);
			            
#if defined(APP_LINEIN_A2DP_SOURCE)||defined(APP_I2S_A2DP_SOURCE)
		
			//debug switch src mode 
			nv_record_env_get(&nvrecord_env);
			if(app_bt_device.src_or_snk==BT_DEVICE_SNK)
			{
				nvrecord_env->src_snk_flag.src_snk_mode =BT_DEVICE_SRC;
				TRACE("!!!!!!!!!!!!SINK_MODE!!!!!!!!!!!");
				
			
			
			nv_record_env_set(nvrecord_env);
			app_reset();
				}
		}
#endif
		}
			
			
			
		
	       else{
		        TRACE("EXT PIN-->LINEOUT");
		        lineout_index = 0;
	            nv_record_env_get(&nvrecord_env);
			    if(app_bt_device.src_or_snk==BT_DEVICE_SRC)
			     {
				      nvrecord_env->src_snk_flag.src_snk_mode =BT_DEVICE_SNK;
				      TRACE("!!!!!!!!!!!!SINK_MODE!!!!!!!!!!!");
				
			
			
			          nv_record_env_set(nvrecord_env);
			          app_reset();
				}
		
		}
		


}


static void app_battery_event_process(enum APP_BATTERY_STATUS_T status, APP_BATTERY_MV_T volt)
{
    uint32_t app_battevt;
    APP_MESSAGE_BLOCK msg;
    
    APP_BATTERY_TRACE("%s %d,%d",__func__, status, volt);

    msg.mod_id = APP_MODUAL_BATTERY;
    APP_BATTERY_SET_MESSAGE(app_battevt, status, volt);
    msg.msg_body.message_id = app_battevt;
    msg.msg_body.message_ptr = (uint32_t)NULL;
    app_mailbox_put(&msg);
}

static enum APP_BATTERY_CHARGER_T app_battery_charger_forcegetstatus(void);
static int app_battery_handle_process(APP_MESSAGE_BODY *msg_body)
{
    int8_t level = 0;
    uint8_t status;
    APP_BATTERY_MV_T volt;
    static uint8_t charging_plugout_cnt = 0;

    if ((app_battery_measure.status == APP_BATTERY_STATUS_CHARGING)){
        app_battery_measure.periodic = APP_BATTERY_MEASURE_PERIODIC_NORMAL;
        osTimerStop(app_batter_timer);
        osTimerStart(app_batter_timer, APP_BATTERY_CHARGING_PERIODIC_MS);
    }else if (app_battery_measure.periodic == APP_BATTERY_MEASURE_PERIODIC_FAST){
        app_battery_measure.periodic = APP_BATTERY_MEASURE_PERIODIC_NORMAL;
        osTimerStop(app_batter_timer);
        osTimerStart(app_batter_timer, APP_BATTERY_MEASURE_PERIODIC_NORMAL_MS);
    }
    APP_BATTERY_GET_STATUS(msg_body->message_id, status);
    APP_BATTERY_GET_VOLT(msg_body->message_id, volt);






	
    TRACE("%s %d,%d################",__func__, status, volt);
    if (app_battery_measure.status == APP_BATTERY_STATUS_NORMAL){
        switch (status) {
			case APP_BATTERY_STATUS_UNDERVOLT:
				TRACE("UNDERVOLT:%d", volt);
				app_status_indication_set(APP_STATUS_INDICATION_CHARGENEED);
#ifdef MEDIA_PLAYER_SUPPORT
				app_voice_report(APP_STATUS_INDICATION_CHARGENEED, 0);			
#endif
            case APP_BATTERY_STATUS_NORMAL:
            case APP_BATTERY_STATUS_OVERVOLT:
                app_battery_measure.currvolt = volt;
                level = (volt-APP_BATTERY_PD_MV)/APP_BATTERY_MV_BASE;

                if (level<APP_BATTERY_LEVEL_MIN)
                    level = APP_BATTERY_LEVEL_MIN;
                if (level>APP_BATTERY_LEVEL_MAX)
                    level = APP_BATTERY_LEVEL_MAX;
                
                app_battery_measure.currlevel = level;

                app_status_battery_report(app_battery_measure.currlevel);
				break;
            case APP_BATTERY_STATUS_PDVOLT:
				TRACE("PDVOLT-->POWEROFF:%d", volt);
                osTimerStop(app_batter_timer);
                app_shutdown();  
                break;
            case APP_BATTERY_STATUS_CHARGING:
				TRACE("CHARGING-->APP_BATTERY_CHARGER :%d", volt);
				if (volt == APP_BATTERY_CHARGER_PLUGIN){
					TRACE("CHARGING-->POWEROFF:%d", volt);
	                osTimerStop(app_batter_timer);
	                app_reset();
				}
                break;         
            case APP_BATTERY_STATUS_INVALID:
            default:
                break;
        }
    }else if (app_battery_measure.status == APP_BATTERY_STATUS_CHARGING){
		enum APP_BATTERY_CHARGER_T status_charger = app_battery_charger_forcegetstatus();
		if (status_charger == APP_BATTERY_CHARGER_PLUGOUT){
			if(hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_2){
				TRACE("CHARGING-->RESET");
				osTimerStop(app_batter_timer);
				app_shutdown();
				goto exit;
			}else if (++charging_plugout_cnt>=APP_BATTERY_CHARGING_PLUGOUT_DEDOUNCE_CNT){
				TRACE("CHARGING-->RESET");
				osTimerStop(app_batter_timer);
				app_shutdown();				
				goto exit;
			}
		}else{
			charging_plugout_cnt = 0;
		}

        switch (status) {
            case APP_BATTERY_STATUS_OVERVOLT:
            case APP_BATTERY_STATUS_NORMAL:
            case APP_BATTERY_STATUS_UNDERVOLT:
                app_battery_measure.currvolt = volt;
                break;
            case APP_BATTERY_STATUS_CHARGING:               
                TRACE("CHARGING:%d", volt);
                if (volt == APP_BATTERY_CHARGER_PLUGOUT){
                    TRACE("CHARGING-->RESET");
                    osTimerStop(app_batter_timer);
                    app_shutdown();
					goto exit;
                }
                break;        
            case APP_BATTERY_STATUS_INVALID:
            default:
                break;
        }
	
		if (app_battery_charger_handle_process()<=0){
            if (app_status_indication_get() != APP_STATUS_INDICATION_FULLCHARGE){                    
                TRACE("FULL_CHARGING:%d", app_battery_measure.currvolt);
                app_status_indication_set(APP_STATUS_INDICATION_FULLCHARGE);
#ifdef MEDIA_PLAYER_SUPPORT
                app_voice_report(APP_STATUS_INDICATION_FULLCHARGE, 0);
#endif
            }
		}
    }
	
exit:
    return 0;
}

int app_battery_open(void)
{
    APP_BATTERY_TRACE("%s batt range:%d~%d",__func__, APP_BATTERY_MIN_MV, APP_BATTERY_MAX_MV);

    if (app_batter_timer == NULL)
        app_batter_timer = osTimerCreate (osTimer(APP_BATTERY), osTimerPeriodic, NULL);

    app_battery_measure.status = APP_BATTERY_STATUS_NORMAL;
    app_battery_measure.currlevel = APP_BATTERY_LEVEL_MAX;
    app_battery_measure.currvolt = APP_BATTERY_MAX_MV;
    app_battery_measure.lowvolt = APP_BATTERY_MIN_MV;
    app_battery_measure.highvolt = APP_BATTERY_MAX_MV;
    app_battery_measure.pdvolt = APP_BATTERY_PD_MV;
    app_battery_measure.chargetimeout = APP_BATTERY_CHARGE_TIMEOUT_MIN;
	
    app_battery_measure.periodic = APP_BATTERY_MEASURE_PERIODIC_QTY;
    app_battery_measure.cb = app_battery_event_process;

	app_battery_measure.charger_status.prevolt = 0;	
	app_battery_measure.charger_status.slope_1000_index = 0;	
	app_battery_measure.charger_status.cnt = 0;

	app_set_threadhandle(APP_MODUAL_BATTERY, app_battery_handle_process);





    if (app_battery_ext_charger_detecter_cfg.pin != HAL_IOMUX_PIN_NUM){
        hal_iomux_init((struct HAL_IOMUX_PIN_FUNCTION_MAP *)&app_battery_ext_charger_detecter_cfg, 1);
        hal_gpio_pin_set_dir((enum HAL_GPIO_PIN_T)app_battery_ext_charger_detecter_cfg.pin, HAL_GPIO_DIR_IN, 1);
    }
    
    if (app_battery_charger_indication_open() == APP_BATTERY_CHARGER_PLUGIN){
    	app_battery_measure.status = APP_BATTERY_STATUS_CHARGING;
    	pmu_charger_plugin_config();
    	if (app_battery_ext_charger_indicator_cfg.pin != HAL_IOMUX_PIN_NUM){
    		hal_gpio_pin_set_dir((enum HAL_GPIO_PIN_T)app_battery_ext_charger_indicator_cfg.pin, HAL_GPIO_DIR_IN, 1);
    	}
    	return 1;
    }else{
    	if (app_battery_ext_charger_indicator_cfg.pin != HAL_IOMUX_PIN_NUM){
    		hal_gpio_pin_set_dir((enum HAL_GPIO_PIN_T)app_battery_ext_charger_indicator_cfg.pin, HAL_GPIO_DIR_OUT, 0);
            }
    	return 0;
    }
}

int app_battery_start(void)
{
    APP_BATTERY_TRACE("%s %d",__func__, APP_BATTERY_MEASURE_PERIODIC_FAST_MS);
	
	app_battery_measure.start_tim = hal_sys_timer_get();
    app_battery_measure.periodic = APP_BATTERY_MEASURE_PERIODIC_FAST;
    osTimerStart(app_batter_timer, APP_BATTERY_MEASURE_PERIODIC_FAST_MS);
    
    return 0;
}

int app_battery_stop(void)
{
    osTimerStop(app_batter_timer);
    
    return 0;
}

int app_battery_close(void)
{
    hal_gpadc_close(HAL_GPADC_CHAN_BATTERY);
    
    return 0;
}


static int32_t app_battery_charger_slope_calc(int32_t t1, int32_t v1, int32_t t2, int32_t v2)
{
	int32_t slope_1000;
	slope_1000 = (v2-v1)*1000/(t2-t1);
	return slope_1000;
}

static int app_battery_charger_handle_process(void)
{
	int nRet = 1;
    int8_t i=0,cnt=0;
	uint32_t slope_1000 = 0;
    uint32_t charging_min;
    static uint8_t overvolt_full_charge_cnt = 0;
    static uint8_t ext_pin_full_charge_cnt = 0;

	charging_min = hal_sys_timer_get() - app_battery_measure.start_tim;
	charging_min = TICKS_TO_MS64(charging_min)/1000/60;
	if (charging_min >= app_battery_measure.chargetimeout){
		TRACE("TIMEROUT-->FULL_CHARGING");		
		nRet = -1;
		goto exit;
	}

	if ((app_battery_measure.charger_status.cnt++%APP_BATTERY_CHARGING_OVERVOLT_MEASURE_CNT) == 0){
		if (app_battery_measure.currvolt>=(app_battery_measure.highvolt+APP_BATTERY_CHARGE_OFFSET_MV)){
			overvolt_full_charge_cnt++;
		}else{
			overvolt_full_charge_cnt = 0;
		}
		if (overvolt_full_charge_cnt>=APP_BATTERY_CHARGING_OVERVOLT_DEDOUNCE_CNT){			
			TRACE("OVERVOLT-->FULL_CHARGING");		
			nRet = -1;
			goto exit;
		}
	}
	
	if ((app_battery_measure.charger_status.cnt++%APP_BATTERY_CHARGING_EXTPIN_MEASURE_CNT) == 0){
		if (app_battery_ext_charger_detecter_cfg.pin != HAL_IOMUX_PIN_NUM){
			if (hal_gpio_pin_get_val((enum HAL_GPIO_PIN_T)app_battery_ext_charger_detecter_cfg.pin)){
				ext_pin_full_charge_cnt++;
			}else{
				ext_pin_full_charge_cnt = 0;
			}
			if (ext_pin_full_charge_cnt>=APP_BATTERY_CHARGING_EXTPIN_DEDOUNCE_CNT){			
				TRACE("EXT PIN-->FULL_CHARGING");
				nRet = -1;
				goto exit;
			}
		}
	}
	
	if ((app_battery_measure.charger_status.cnt++%APP_BATTERY_CHARGING_SLOPE_MEASURE_CNT) == 0){
		if (!app_battery_measure.charger_status.prevolt){			
			app_battery_measure.charger_status.slope_1000[app_battery_measure.charger_status.slope_1000_index%APP_BATTERY_CHARGING_SLOPE_TABLE_COUNT] = slope_1000;
			app_battery_measure.charger_status.prevolt = app_battery_measure.currvolt;
			for (i=0;i<APP_BATTERY_CHARGING_SLOPE_TABLE_COUNT;i++){
				app_battery_measure.charger_status.slope_1000[i]=100;
			}
		}else{
			slope_1000 = app_battery_charger_slope_calc(0, app_battery_measure.charger_status.prevolt,
														APP_BATTERY_CHARGING_PERIODIC_MS*APP_BATTERY_CHARGING_SLOPE_MEASURE_CNT/1000, app_battery_measure.currvolt);			
			app_battery_measure.charger_status.slope_1000[app_battery_measure.charger_status.slope_1000_index%APP_BATTERY_CHARGING_SLOPE_TABLE_COUNT] = slope_1000;
			app_battery_measure.charger_status.prevolt = app_battery_measure.currvolt;
			for (i=0;i<APP_BATTERY_CHARGING_SLOPE_TABLE_COUNT;i++){
				if (app_battery_measure.charger_status.slope_1000[i]>0)
					cnt++;
				else
					cnt--;
				TRACE("slope_1000[%d]=%d cnt:%d", i,app_battery_measure.charger_status.slope_1000[i], cnt);
			}
			TRACE("app_battery_charger_slope_proc slope*1000=%d cnt:%d nRet:%d", slope_1000, cnt, nRet);			
			if (cnt>1){
				nRet = 1;
			}/*else (3>=cnt && cnt>=-3){
				nRet = 0;
			}*/else{			
				if (app_battery_measure.currvolt>=(app_battery_measure.highvolt-APP_BATTERY_CHARGE_OFFSET_MV)){
					TRACE("SLOPE-->FULL_CHARGING");
					nRet = -1;
				}
			}
		}
		app_battery_measure.charger_status.slope_1000_index++;
	}
exit:	
	return nRet;
}

static enum APP_BATTERY_CHARGER_T app_battery_charger_forcegetstatus(void)
{
    enum APP_BATTERY_CHARGER_T status = APP_BATTERY_CHARGER_QTY;
    enum PMU_CHARGER_STATUS_T charger;

    charger = pmu_charger_get_status();

    if (charger == PMU_CHARGER_PLUGIN) {
        status = APP_BATTERY_CHARGER_PLUGIN;
        TRACE("force APP_BATTERY_CHARGER_PLUGIN");
    } else{
        status = APP_BATTERY_CHARGER_PLUGOUT;
        TRACE("force APP_BATTERY_CHARGER_PLUGOUT");
    }

    return status;
}

static void app_battery_charger_handler(void)
{
    enum PMU_CHARGER_STATUS_T status;
    TRACE("%s",__func__);

    status = pmu_charger_detect();
    if(hal_get_chip_metal_id() >= HAL_CHIP_METAL_ID_2)
    {
        status = pmu_charger_get_status();
    }
    if (status != PMU_CHARGER_UNKNOWN){
        app_battery_event_process(APP_BATTERY_STATUS_CHARGING,
            (status == PMU_CHARGER_PLUGIN) ? APP_BATTERY_CHARGER_PLUGIN : APP_BATTERY_CHARGER_PLUGOUT);
    }
}

int app_battery_charger_indication_open(void)
{
    enum APP_BATTERY_CHARGER_T status = APP_BATTERY_CHARGER_QTY;
    uint8_t cnt = 0;

    APP_BATTERY_TRACE("%s",__func__);

    pmu_charger_init();

    do{
        status = app_battery_charger_forcegetstatus();
        if (status == APP_BATTERY_CHARGER_PLUGIN)
            break;
        osDelay(20);
    }while(cnt++<10);

    if (app_battery_ext_charger_detecter_cfg.pin != HAL_IOMUX_PIN_NUM){
        if (!hal_gpio_pin_get_val((enum HAL_GPIO_PIN_T)app_battery_ext_charger_detecter_cfg.pin)){
            status = APP_BATTERY_CHARGER_PLUGIN;
        }
    }

    if (app_line_out_detect_cfg.pin != HAL_IOMUX_PIN_NUM){
		if (!hal_gpio_pin_get_val((enum HAL_GPIO_PIN_T)app_line_out_detect_cfg.pin)){
			TRACE("EXT PIN-->LINEOUT");
			lineout_index = 1;
			osTimerStart(app_lineout_timer,APP_LINEOUT_PERIODIC_MS);
		}else{
			TRACE("EXT PIN-->LINEOUT");
			lineout_index = 0;
			osTimerStart(app_lineout_timer,APP_LINEOUT_PERIODIC_MS);
		}
	}

	
    if (status == APP_BATTERY_CHARGER_PLUGOUT){
        NVIC_SetVector(CHARGER_IRQn, (uint32_t)app_battery_charger_handler);
        NVIC_SetPriority(CHARGER_IRQn, 3);
        NVIC_EnableIRQ(CHARGER_IRQn);
    }

    return status;

	
}



