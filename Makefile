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

# Dobby (Source/Includes/Dobby/) - dùng để inline-hook hàm nằm THẲNG trong UnityFramework/
# GameAssembly (AntiReportSpoof.h) - MSHookFunction (Substrate) xác nhận KHÔNG hook được hàm đó
# trên máy thật (test cả 2 kiểu địa chỉ: tra theo tên lẫn RVA cứng, cả 2 đều thất bại), nhiều khả
# năng do prologue có instruction PAC (arm64e) mà Substrate không hiểu. libdobby.a là universal
# archive có sẵn cả slice arm64 lẫn arm64e (đã kiểm tra bằng `file`), khớp ARCHS=arm64 ở trên.
$(TWEAK_NAME)_LDFLAGS += Source/Includes/Dobby/libdobby.a

include $(THEOS_MAKE_PATH)/tweak.mk
