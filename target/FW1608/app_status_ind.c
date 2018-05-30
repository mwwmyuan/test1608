#include "cmsis_os.h"
#include "stdbool.h"
#include "hal_trace.h"
#include "app_pwl.h"
#include "app_status_ind.h"
#include "string.h"

static APP_STATUS_INDICATION_T app_status = APP_STATUS_INDICATION_NUM;
static APP_STATUS_INDICATION_T app_status_ind_filter = APP_STATUS_INDICATION_NUM;

int app_status_indication_filter_set(APP_STATUS_INDICATION_T status)
{
    app_status_ind_filter = status;
    return 0;
}

APP_STATUS_INDICATION_T app_status_indication_get(void)
{
    return app_status;
}

int app_status_indication_set(APP_STATUS_INDICATION_T status)
{
    struct APP_PWL_CFG_T cfg0;
    struct APP_PWL_CFG_T cfg1;
	struct APP_PWL_CFG_T cfg2;
    TRACE("%s %d",__func__, status);

    if (app_status == status)
        return 0;

    if (app_status_ind_filter == status)
        return 0;
    
    app_status = status;
    memset(&cfg0, 0, sizeof(struct APP_PWL_CFG_T));
    memset(&cfg1, 0, sizeof(struct APP_PWL_CFG_T));
	memset(&cfg2, 0, sizeof(struct APP_PWL_CFG_T));
    app_pwl_stop(APP_PWL_ID_0);
    app_pwl_stop(APP_PWL_ID_1);
	app_pwl_stop(APP_PWL_ID_2);
    switch (status) {
        case APP_STATUS_INDICATION_POWERON:
            cfg0.part[0].level = 1;
            cfg0.part[0].time = (1000);
            cfg0.part[1].level = 0;
            cfg0.part[1].time = (200);

            cfg0.parttotal = 2;
            cfg0.startlevel = 1;
            cfg0.periodic = false;

            app_pwl_setup(APP_PWL_ID_0, &cfg0);
            app_pwl_start(APP_PWL_ID_0);

            break;
        case APP_STATUS_INDICATION_INITIAL:
            break;
        
    
        case APP_STATUS_INDICATION_BOTHSCAN:   
			TRACE("paringLED!!!");


            cfg1.part[0].level = 1;
            cfg1.part[0].time = (300);
            cfg1.part[1].level = 0;
            cfg1.part[1].time = (300);
            cfg1.parttotal = 2;
            cfg1.startlevel = 1;
            cfg1.periodic = true;


            app_pwl_setup(APP_PWL_ID_1, &cfg1);
            app_pwl_start(APP_PWL_ID_1);
            break;

	    case APP_STATUS_INDICATION_PAGESCAN:
        case APP_STATUS_INDICATION_CONNECTABLE:
            cfg1.part[0].level = 1;
            cfg1.part[0].time = (300);
            cfg1.part[1].level = 0;
            cfg1.part[1].time = (3000);
            cfg1.parttotal = 2;
            cfg1.startlevel = 0;
            cfg1.periodic = true;
        
            app_pwl_setup(APP_PWL_ID_1, &cfg1);
            app_pwl_start(APP_PWL_ID_1);
            break;

			case APP_STATUS_INDICATION_SRC_CONNECTABLE:
            cfg2.part[0].level = 1;
            cfg2.part[0].time = (300);
            cfg2.part[1].level = 0;
            cfg2.part[1].time = (3000);
            cfg2.parttotal = 2;
            cfg2.startlevel = 0;
            cfg2.periodic = true;
        
            app_pwl_setup(APP_PWL_ID_2, &cfg2);
            app_pwl_start(APP_PWL_ID_2);
            break;
			
        case APP_STATUS_INDICATION_CONNECTED:
			TRACE("connectedLED!!!");
            cfg1.part[0].level = 1;
            cfg1.part[0].time = (5000);
           
            cfg1.parttotal = 1;
            cfg1.startlevel = 1;
            cfg1.periodic = true;
            app_pwl_setup(APP_PWL_ID_1, &cfg1);
            app_pwl_start(APP_PWL_ID_1);
            break;

		case APP_STATUS_INDICATION_SRC_CONNECTED:
			TRACE("connectedLED!!!");
            cfg2.part[0].level = 1;
            cfg2.part[0].time = (5000);
           
            cfg2.parttotal = 1;
            cfg2.startlevel = 1;
            cfg2.periodic = true;
            app_pwl_setup(APP_PWL_ID_2, &cfg2);
            app_pwl_start(APP_PWL_ID_2);
            break;

         case APP_STATUS_INDICATION_SRC_SEARCH:   
			TRACE("paringLED!!!");


            cfg2.part[0].level = 1;
            cfg2.part[0].time = (300);
            cfg2.part[1].level = 0;
            cfg2.part[1].time = (300);
            cfg2.parttotal = 2;
            cfg2.startlevel = 1;
            cfg2.periodic = true;


            app_pwl_setup(APP_PWL_ID_2, &cfg2);
            app_pwl_start(APP_PWL_ID_2);
            break;

			
			
        case APP_STATUS_INDICATION_CHARGING:
            cfg0.part[0].level = 1;
            cfg0.part[0].time = (5000);
            cfg0.parttotal = 1;
            cfg0.startlevel = 1;
            cfg0.periodic = true;
            app_pwl_setup(APP_PWL_ID_0, &cfg0);
            app_pwl_start(APP_PWL_ID_0);
            break;
        case APP_STATUS_INDICATION_FULLCHARGE:
            cfg0.part[0].level = 0;
            cfg0.part[0].time = (5000);
            cfg0.parttotal = 1;
            cfg0.startlevel = 0;
            cfg0.periodic = true;
            app_pwl_setup(APP_PWL_ID_0, &cfg0);
            app_pwl_start(APP_PWL_ID_0);
            break;
        case APP_STATUS_INDICATION_POWEROFF:
            cfg0.part[0].level = 1;
            cfg0.part[0].time = (1000);
            cfg0.part[1].level = 0;
            cfg0.part[1].time = (200);

            cfg0.parttotal = 2;
            cfg0.startlevel = 1;
            cfg0.periodic = false;

            app_pwl_setup(APP_PWL_ID_0, &cfg0);
            app_pwl_start(APP_PWL_ID_0);

            break;
        case APP_STATUS_INDICATION_CHARGENEED:
            cfg1.part[0].level = 1;
            cfg1.part[0].time = (500);
            cfg1.part[1].level = 0;
            cfg1.part[1].time = (2000);
            cfg1.parttotal = 2;
            cfg1.startlevel = 1;
            cfg1.periodic = true;
            app_pwl_setup(APP_PWL_ID_1, &cfg1);
            app_pwl_start(APP_PWL_ID_1);    
            break;
        default:
            break;
    }
    return 0;
}

