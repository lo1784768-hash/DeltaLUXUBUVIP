ifndef THEOS
export THEOS=/var/mobile/theos
endif

ARCHS = arm64
TARGET = iphone:clang:latest:14.0

DEBUG = 0
FINALPACKAGE = 1
FOR_RELEASE = 1

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = Delta

$(TWEAK_NAME)_FRAMEWORKS = UIKit Foundation QuartzCore CoreGraphics AudioToolbox

$(TWEAK_NAME)_CCFLAGS = -std=c++11 -fno-rtti -fno-exceptions -DNDEBUG
$(TWEAK_NAME)_CFLAGS = -fobjc-arc -Wno-deprecated-declarations -Wno-unused-variable -Wno-unused-value

$(TWEAK_NAME)_FILES = Source/Menu.mm Source/fishhook.c Source/Includes/Generated/mach_excServer.c

# Source/Includes/Generated/mach_excServer.c là code sinh ra bởi `mig` (bước "Generate MIG exception
# server stubs" trong .github/workflows/build.yml) từ mach_exc.defs của SDK thật trên máy build
# macOS - đây là code Apple sinh ra, không phải tự viết tay, dùng để thay thế bộ khung Mach
# exception tự viết tay trước đây (xem HWBreakHook.h). File này không tồn tại nếu build local mà
# chưa chạy bước mig - CI luôn chạy bước đó trước khi build.
Source/Includes/Generated/mach_excServer.c:
	$(error Source/Includes/Generated/mach_excServer.c không tồn tại - chạy `mig` trước (xem .github/workflows/build.yml) hoặc build qua CI)

$(TWEAK_NAME)_LIBRARIES += substrate z

include $(THEOS_MAKE_PATH)/tweak.mk
