# The menu bar of the mods puts its hooks on the code of the swap chain functions with MinHook. The vcpkg library of
# MinHook is a DLL, but version.dll must bring no other DLL into the game folder. Thus the mod compiles the MinHook
# sources itself, from the same archive as the vcpkg port (vcpkg/ports/minhook/portfile.cmake).

set(MOA_MOD_MINHOOK_VERSION "1.3.4")
set(MOA_MOD_MINHOOK_SHA512
    "8a33233598b56ad9da44d22d470c2432f68364dac31bc719fcd6b085e681fa10ddd41865fbde056ee7f4e7a075cc135344b6bf444eadbd7e7314ee1bedfd89b5"
)
set(_moa_minhook_archive_name "TsudaKageyu-minhook-v${MOA_MOD_MINHOOK_VERSION}.tar.gz")
set(_moa_minhook_deps "${CMAKE_BINARY_DIR}/_deps/moa_mod_minhook")

# Use the archive in the download cache of vcpkg. If it is not there, download it once into the build folder.
set(_moa_minhook_archive "$ENV{VCPKG_DOWNLOADS}/${_moa_minhook_archive_name}")
if (NOT DEFINED ENV{VCPKG_DOWNLOADS} OR NOT EXISTS "${_moa_minhook_archive}")
  set(_moa_minhook_archive "${_moa_minhook_deps}/${_moa_minhook_archive_name}")
  if (NOT EXISTS "${_moa_minhook_archive}")
    message(STATUS "Download the MinHook sources ${MOA_MOD_MINHOOK_VERSION} for the research mod")
    file(
      DOWNLOAD "https://github.com/TsudaKageyu/minhook/archive/v${MOA_MOD_MINHOOK_VERSION}.tar.gz"
      "${_moa_minhook_archive}"
      EXPECTED_HASH SHA512=${MOA_MOD_MINHOOK_SHA512}
      STATUS _moa_minhook_status
    )
    list(GET _moa_minhook_status 0 _moa_minhook_code)
    if (NOT _moa_minhook_code EQUAL 0)
      file(REMOVE "${_moa_minhook_archive}")
      message(FATAL_ERROR "The download of the MinHook sources failed: ${_moa_minhook_status}")
    endif ()
  endif ()
endif ()
file(SHA512 "${_moa_minhook_archive}" _moa_minhook_hash)
if (NOT _moa_minhook_hash STREQUAL MOA_MOD_MINHOOK_SHA512)
  message(FATAL_ERROR "${_moa_minhook_archive} is not the archive of MinHook ${MOA_MOD_MINHOOK_VERSION}")
endif ()

set(MOA_MOD_MINHOOK_DIR "${_moa_minhook_deps}/minhook-${MOA_MOD_MINHOOK_VERSION}")
if (NOT EXISTS "${MOA_MOD_MINHOOK_DIR}/src/hook.c")
  file(ARCHIVE_EXTRACT INPUT "${_moa_minhook_archive}" DESTINATION "${_moa_minhook_deps}")
endif ()

add_library(
  moa_mod_minhook STATIC
  ${MOA_MOD_MINHOOK_DIR}/src/buffer.c
  ${MOA_MOD_MINHOOK_DIR}/src/hook.c
  ${MOA_MOD_MINHOOK_DIR}/src/trampoline.c
  ${MOA_MOD_MINHOOK_DIR}/src/hde/hde64.c
)
set_target_properties(moa_mod_minhook PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# The mod includes this header before the header of the vcpkg MinHook.
target_include_directories(moa_mod_minhook BEFORE PUBLIC ${MOA_MOD_MINHOOK_DIR}/include)
target_compile_options(moa_mod_minhook PRIVATE /W0)
