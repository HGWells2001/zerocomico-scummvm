MODULE := engines/zerocomico

MODULE_OBJS := \
	bsp.o \
	camera_script.o \
	character_script.o \
	chapter.o \
	cutscene_script.o \
	dialog_script.o \
	lzhuf.o \
	metaengine.o \
	model.o \
	model_animation.o \
	model_data.o \
	puzzle_script.o \
	resource.o \
	scene_model.o \
	sequence_script.o \
	script.o \
	script_program.o \
	script_vm.o \
	shape_script.o \
	software_renderer.o \
	text_table_script.o \
	wrapped_flic.o \
	zerocomico.o

MODULE_DIRS += \
	engines/zerocomico

ifeq ($(ENABLE_ZEROCOMICO), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

include $(srcdir)/rules.mk

DETECT_OBJS += $(MODULE)/detection.o
