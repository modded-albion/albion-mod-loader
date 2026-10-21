# The menu bar of the mods uses Dear ImGui. The vcpkg library of ImGui uses the DLL C runtime, but version.dll uses the
# static C runtime and must bring no other DLL into the game folder. Thus the loader compiles the ImGui sources itself,
# from the same archive as the vcpkg port (vcpkg/ports/imgui/portfile.cmake, feature docking-experimental).

set(MOA_MOD_IMGUI_TAG "1.92.9b-docking")
set(MOA_MOD_IMGUI_SHA512
    "7eddcdb475f1db1fc8242d918533b955c964d2267abe713bdf23f8e2444770946d3c79c7855e360bab6168e36231b95bd05a84106c08f876dcd53daac9caccac"
)
set(_moa_imgui_archive_name "ocornut-imgui-v${MOA_MOD_IMGUI_TAG}.tar.gz")
set(_moa_imgui_deps "${CMAKE_BINARY_DIR}/_deps/moa_mod_imgui")

# Use the archive in the download cache of vcpkg. If it is not there, download it once into the build folder.
set(_moa_imgui_archive "$ENV{VCPKG_DOWNLOADS}/${_moa_imgui_archive_name}")
if (NOT DEFINED ENV{VCPKG_DOWNLOADS} OR NOT EXISTS "${_moa_imgui_archive}")
  set(_moa_imgui_archive "${_moa_imgui_deps}/${_moa_imgui_archive_name}")
  if (NOT EXISTS "${_moa_imgui_archive}")
    message(STATUS "Download the Dear ImGui sources ${MOA_MOD_IMGUI_TAG} for the mod loader")
    file(
      DOWNLOAD "https://github.com/ocornut/imgui/archive/v${MOA_MOD_IMGUI_TAG}.tar.gz" "${_moa_imgui_archive}"
      EXPECTED_HASH SHA512=${MOA_MOD_IMGUI_SHA512}
      STATUS _moa_imgui_status
    )
    list(GET _moa_imgui_status 0 _moa_imgui_code)
    if (NOT _moa_imgui_code EQUAL 0)
      file(REMOVE "${_moa_imgui_archive}")
      message(FATAL_ERROR "The download of the Dear ImGui sources failed: ${_moa_imgui_status}")
    endif ()
  endif ()
endif ()
file(SHA512 "${_moa_imgui_archive}" _moa_imgui_hash)
if (NOT _moa_imgui_hash STREQUAL MOA_MOD_IMGUI_SHA512)
  message(FATAL_ERROR "${_moa_imgui_archive} is not the archive of Dear ImGui ${MOA_MOD_IMGUI_TAG}")
endif ()

set(MOA_MOD_IMGUI_DIR "${_moa_imgui_deps}/imgui-${MOA_MOD_IMGUI_TAG}")
if (NOT EXISTS "${MOA_MOD_IMGUI_DIR}/imgui.cpp")
  file(ARCHIVE_EXTRACT INPUT "${_moa_imgui_archive}" DESTINATION "${_moa_imgui_deps}")
endif ()

add_library(
  moa_mod_imgui STATIC
  ${MOA_MOD_IMGUI_DIR}/imgui.cpp
  ${MOA_MOD_IMGUI_DIR}/imgui_draw.cpp
  ${MOA_MOD_IMGUI_DIR}/imgui_tables.cpp
  ${MOA_MOD_IMGUI_DIR}/imgui_widgets.cpp
  ${MOA_MOD_IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${MOA_MOD_IMGUI_DIR}/backends/imgui_impl_dx11.cpp
  ${MOA_MOD_IMGUI_DIR}/backends/imgui_impl_dx12.cpp
)
set_target_properties(moa_mod_imgui PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# The mod includes these headers before the headers of the vcpkg ImGui.
target_include_directories(moa_mod_imgui BEFORE PUBLIC ${MOA_MOD_IMGUI_DIR} ${MOA_MOD_IMGUI_DIR}/backends)
# The options of ImGui are in one header, for the ImGui sources and for the mod.
target_include_directories(moa_mod_imgui PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../imgui_config)
target_compile_definitions(moa_mod_imgui PUBLIC IMGUI_USER_CONFIG="ModImGuiConfig.h")
target_compile_options(moa_mod_imgui PRIVATE /W0 /utf-8)
# The backends compile their shaders with d3dcompiler_47.dll, and the DirectX 12 backend uses dxgi.dll.
target_link_libraries(moa_mod_imgui PUBLIC d3dcompiler dxgi dwmapi)
