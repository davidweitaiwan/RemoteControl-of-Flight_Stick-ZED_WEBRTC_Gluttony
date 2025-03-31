# CMakeList.txt: GstWebrtcV2 的 CMake 專案，在此包含來源及定義
# 專案專屬邏輯。
#

# 將來源新增到此專案的可執行檔。
add_executable (GstWebrtcV2 "GstWebrtcV2.cpp" )

if (POLICY CMP0141)
target_link_libraries(GstWebrtcV2 C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\json-glib-1.0.lib) # also adds the required include path
target_link_libraries(GstWebrtcV2 C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\gobject-2.0.lib) # also adds the required include path
target_link_libraries(GstWebrtcV2 C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\gstreamer-1.0.lib) # also adds the required include path
target_link_libraries(GstWebrtcV2 C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\glib-2.0.lib) # also adds the required include path
target_link_libraries(GstWebrtcV2 C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\soup-2.4.lib) # also adds the required include path
target_link_libraries(GstWebrtcV2 C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\gstwebrtc-1.0.lib) # also adds the required include path
target_link_libraries(GstWebrtcV2 C:\\gstreamer\\1.0\\msvc_x86_64\\lib\\gstsdp-1.0.lib) # also adds the required include path
endif()

# if GNU compiler
if (CMAKE_COMPILER_IS_GNUCXX)
#if x86_64
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
target_link_libraries(GstWebrtcV2 /usr/lib/x86_64-linux-gnu/libjson-glib-1.0.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/x86_64-linux-gnu/libgobject-2.0.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/x86_64-linux-gnu/libgstreamer-1.0.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/x86_64-linux-gnu/libglib-2.0.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/x86_64-linux-gnu/libsoup-2.4.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/x86_64-linux-gnu/libgstwebrtc-1.0.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/x86_64-linux-gnu/libgstsdp-1.0.so) # also adds the required include path
endif()
if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
target_link_libraries(GstWebrtcV2 /usr/lib/aarch64-linux-gnu/libjson-glib-1.0.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/aarch64-linux-gnu/libgobject-2.0.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/aarch64-linux-gnu/libgstreamer-1.0.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/aarch64-linux-gnu/libglib-2.0.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/aarch64-linux-gnu/libsoup-2.4.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/aarch64-linux-gnu/libgstwebrtc-1.0.so) # also adds the required include path
target_link_libraries(GstWebrtcV2 /usr/lib/aarch64-linux-gnu/libgstsdp-1.0.so) # also adds the required include path
endif()

endif()

# TODO: 新增測試，並視需要安裝目標。
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET GstWebrtcV2 PROPERTY CXX_STANDARD 20)
endif()