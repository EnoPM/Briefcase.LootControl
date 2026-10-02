add_library(Briefcase.LootControl.UE4SS SHARED LootControlUe4ss.cpp)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" MOD_VERSION)
string(STRIP "${MOD_VERSION}" MOD_VERSION)
configure_file(ModVersion.hpp.in generated/ModVersion.hpp @ONLY)
target_include_directories(Briefcase.LootControl.UE4SS PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(Briefcase.LootControl.UE4SS PRIVATE
  Briefcase::DeceiveInc nlohmann_json::nlohmann_json)
set_target_properties(Briefcase.LootControl.UE4SS PROPERTIES OUTPUT_NAME main)

if(BUILD_TESTING)
  add_executable(Briefcase.LootControl.Config ../tests/Config.cpp)
  target_link_libraries(Briefcase.LootControl.Config PRIVATE nlohmann_json::nlohmann_json)
  add_test(NAME Briefcase.LootControl.Config COMMAND Briefcase.LootControl.Config)
endif()
