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

add_library(d2x_resources src/resources/archive.cpp src/resources/formats.cpp src/resources/dcc.cpp src/resources/data_table.cpp src/resources/text.cpp src/resources/anim_data.cpp src/resources/monster_palshift.cpp)
target_link_libraries(d2x_resources PUBLIC d2x_core PRIVATE storm)

add_library(d2x_navigation src/world/navigation.cpp src/world/room_activation.cpp)
target_link_libraries(d2x_navigation PUBLIC d2x_core)
add_library(d2x_world src/world/map.cpp src/world/map_assembly.cpp src/world/maze.cpp src/world/outdoor.cpp src/world/outdoor_layout.cpp src/world/region.cpp src/world/region_catalog.cpp src/world/exits.cpp src/world/world_report.cpp)
target_sources(d2x_world PRIVATE src/world/object_population.cpp)
target_sources(d2x_world PRIVATE src/world/chest.cpp)
target_sources(d2x_world PRIVATE src/world/shrine_catalog.cpp)
target_sources(d2x_world PRIVATE
  src/world/outdoor_substitution.cpp
  src/world/outdoor_shrines.cpp
  src/world/outdoor_river.cpp
  src/world/outdoor_paths.cpp
  src/world/outdoor_cliffs.cpp
  src/world/cow_level.cpp
  src/world/maze/generation.cpp src/world/maze/resources.cpp
  src/world/maze/cave.cpp src/world/maze/crypt.cpp src/world/maze/barracks.cpp
  src/world/maze/jail.cpp src/world/maze/catacombs.cpp)
target_link_libraries(d2x_world PUBLIC d2x_content d2x_navigation)

add_library(d2x_items
  src/gameplay/items/equipment_rules.cpp
  src/gameplay/items/equipment.cpp
  src/gameplay/items/equipment_stats.cpp
  src/gameplay/items/durability.cpp
  src/gameplay/items/definitions.cpp
  src/gameplay/items/inventory.cpp src/gameplay/items/transfers.cpp src/gameplay/items/collection.cpp
  src/gameplay/items/preview.cpp src/gameplay/items/belt.cpp src/gameplay/items/books.cpp src/gameplay/items/inventory_snapshot.cpp)
target_link_libraries(d2x_items PUBLIC d2x_core)
add_library(d2x_content src/content/classic_data.cpp src/content/lod_data.cpp src/content/world_catalog.cpp src/content/monster_catalog.cpp)
target_sources(d2x_content PRIVATE src/content/character_progression.cpp)
target_sources(d2x_content PRIVATE src/content/character_attributes.cpp)
target_sources(d2x_content PRIVATE src/content/string_table.cpp src/content/skill_data.cpp)
target_sources(d2x_content PRIVATE src/content/sorceress_data.cpp src/content/frozen_orb_data.cpp src/content/blizzard_data.cpp src/content/weapon_skill_data.cpp src/content/necromancer_data.cpp)
target_sources(d2x_content PRIVATE src/content/skill_animation.cpp)
target_sources(d2x_content PRIVATE src/content/item_projectiles.cpp src/content/missile_effects.cpp)
target_sources(d2x_content PRIVATE src/content/monster_experience.cpp)
target_sources(d2x_content PRIVATE src/content/monster_combat.cpp)
target_sources(d2x_content PRIVATE src/content/monster_difficulty_combat.cpp src/content/monster_enchantment.cpp)
target_sources(d2x_content PRIVATE src/content/monster_ai_data.cpp)
target_sources(d2x_content PRIVATE src/content/monster_spell_data.cpp)
target_sources(d2x_content PRIVATE src/content/monster_animation.cpp)
target_sources(d2x_content PRIVATE src/content/equipment_modifiers.cpp)
target_sources(d2x_content PRIVATE src/content/equipment_combat.cpp)
target_sources(d2x_content PRIVATE src/content/equipment_data.cpp)
target_sources(d2x_content PRIVATE src/content/item_appearance.cpp)
target_sources(d2x_content PRIVATE src/content/monster_loot.cpp)
target_sources(d2x_content PRIVATE src/content/object_loot.cpp)
target_sources(d2x_content PRIVATE src/content/chest_loot.cpp)
target_sources(d2x_content PRIVATE src/content/item_quality.cpp)
target_sources(d2x_content PRIVATE src/content/item_loot.cpp)
target_sources(d2x_content PRIVATE src/content/special_items.cpp)
target_sources(d2x_content PRIVATE src/content/item_affixes.cpp)
target_sources(d2x_content PRIVATE src/content/item_properties.cpp)
target_sources(d2x_content PRIVATE src/content/item_descriptions.cpp src/content/item_pricing.cpp)
target_sources(d2x_content PRIVATE src/content/item_consumables.cpp)
target_sources(d2x_content PRIVATE src/content/item_magic_loot.cpp)
target_sources(d2x_content PRIVATE src/content/item_grades.cpp)
target_sources(d2x_content PRIVATE src/content/npc_dialogue.cpp)
target_sources(d2x_content PRIVATE src/content/hireling_data.cpp)
target_sources(d2x_content PRIVATE src/content/automap_data.cpp)
target_sources(d2x_content PRIVATE src/content/vendor_data.cpp)
target_sources(d2x_content PRIVATE src/content/gamble_stock.cpp)
target_link_libraries(d2x_content PUBLIC d2x_resources d2x_items)

# Rules and state compile without raylib, StormLib, a window or an archive.
add_library(d2x_gameplay
  src/gameplay/model/definitions.cpp src/gameplay/simulation/simulation.cpp src/gameplay/player/movement.cpp
  src/gameplay/combat/combat.cpp src/gameplay/combat/stat_modifiers.cpp
  src/gameplay/combat/unit.cpp
  src/gameplay/combat/damage_resolution.cpp src/gameplay/combat/player_damage.cpp
  src/gameplay/combat/weapon_elements.cpp
  src/gameplay/skills/casting.cpp src/gameplay/skills/summoning.cpp src/gameplay/skills/frozen_orb.cpp src/gameplay/skills/blizzard.cpp
  src/gameplay/monsters/monster_ai.cpp src/gameplay/monsters/monster_melee.cpp
  src/gameplay/monsters/monster_special_ai.cpp
    src/gameplay/monsters/monster_resurrection.cpp
    src/gameplay/monsters/monster_nest.cpp
    src/gameplay/monsters/blood_hawk_ai.cpp
    src/gameplay/monsters/arach_web.cpp
    src/gameplay/monsters/arach_ai.cpp
  src/gameplay/monsters/monster_element.cpp src/gameplay/monsters/monster_enchantments.cpp
  src/gameplay/monsters/skeleton_ai.cpp src/gameplay/monsters/brute_ai.cpp
  src/gameplay/monsters/corrupt_rogue_ai.cpp
  src/gameplay/monsters/goatman_ai.cpp
  src/gameplay/monsters/quill_rat_ai.cpp
  src/gameplay/monsters/wraith_ai.cpp
  src/gameplay/monsters/corrupt_lancer_ai.cpp
  src/gameplay/monsters/corrupt_archer_ai.cpp
  src/gameplay/monsters/skeleton_bow_ai.cpp
  src/gameplay/monsters/bighead_ai.cpp
  src/gameplay/monsters/skeleton_mage_ai.cpp
  src/gameplay/monsters/fetish_ai.cpp
  src/gameplay/monsters/vampire_ai.cpp
  src/gameplay/monsters/fallen_shaman_ai.cpp
  src/gameplay/monsters/zombie_ai.cpp
  src/gameplay/monsters/fallen_ai.cpp src/gameplay/monsters/monster_wander.cpp
  src/gameplay/loot/loot.cpp src/gameplay/consumables/potions.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/character/attributes.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/combat/accuracy.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/combat/physical_projectiles.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/combat/weapon_attack.cpp src/gameplay/combat/attacking.cpp
  src/gameplay/combat/missile_effects.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/skills/resolve.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/monsters/monster_spawn.cpp src/gameplay/monsters/monster_activation.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/loot/treasure.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/loot/quality.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/loot/special.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/quest/den_of_evil.cpp src/gameplay/quest/burial_grounds.cpp src/gameplay/quest/search_for_cain.cpp src/gameplay/quest/forgotten_tower.cpp src/gameplay/quest/tools_of_trade.cpp src/gameplay/quest/sisters_to_slaughter.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/loot/affix.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/effects/state.cpp src/gameplay/effects/reactions.cpp)
target_sources(d2x_content PRIVATE src/content/state_data.cpp src/content/shrine_data.cpp)
target_link_libraries(d2x_gameplay PUBLIC d2x_navigation)
target_link_libraries(d2x_content PUBLIC d2x_gameplay)
add_library(d2x_population src/world/population.cpp)
target_link_libraries(d2x_population PUBLIC d2x_content d2x_gameplay)
add_library(d2x_session src/gameplay/session/session.cpp src/gameplay/session/session_skills.cpp src/gameplay/session/session_loot.cpp src/gameplay/session/session_inventory.cpp src/gameplay/session/session_consumables.cpp src/gameplay/session/session_gold.cpp src/gameplay/session/session_interaction.cpp src/gameplay/session/session_restore.cpp src/gameplay/session/session_exits.cpp src/gameplay/session/session_monster_combat.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_objects.cpp src/gameplay/session/session_shrines.cpp)
target_sources(d2x_session PRIVATE src/gameplay/npc/identification.cpp src/gameplay/npc/service.cpp src/gameplay/npc/movement.cpp)
target_sources(d2x_session PRIVATE src/gameplay/npc/store.cpp src/gameplay/npc/purchase.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_item_snapshot.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_character_save.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_debug_monsters.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_quests.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_quest_dialogue.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_cain_world.cpp src/gameplay/session/session_cain_items.cpp src/gameplay/session/session_tools.cpp src/gameplay/session/session_slaughter.cpp)
target_sources(d2x_session PRIVATE src/gameplay/npc/hireling.cpp)
target_sources(d2x_session PRIVATE src/gameplay/npc/hireling_services.cpp)
target_link_libraries(d2x_session PUBLIC d2x_gameplay d2x_world d2x_content d2x_population)
add_library(d2x_persistence src/persistence/d2s_codec.cpp src/persistence/save_file.cpp
  src/persistence/d2s_header.cpp src/persistence/d2s_fixed_sections.cpp
  src/persistence/d2s_stats.cpp
  src/persistence/d2s_items.cpp
  src/persistence/d2s_inventory.cpp
  src/persistence/d2s_skills.cpp)
target_link_libraries(d2x_persistence PUBLIC d2x_gameplay d2x_items d2x_content)

add_library(d2x_presentation
  src/presentation/graphics.cpp src/presentation/audio.cpp src/presentation/primitives.cpp
  src/presentation/scene_assets.cpp src/presentation/hero_assets.cpp src/presentation/scene_view.cpp
  src/presentation/skill_assets.cpp src/presentation/hud_controls.cpp src/presentation/skill_controller.cpp
  src/presentation/skill_tree_view.cpp src/presentation/quest_panel.cpp
  src/presentation/npc_alert_view.cpp src/presentation/panel_frame.cpp
  src/presentation/exit_view.cpp src/presentation/waypoint_view.cpp
  src/presentation/world_renderer.cpp src/presentation/hud.cpp src/presentation/npc_dialogue_view.cpp src/presentation/npc_menu_view.cpp src/presentation/npc_shop_view.cpp src/presentation/controller.cpp
  src/presentation/loot_view.cpp src/presentation/inventory_panel.cpp
  src/presentation/character_panel.cpp src/presentation/character_action_stats.cpp
  src/presentation/container_view.cpp src/presentation/storage_view.cpp src/presentation/cube_view.cpp src/presentation/belt_view.cpp src/presentation/inventory_view.cpp src/presentation/inventory_controller.cpp)
target_link_libraries(d2x_presentation PUBLIC d2x_session raylib)
target_sources(d2x_presentation PRIVATE src/presentation/monster_audio.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/monster_assets.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/hireling_assets.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/hireling_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/automap_assets.cpp src/presentation/automap_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/lighting_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/palette_blend_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/audio_emitters.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/projectile_view.cpp)

add_executable(d2x src/main.cpp src/app/application.cpp src/app/options.cpp src/app/input.cpp
  src/app/character_frontend.cpp src/app/crash_report.cpp)
target_sources(d2x PRIVATE src/app/debug_pipe.cpp src/app/debug_commands.cpp src/app/debug_inventory.cpp
    src/app/debug_item_actions.cpp src/app/debug_monsters.cpp src/app/debug_hireling.cpp)
target_link_libraries(d2x PRIVATE d2x_presentation d2x_persistence)
target_link_libraries(d2x PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(d2x PRIVATE advapi32 dbghelp)
endif()
add_executable(d2x_assets src/asset_tool.cpp)
target_link_libraries(d2x_assets PRIVATE d2x_content d2x_world d2x_population d2x_persistence raylib)
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
