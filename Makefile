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

$(TWEAK_NAME)_FILES = Source/Menu.mm Source/fishhook.c

# Source/Includes/Generated/mach_excServer.c (code MIG sinh ra cho Mach exception server) KHÔNG
# còn được liệt kê ở đây nữa - HWBreakHook.h (nơi duy nhất định nghĩa 3 hàm
# catch_mach_exception_raise*() mà file .c đó cần) không còn được include ở đâu cả kể từ khi
# AssetRedirect.h bỏ hẳn hook, chuyển sang gọi trực tiếp kiểu Monite (xem AssetRedirect.h PHẦN 3).
# Biên dịch file .c đó vào mà thiếu 3 hàm implementation sẽ lỗi "symbol(s) not found" lúc link -
# đã xác nhận qua CI. fishhook.c vẫn giữ lại dù cũng không còn ai gọi tới (không hại gì, chỉ tốn
# vài KB) - khác mach_excServer.c là file PHẢI được `mig` sinh ra trước mỗi lần build, nên bỏ hẳn
# ra khỏi FILES thay vì để trong build mà không dùng.

$(TWEAK_NAME)_LIBRARIES += substrate z

include $(THEOS_MAKE_PATH)/tweak.mk
