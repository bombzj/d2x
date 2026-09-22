cmake_minimum_required(VERSION 3.25)
project(d2x VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
include(cmake/Dependencies.cmake)

add_library(d2x_core INTERFACE)
target_include_directories(d2x_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(d2x_core INTERFACE cxx_std_20)
if(WIN32)
  target_compile_definitions(d2x_core INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_library(d2x_resources src/resources/archive.cpp src/resources/formats.cpp src/resources/dcc.cpp src/resources/data_table.cpp)
target_link_libraries(d2x_resources PUBLIC d2x_core PRIVATE storm)

add_library(d2x_navigation src/world/navigation.cpp)
target_link_libraries(d2x_navigation PUBLIC d2x_core)
add_library(d2x_world src/world/map.cpp src/world/region.cpp src/world/region_catalog.cpp)
target_link_libraries(d2x_world PUBLIC d2x_content d2x_navigation)

add_library(d2x_items
  src/gameplay/items/definitions.cpp
  src/gameplay/items/inventory.cpp src/gameplay/items/transfers.cpp src/gameplay/items/collection.cpp
  src/gameplay/items/preview.cpp src/gameplay/items/belt.cpp src/gameplay/items/inventory_snapshot.cpp)
target_link_libraries(d2x_items PUBLIC d2x_core)
add_library(d2x_content src/content/classic_data.cpp src/content/lod_data.cpp src/content/world_catalog.cpp src/content/world_report.cpp src/content/monster_catalog.cpp)
target_link_libraries(d2x_content PUBLIC d2x_resources d2x_items)

# Rules and state compile without raylib, StormLib, a window or an archive.
add_library(d2x_gameplay
  src/gameplay/definitions.cpp src/gameplay/simulation.cpp src/gameplay/movement.cpp
  src/gameplay/combat.cpp src/gameplay/skills.cpp src/gameplay/monster_ai.cpp src/gameplay/loot.cpp src/gameplay/potions.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/monster_spawn.cpp)
target_link_libraries(d2x_gameplay PUBLIC d2x_navigation)
add_library(d2x_population src/world/population.cpp)
target_link_libraries(d2x_population PUBLIC d2x_content d2x_gameplay)
add_library(d2x_session src/gameplay/session.cpp src/gameplay/session_loot.cpp src/gameplay/session_inventory.cpp src/gameplay/session_consumables.cpp src/gameplay/session_interaction.cpp src/gameplay/session_snapshot.cpp)
target_link_libraries(d2x_session PUBLIC d2x_gameplay d2x_world d2x_content d2x_population)
add_library(d2x_persistence src/persistence/save_codec.cpp src/persistence/save_file.cpp)
target_link_libraries(d2x_persistence PUBLIC d2x_gameplay d2x_items)

add_library(d2x_presentation
  src/presentation/graphics.cpp src/presentation/audio.cpp src/presentation/primitives.cpp
  src/presentation/scene_assets.cpp src/presentation/scene_view.cpp
  src/presentation/world_renderer.cpp src/presentation/hud.cpp src/presentation/controller.cpp
  src/presentation/loot_view.cpp src/presentation/inventory_panel.cpp
  src/presentation/container_view.cpp src/presentation/storage_view.cpp src/presentation/belt_view.cpp src/presentation/inventory_view.cpp src/presentation/inventory_controller.cpp)
target_link_libraries(d2x_presentation PUBLIC d2x_session raylib)

add_executable(d2x src/main.cpp src/app/application.cpp src/app/options.cpp src/app/input.cpp)
target_link_libraries(d2x PRIVATE d2x_presentation d2x_persistence)
add_executable(d2x_assets src/asset_tool.cpp)
target_link_libraries(d2x_assets PRIVATE d2x_content d2x_world d2x_population raylib)
foreach(target IN ITEMS d2x_resources d2x_navigation d2x_world d2x_items d2x_content d2x_gameplay d2x_population d2x_session d2x_persistence d2x_presentation d2x d2x_assets)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endforeach()
if(MINGW)
  foreach(app IN ITEMS d2x d2x_assets)
    target_link_options(${app} PRIVATE -static -static-libgcc -static-libstdc++)
  endforeach()
endif()
