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
#if (POLICY CMP0141)
#     cmake_policy(SET CMP0141 NEW)
#      set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
#          include_directories(
#            C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\glib-2.0\\include
#            C:\\gstreamer\\1.0\\msvc_x86_64\\include\\gstreamer-1.0
#            C:\\gstreamer\\1.0\\msvc_x86_64\\include\\glib-2.0
#            C:\\gstreamer\\1.0\\msvc_x86_64\\include\\glib-2.0\\glib
#            C:\\gstreamer\\1.0\\msvc_x86_64\\include\\json-glib-1.0
#           C:\\gstreamer\\1.0\\msvc_x86_64\\include\\libsoup-2.4)
#endif()

	include_directories(
		/usr/include/glib-2.0
		/usr/lib/x86_64-linux-gnu/glib-2.0/include
		/usr/include/gstreamer-1.0
		/usr/include/libsoup-2.4
		/usr/include/json-glib-1.0)
	#include_directories(
    #/usr/include/glib-2.0
	#/usr/lib/aarch64-linux-gnu/glib-2.0/include
	#/usr/include/gstreamer-1.0
	#/usr/include/libsoup-2.4
	#/usr/include/json-glib-1.0)
# if linux


project ("GstWebrtcV2")

# 包含子專案。
add_subdirectory ("GstWebrtcV2")
