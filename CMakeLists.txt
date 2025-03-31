# CMakeList.txt: 最上層的 CMake 專案檔，執行全域設定
# 並在這裡包含子專案。
#

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTLIBS REQUIRED
        gobject-2.0
        glib-2.0
        gstreamer-1.0
        libsoup-2.4
        json-glib-1.0)

# 如果支援，則為 MSVC 編譯器啟用熱重新載入。


	#include_directories(
	#	/usr/include/glib-2.0
	#	/usr/lib/x86_64-linux-gnu/glib-2.0/include
	#	/usr/include/gstreamer-1.0
	#	/usr/include/libsoup-2.4
	#	/usr/include/json-glib-1.0)
include_directories(
    /usr/include/glib-2.0
	/usr/lib/aarch64-linux-gnu/glib-2.0/include
	/usr/include/gstreamer-1.0
	/usr/include/libsoup-2.4
	/usr/include/json-glib-1.0)
# if linux


project ("GstWebrtcV2")

# 包含子專案。
add_subdirectory ("GstWebrtcV2")
