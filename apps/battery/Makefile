cur_dir := $(dir $(lastword $(MAKEFILE_LIST)))

obj-y := $(patsubst $(cur_dir)%,%,$(wildcard $(cur_dir)*.c $(cur_dir)*.cpp $(cur_dir)*.S))
obj-y := $(obj-y:.c=.o)
obj-y := $(obj-y:.cpp=.o)
obj-y := $(obj-y:.S=.o)

subdir-ccflags-y += -Iplatform/drivers/ana \
                    -Iservices/nvrecord \
					-Iservices/bt_app \
				    -Iservices/bt_profiles\inc \
                    -Iapps/audioplayers \					
					
						


ifeq ($(RTOS),1)
ccflags-y += -DRTOS
endif
