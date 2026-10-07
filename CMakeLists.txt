cmake_minimum_required(VERSION 3.25)
project(d2x VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
include(cmake/Dependencies.cmake)

add_library(d2x_core INTERFACE)
target_include_directories(d2x_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(d2x_core INTERFACE cxx_std_20)
if(WIN32)
  target_compile_definitions(d2x_core INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_library(d2x_resources src/resources/atomic_file.cpp src/resources/archive.cpp src/resources/formats.cpp src/resources/dcc.cpp src/resources/data_table.cpp src/resources/text.cpp src/resources/anim_data.cpp src/resources/monster_palshift.cpp)
target_link_libraries(d2x_resources PUBLIC d2x_core PRIVATE storm)

add_library(d2x_navigation src/world/navigation.cpp src/world/room_activation.cpp)
target_link_libraries(d2x_navigation PUBLIC d2x_core)
add_library(d2x_world src/world/map.cpp src/world/map_assembly.cpp src/world/maze.cpp src/world/outdoor/outdoor.cpp src/world/region.cpp src/world/region_catalog.cpp src/world/exits.cpp src/world/world_report.cpp)
target_sources(d2x_world PRIVATE src/world/object_population.cpp)
target_sources(d2x_world PRIVATE src/world/chest.cpp)
target_sources(d2x_world PRIVATE src/world/shrine_catalog.cpp)
target_sources(d2x_world PRIVATE src/world/native_room_activation.cpp src/world/native_map.cpp)
target_sources(d2x_world PRIVATE src/world/outdoor/native_act_layout.cpp src/world/retail/outdoor_grid.cpp
  src/world/outdoor/native_jungle_layout.cpp src/world/outdoor/native_connections.cpp src/world/retail/boundary.cpp
  src/world/retail/border_presets.cpp src/world/retail/secondary_border.cpp
  src/world/retail/wilderness.cpp src/world/retail/desert.cpp src/world/retail/jungle.cpp
  src/world/retail/mesa.cpp src/world/retail/barricade.cpp src/world/retail/dirt_paths.cpp
  src/world/retail/outdoor_layout.cpp src/world/retail/rooms.cpp
  src/world/retail/preset_scan.cpp src/world/retail/room_grids.cpp
  src/world/retail/room_themes.cpp src/world/retail/tile_selector.cpp
  src/world/retail/room_data.cpp src/world/retail/preset_room.cpp
  src/world/retail/tile_materialization.cpp)
target_sources(d2x_world PRIVATE
  src/world/outdoor/outdoor_substitution.cpp
  src/world/outdoor/outdoor_shrines.cpp
  src/world/outdoor/outdoor_river.cpp
  src/world/outdoor/outdoor_paths.cpp
  src/world/cow_level.cpp
  src/world/maze/generation.cpp src/world/maze/resources.cpp
  src/world/maze/cave.cpp src/world/maze/crypt.cpp src/world/maze/barracks.cpp
  src/world/maze/jail.cpp src/world/maze/catacombs.cpp)
target_link_libraries(d2x_world PUBLIC d2x_content d2x_navigation)

# Read-only item definitions and property calculations; no inventory authority.
add_library(d2x_items src/gameplay/items/definitions.cpp
  src/gameplay/items/inventory_error.cpp src/gameplay/items/equipment_rules.cpp src/gameplay/items/equipment_combat.cpp)
target_link_libraries(d2x_items PUBLIC d2x_core)
add_library(d2x_content src/content/classic_data.cpp src/content/lod_data.cpp src/content/world/world_catalog.cpp src/content/monsters/monster_catalog.cpp)
target_sources(d2x_content PRIVATE src/content/character/character_progression.cpp)
target_sources(d2x_content PRIVATE src/content/character/character_attributes.cpp)
target_sources(d2x_content PRIVATE src/content/character/death_data.cpp)
target_sources(d2x_content PRIVATE src/content/string_table.cpp src/content/skills/skill_data.cpp
  src/content/skills/skill_eligibility.cpp)
target_sources(d2x_content PRIVATE src/content/skills/sorceress_data.cpp src/content/skills/frozen_orb_data.cpp src/content/skills/blizzard_data.cpp src/content/skills/glacial_spike_data.cpp src/content/skills/weapon_skill_data.cpp src/content/skills/necromancer_data.cpp src/content/skills/necro_summon_data.cpp src/content/skills/curse_data.cpp
    src/content/skills/bone_data.cpp src/content/skills/amazon_bow_data.cpp src/content/skills/amazon_spear_data.cpp src/content/skills/amazon_magic_data.cpp src/content/skills/amazon_summon_data.cpp)
target_sources(d2x_content PRIVATE src/content/skills/skill_animation.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_projectiles.cpp src/content/skills/missile_effects.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_experience.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_combat.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_difficulty_combat.cpp src/content/monsters/monster_enchantment.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_ai_data.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_spell_data.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_animation.cpp)
target_sources(d2x_content PRIVATE src/content/items/equipment_modifiers.cpp)
target_sources(d2x_content PRIVATE src/content/items/equipment_data.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_appearance.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_loot.cpp)
target_sources(d2x_content PRIVATE src/content/items/object_loot.cpp)
target_sources(d2x_content PRIVATE src/content/items/chest_loot.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_quality.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_loot.cpp)
target_sources(d2x_content PRIVATE src/content/items/special_items.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_affixes.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_properties.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_descriptions.cpp src/content/items/item_pricing.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_consumables.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_magic_loot.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_grades.cpp)
target_sources(d2x_content PRIVATE src/content/npc/npc_dialogue.cpp)
target_sources(d2x_content PRIVATE src/content/quest/quest_data.cpp)
target_sources(d2x_content PRIVATE src/content/npc/hireling_data.cpp)
target_sources(d2x_content PRIVATE src/content/world/automap_data.cpp)
target_sources(d2x_content PRIVATE src/content/npc/vendor_data.cpp)
target_link_libraries(d2x_content PUBLIC d2x_resources d2x_items)

# Rules and state compile without raylib, StormLib, a window or an archive.
# Pure display, geometry and resource-tool calculations. No simulation/executor.
add_library(d2x_gameplay
  src/gameplay/character/attributes.cpp src/gameplay/character/skill_rank.cpp
  src/gameplay/combat/stat_modifiers.cpp src/gameplay/combat/geometry.cpp
  src/gameplay/combat/attack_element_ranges.cpp
  src/gameplay/skills/resolve.cpp src/gameplay/skills/curse_resolve.cpp
  src/gameplay/skills/summon_resolve.cpp src/gameplay/skills/amazon_summon_resolve.cpp
  src/gameplay/skills/projectile_path.cpp src/gameplay/skills/damage_curve.cpp
  src/gameplay/skills/rank_sources.cpp src/gameplay/skills/rank_bonus.cpp src/gameplay/skills/aura_resolve.cpp
  src/gameplay/skills/passive.cpp
  src/gameplay/monsters/implementation.cpp src/gameplay/monsters/rank.cpp
  src/gameplay/loot/treasure.cpp src/gameplay/loot/quality.cpp
  src/gameplay/loot/special.cpp src/gameplay/loot/affix.cpp
  src/gameplay/rewards/experience.cpp)
target_link_libraries(d2x_gameplay PUBLIC d2x_navigation)
target_sources(d2x_content PRIVATE src/content/skills/state_data.cpp src/content/world/shrine_data.cpp
  src/content/skills/aura_data.cpp)
target_link_libraries(d2x_content PUBLIC d2x_gameplay)
target_sources(d2x_world PRIVATE src/world/interaction_geometry.cpp)

add_library(d2x_population src/world/population.cpp)
target_link_libraries(d2x_population PUBLIC d2x_content d2x_gameplay)
target_sources(d2x_content PRIVATE src/content/items/socket_data.cpp)
add_library(d2x_persistence src/persistence/d2s_codec.cpp src/persistence/save_file.cpp
  src/persistence/d2s_header.cpp src/persistence/d2s_fixed_sections.cpp
  src/persistence/d2s_stats.cpp
  src/persistence/d2s_items.cpp
  src/persistence/d2s_inventory.cpp
  src/persistence/d2s_skills.cpp)
target_sources(d2x_persistence PRIVATE src/persistence/d2s_quests.cpp)
target_link_libraries(d2x_persistence PUBLIC d2x_gameplay d2x_items d2x_content)

add_library(d2x_client_api INTERFACE)
target_link_libraries(d2x_client_api INTERFACE d2x_core)
include(cmake/Networking.cmake)
add_library(d2x_client src/client/inventory_view.cpp src/client/automap_exploration.cpp
  src/client/character_projection.cpp src/client/quest_projection.cpp)
target_link_libraries(d2x_client PUBLIC d2x_client_api PRIVATE d2x_content)
add_library(d2x_remote_scene src/client/remote_town.cpp src/client/remote_control.cpp src/client/remote_inventory.cpp src/client/remote_combat.cpp src/client/remote_ui_clients.cpp)
target_link_libraries(d2x_remote_scene PUBLIC d2x_client d2x_world d2x_remote_client)
target_sources(d2x_world PRIVATE src/world/preset_identity.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_display.cpp src/content/character/character_display.cpp)
target_sources(d2x_content PRIVATE src/content/character/realm_portrait.cpp)

add_library(d2x_presentation
  src/presentation/graphics/graphics.cpp src/presentation/audio/audio.cpp src/presentation/graphics/primitives.cpp
  src/presentation/graphics/original_menu.cpp
  src/presentation/scene_assets.cpp src/presentation/actors/actor_animation.cpp src/presentation/scene_view.cpp
  src/presentation/hud/skill_assets.cpp src/presentation/hud/hud_controls.cpp src/presentation/hud/skill_controller.cpp
  src/presentation/hud/classic_hud.cpp
  src/presentation/hud/chat_view.cpp
  src/presentation/hud/trade_invite_view.cpp
  src/presentation/hud/skill_tree_view.cpp src/presentation/hud/quest_panel.cpp
  src/presentation/hud/quest_controller.cpp src/presentation/npc/npc_controller.cpp
  src/presentation/npc/npc_alert_view.cpp src/presentation/hud/panel_frame.cpp
  src/presentation/hud/exit_view.cpp src/presentation/hud/waypoint_view.cpp
  src/presentation/world/world_renderer.cpp src/presentation/hud/hud.cpp src/presentation/npc/npc_dialogue_view.cpp src/presentation/npc/npc_menu_view.cpp src/presentation/npc/npc_shop_view.cpp src/presentation/controller.cpp
  src/presentation/loot_view.cpp src/presentation/inventory/inventory_panel.cpp
  src/presentation/hud/character_panel.cpp
  src/presentation/inventory/container_view.cpp src/presentation/inventory/storage_view.cpp src/presentation/inventory/cube_view.cpp src/presentation/inventory/belt_view.cpp src/presentation/inventory/inventory_view.cpp src/presentation/inventory/inventory_controller.cpp)
target_link_libraries(d2x_presentation PUBLIC d2x_client raylib PRIVATE d2x_content d2x_world)
target_sources(d2x_content PRIVATE src/content/audio/sound_catalog.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/audio/scene_audio.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/npc/hireling_view.cpp
  src/presentation/npc/client_views.cpp src/presentation/npc/commerce_controller.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/automap_assets.cpp src/presentation/world/automap_view.cpp
  src/presentation/world/map_view.cpp src/presentation/world/map_controller.cpp src/presentation/world/object_hint.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/lighting_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/palette_blend_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/audio/audio_emitters.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/projectile_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/client_missile_assets.cpp
  src/presentation/world/client_missile_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/inventory/item_tooltip.cpp
  src/presentation/npc/inventory_interactions.cpp)

add_executable(d2x src/main.cpp src/app/application.cpp src/app/options.cpp src/app/input.cpp
  src/app/frontend.cpp src/app/online_login_memory.cpp src/app/client_preferences.cpp src/app/crash_report.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/frontend/realm_frontend.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/remote/remote_scene.cpp)
target_link_libraries(d2x_presentation PRIVATE d2x_remote_scene)
target_sources(d2x PRIVATE src/app/debug/debug_pipe.cpp src/app/debug/ui_input.cpp)
target_sources(d2x PRIVATE src/app/debug/online_commands.cpp)
target_link_libraries(d2x PRIVATE d2x_presentation d2x_remote_client)
target_link_libraries(d2x PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(d2x PRIVATE advapi32 dbghelp)
endif()
add_executable(d2x_assets src/asset_tool.cpp)
target_link_libraries(d2x_assets PRIVATE d2x_content d2x_world d2x_population d2x_persistence raylib nlohmann_json::nlohmann_json)
foreach(target IN ITEMS d2x_resources d2x_navigation d2x_world d2x_items d2x_content d2x_gameplay d2x_population d2x_persistence d2x_presentation d2x_client d2x_network d2x_d2gs_protocol d2x_remote_client d2x_remote_scene d2x d2x_assets)
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

target_sources(d2x_content PRIVATE src/content/items/cube_data.cpp)
