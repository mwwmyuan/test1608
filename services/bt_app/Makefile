
cur_dir := $(dir $(lastword $(MAKEFILE_LIST)))

obj_s := $(patsubst $(cur_dir)%,%,$(wildcard $(cur_dir)*.s))
obj_c := $(patsubst $(cur_dir)%,%,$(wildcard $(cur_dir)*.c))

obj_cpp := $(patsubst $(cur_dir)%,%,$(wildcard $(cur_dir)*.cpp))

obj-y := $(obj_c:.c=.o) $(obj_s:.S=.o) $(obj_cpp:.cpp=.o)

ccflags-y += \
	-Iservices/audio_eq \
	-Iservices/fs/fat \
	-Iservices/fs/sd \
	-Iservices/fs/fat/ChaN \
	-Iservices/bt_profiles/inc \
	-Iservices/bt_profiles/inc/sys \
	-Iservices/overlay \
	-Iservices/nvrecord \
	-Iservices/resources \
	-Iservices/multimedia/rbcodec \
	-Iservices/multimedia/audio/process/resample/include \
	-Iservices/multimedia/audio/process/filters/include \
	-Iservices/multimedia/audio/process/drc/include \
	-Iplatform/drivers/uarthci \
	-Iplatform/drivers/ana \
	-Iplatform/drivers/bt \
	-Iutils/cqueue \
	-Iservices/audioflinger \
	-Iutils/lockcqueue \
	-Iutils/intersyshci \
	-Iapps/key \
	-Iapps/main \
	-Iapps/common \
	-Iapps/audioplayers \
	-Iapps/common \
	-Iapps/factory \
	-Iservices/resources \
	-Iservices/ble_app \
	-Iutils/hwtimer_list \
	-Iservices/ble_stack/ble_ip \
	-Iservices/ble_stack/hl/api \
	-Iservices/ble_stack/app/api/  \
	-Iservices/ble_stack/common/api/ \
	-Iservices/ble_stack/hl/inc/ \
	-Iservices/ble_stack/ke/api \
	-Iservices/nvrecord 


KBUILD_CPPFLAGS += -DAUDIO_OUTPUT_VOLUME_DEFAULT=$(AUDIO_OUTPUT_VOLUME_DEFAULT)

ifeq ($(BT_XTAL_SYNC),1)
CFLAGS_app_bt_stream.o += -DBT_XTAL_SYNC
endif
