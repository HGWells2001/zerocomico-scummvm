MODULE := engines/zerocomico

MODULE_OBJS := \
	bsp.o \
	lzhuf.o \
	metaengine.o \
	model.o \
	model_animation.o \
	model_data.o \
	resource.o \
	scene_model.o \
	script.o \
	script_program.o \
	wrapped_flic.o \
	zerocomico.o

MODULE_DIRS += \
	engines/zerocomico

ifeq ($(ENABLE_ZEROCOMICO), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

include $(srcdir)/rules.mk

DETECT_OBJS += $(MODULE)/detection.o
