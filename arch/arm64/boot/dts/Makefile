dts-dirs += amd
dts-dirs += apm
dts-dirs += arm
dts-dirs += cavium

DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))
DTB_NAMES += $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES_MIN_NO_APPEND))
ifneq ($(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES_MIN),"")
	DTB_NAMES += $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES_MIN))_evt
endif
DTB_NAMES += $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES_P0_01))
DTB_NAMES += $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES_HVT))
DTB_NAMES += $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES_HVT1_2))
DTB_NAMES += $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES_EVT))
DTB_NAMES += $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES_DVT))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif

always += $(DTB_LIST)
subdir-y	:= $(dts-dirs)
clean-files := dts/*.dtb *.dtb
