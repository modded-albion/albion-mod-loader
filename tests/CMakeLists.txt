find_package(GTest CONFIG REQUIRED)

# The small mod DLLs that the tests of the DLL loader load. Each one is the same source with another kind.
set(MOA_TEST_MODS_DIR ${CMAKE_CURRENT_BINARY_DIR}/test_mods)
set(_moa_test_mods
    good
    1
    init_fails
    2
    old
    3
    crash
    4
    same_id
    5
    bad_id
    6
)
set(_moa_test_mod_targets)
while (_moa_test_mods)
  list(POP_FRONT _moa_test_mods _name _kind)
  set(_target moa_test_mod_${_name})
  add_library(${_target} SHARED mods/TestMod.cpp)
  target_compile_definitions(${_target} PRIVATE MOA_TEST_MOD_KIND=${_kind})
  target_link_libraries(${_target} PRIVATE moa_mod_sdk)
  set_target_properties(${_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${MOA_TEST_MODS_DIR})
  moa_set_warnings(${_target})
  list(APPEND _moa_test_mod_targets ${_target})
endwhile ()

add_executable(
  moa_loader_tests
  test_CarriableMerge.cpp
  test_HookRules.cpp
  test_KnackMerge.cpp
  test_LoaderSettings.cpp
  test_MapOverrideSet.cpp
  test_ModCatalogue.cpp
  test_ModDllLoader.cpp
  test_ModRegistry.cpp
  test_OverlayRules.cpp
  test_Pack.cpp
  test_PackTables.cpp
  test_PackWriter.cpp
  test_ResourceLoads.cpp
  test_TemplatePlan.cpp
  test_TypedMerge.cpp
)
target_link_libraries(moa_loader_tests PRIVATE moa_loader_core moa_loader_dlls GTest::gtest_main)
target_compile_definitions(moa_loader_tests PRIVATE MOA_TEST_MODS_DIR="${MOA_TEST_MODS_DIR}" WIN32_LEAN_AND_MEAN NOMINMAX)
add_dependencies(moa_loader_tests ${_moa_test_mod_targets})
moa_set_warnings(moa_loader_tests)

include(GoogleTest)
gtest_discover_tests(moa_loader_tests DISCOVERY_MODE PRE_TEST)
