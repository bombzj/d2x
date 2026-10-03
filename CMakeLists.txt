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
add_library(d2x_world src/world/map.cpp src/world/map_assembly.cpp src/world/maze.cpp src/world/outdoor/outdoor.cpp src/world/outdoor/outdoor_layout.cpp src/world/region.cpp src/world/region_catalog.cpp src/world/exits.cpp src/world/world_report.cpp)
target_sources(d2x_world PRIVATE src/world/object_population.cpp)
target_sources(d2x_world PRIVATE src/world/chest.cpp)
target_sources(d2x_world PRIVATE src/world/shrine_catalog.cpp)
target_sources(d2x_world PRIVATE
  src/world/outdoor/outdoor_substitution.cpp
  src/world/outdoor/outdoor_shrines.cpp
  src/world/outdoor/outdoor_river.cpp
  src/world/outdoor/outdoor_paths.cpp
  src/world/outdoor/outdoor_cliffs.cpp
  src/world/outdoor/desert.cpp
  src/world/outdoor/jungle.cpp
  src/world/outdoor/mesa.cpp
  src/world/outdoor/barricade.cpp
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
add_library(d2x_content src/content/classic_data.cpp src/content/lod_data.cpp src/content/world/world_catalog.cpp src/content/monsters/monster_catalog.cpp)
target_sources(d2x_content PRIVATE src/content/character/character_progression.cpp)
target_sources(d2x_content PRIVATE src/content/character/character_attributes.cpp)
target_sources(d2x_content PRIVATE src/content/string_table.cpp src/content/skills/skill_data.cpp)
target_sources(d2x_content PRIVATE src/content/skills/sorceress_data.cpp src/content/skills/frozen_orb_data.cpp src/content/skills/blizzard_data.cpp src/content/skills/glacial_spike_data.cpp src/content/skills/weapon_skill_data.cpp src/content/skills/necromancer_data.cpp)
target_sources(d2x_content PRIVATE src/content/skills/skill_animation.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_projectiles.cpp src/content/skills/missile_effects.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_experience.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_combat.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_difficulty_combat.cpp src/content/monsters/monster_enchantment.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_ai_data.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_spell_data.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_animation.cpp)
target_sources(d2x_content PRIVATE src/content/items/equipment_modifiers.cpp)
target_sources(d2x_content PRIVATE src/content/items/equipment_combat.cpp)
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
target_sources(d2x_content PRIVATE src/content/npc/hireling_data.cpp)
target_sources(d2x_content PRIVATE src/content/world/automap_data.cpp)
target_sources(d2x_content PRIVATE src/content/npc/vendor_data.cpp)
target_sources(d2x_content PRIVATE src/content/npc/gamble_stock.cpp)
target_link_libraries(d2x_content PUBLIC d2x_resources d2x_items)

# Rules and state compile without raylib, StormLib, a window or an archive.
add_library(d2x_gameplay
  src/gameplay/model/definitions.cpp src/gameplay/simulation/simulation.cpp src/gameplay/player/movement.cpp
  src/gameplay/combat/combat.cpp src/gameplay/combat/stat_modifiers.cpp
  src/gameplay/combat/unit.cpp
  src/gameplay/combat/damage_resolution.cpp src/gameplay/combat/player_damage.cpp
  src/gameplay/combat/weapon_elements.cpp
  src/gameplay/skills/casting.cpp src/gameplay/skills/release.cpp src/gameplay/skills/projectile_launch.cpp
  src/gameplay/skills/curse_runtime.cpp src/gameplay/skills/utility_effects.cpp
  src/gameplay/skills/native_effects.cpp src/gameplay/skills/native_projectiles.cpp src/gameplay/skills/firewall.cpp
  src/gameplay/skills/weapon_runtime.cpp src/gameplay/skills/weapon_contributions.cpp src/gameplay/skills/charge.cpp src/gameplay/skills/applied_effects.cpp src/gameplay/skills/direct_damage.cpp src/gameplay/skills/heaven.cpp src/gameplay/skills/holy_bolt.cpp
  src/gameplay/skills/missile_rules.cpp src/gameplay/skills/missile_dispatch.cpp src/gameplay/skills/runtime.cpp src/gameplay/skills/elemental_runtime.cpp
  src/gameplay/simulation/unit_records.cpp
  src/gameplay/simulation/skill_world.cpp src/gameplay/combat/cold_effects.cpp src/gameplay/combat/knockback.cpp src/gameplay/monsters/companions.cpp src/gameplay/skills/frozen_orb.cpp src/gameplay/skills/blizzard.cpp src/gameplay/skills/arc.cpp src/gameplay/skills/meteor.cpp src/gameplay/skills/glacial_spike.cpp src/gameplay/skills/chilling_armor.cpp
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
target_sources(d2x_gameplay PRIVATE src/gameplay/combat/weapon_attack.cpp src/gameplay/combat/geometry.cpp src/gameplay/combat/attacking.cpp
  src/gameplay/combat/missile_effects.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/skills/resolve.cpp src/gameplay/skills/source.cpp
  src/gameplay/skills/summon_resolve.cpp src/gameplay/skills/projectile_path.cpp
  src/gameplay/skills/damage_curve.cpp src/gameplay/skills/rank_bonus.cpp src/gameplay/skills/aura_resolve.cpp src/gameplay/skills/passive.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/skills/aura.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/monsters/implementation.cpp src/gameplay/monsters/rank.cpp src/gameplay/monsters/monster_activation.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/loot/treasure.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/loot/quality.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/loot/special.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/quest/den_of_evil.cpp src/gameplay/quest/burial_grounds.cpp src/gameplay/quest/search_for_cain.cpp src/gameplay/quest/forgotten_tower.cpp src/gameplay/quest/tools_of_trade.cpp src/gameplay/quest/sisters_to_slaughter.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/character/runtime_record.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/character/progression.cpp src/gameplay/character/learning.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/loot/affix.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/effects/state.cpp src/gameplay/skills/reactions.cpp src/gameplay/skills/shield.cpp)
target_sources(d2x_content PRIVATE src/content/skills/state_data.cpp src/content/world/shrine_data.cpp
  src/content/skills/aura_data.cpp src/content/skills/passive_data.cpp)
target_link_libraries(d2x_gameplay PUBLIC d2x_navigation)
target_link_libraries(d2x_content PUBLIC d2x_gameplay)
add_library(d2x_population src/world/population.cpp)
target_link_libraries(d2x_population PUBLIC d2x_content d2x_gameplay)
add_library(d2x_session src/gameplay/session/session.cpp src/gameplay/session/session_skills.cpp src/gameplay/session/session_loot.cpp src/gameplay/session/session_inventory.cpp src/gameplay/session/session_consumables.cpp src/gameplay/session/session_gold.cpp src/gameplay/session/session_interaction.cpp src/gameplay/session/session_restore.cpp src/gameplay/session/session_exits.cpp src/gameplay/session/session_monster_combat.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_facade.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_objects.cpp src/gameplay/session/session_shrines.cpp)
target_sources(d2x_session PRIVATE src/gameplay/npc/identification.cpp src/gameplay/npc/service.cpp src/gameplay/npc/movement.cpp)
target_sources(d2x_session PRIVATE src/gameplay/npc/store.cpp src/gameplay/npc/purchase.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_item_snapshot.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_character_save.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_debug_monsters.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_quests.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_act_two.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_quest_dialogue.cpp)
target_sources(d2x_session PRIVATE src/gameplay/session/session_cain_world.cpp src/gameplay/session/session_cain_items.cpp src/gameplay/session/session_tools.cpp src/gameplay/session/session_slaughter.cpp)
target_sources(d2x_session PRIVATE src/gameplay/npc/hireling.cpp)
target_sources(d2x_session PRIVATE src/gameplay/npc/hireling_services.cpp)
target_link_libraries(d2x_session PUBLIC d2x_gameplay d2x_world d2x_content d2x_population)
target_sources(d2x_session PRIVATE src/gameplay/session/session_character.cpp
  src/gameplay/session/session_skill_sources.cpp)
add_library(d2x_persistence src/persistence/d2s_codec.cpp src/persistence/save_file.cpp
  src/persistence/d2s_header.cpp src/persistence/d2s_fixed_sections.cpp
  src/persistence/d2s_stats.cpp
  src/persistence/d2s_items.cpp
  src/persistence/d2s_inventory.cpp
  src/persistence/d2s_skills.cpp)
target_link_libraries(d2x_persistence PUBLIC d2x_gameplay d2x_items d2x_content)

add_library(d2x_client_api INTERFACE)
target_link_libraries(d2x_client_api INTERFACE d2x_core)
add_library(d2x_client src/client/inventory_view.cpp)
target_link_libraries(d2x_client PUBLIC d2x_client_api)
add_library(d2x_local_client src/client/local_actor_client.cpp src/client/local_inventory_client.cpp
  src/client/local_character_client.cpp src/client/local_quest_client.cpp src/client/local_npc_client.cpp)
target_link_libraries(d2x_local_client PUBLIC d2x_client PRIVATE d2x_session)
target_sources(d2x_content PRIVATE src/content/items/item_display.cpp src/content/character/character_display.cpp)

add_library(d2x_presentation
  src/presentation/graphics/graphics.cpp src/presentation/audio/audio.cpp src/presentation/graphics/primitives.cpp
  src/presentation/scene_assets.cpp src/presentation/actors/hero_assets.cpp src/presentation/scene_view.cpp
  src/presentation/hud/skill_assets.cpp src/presentation/hud/hud_controls.cpp src/presentation/hud/skill_controller.cpp
  src/presentation/hud/skill_tree_view.cpp src/presentation/hud/quest_panel.cpp
  src/presentation/hud/quest_controller.cpp src/presentation/npc/npc_controller.cpp
  src/presentation/actors/state_overlay_view.cpp
  src/presentation/npc/npc_alert_view.cpp src/presentation/hud/panel_frame.cpp
  src/presentation/hud/exit_view.cpp src/presentation/hud/waypoint_view.cpp
  src/presentation/world/world_renderer.cpp src/presentation/hud/hud.cpp src/presentation/npc/npc_dialogue_view.cpp src/presentation/npc/npc_menu_view.cpp src/presentation/npc/npc_shop_view.cpp src/presentation/controller.cpp
  src/presentation/loot_view.cpp src/presentation/inventory/inventory_panel.cpp
  src/presentation/hud/character_panel.cpp
  src/presentation/inventory/container_view.cpp src/presentation/inventory/storage_view.cpp src/presentation/inventory/cube_view.cpp src/presentation/inventory/belt_view.cpp src/presentation/inventory/inventory_view.cpp src/presentation/inventory/inventory_controller.cpp)
target_link_libraries(d2x_presentation PUBLIC d2x_client d2x_session raylib)
target_sources(d2x_presentation PRIVATE src/presentation/actors/monster_audio.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/actors/monster_assets.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/actors/hireling_assets.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/npc/hireling_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/automap_assets.cpp src/presentation/world/automap_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/lighting_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/palette_blend_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/audio/audio_emitters.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/projectile_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/inventory/item_tooltip.cpp
  src/presentation/items/item_display_compat.cpp src/presentation/npc/inventory_interactions.cpp)

add_executable(d2x src/main.cpp src/app/application.cpp src/app/options.cpp src/app/input.cpp
  src/app/character_frontend.cpp src/app/crash_report.cpp)
target_sources(d2x PRIVATE src/app/debug/debug_pipe.cpp src/app/debug/debug_commands.cpp src/app/debug/debug_inventory.cpp
    src/app/debug/debug_item_actions.cpp src/app/debug/debug_monsters.cpp src/app/debug/debug_hireling.cpp)
target_link_libraries(d2x PRIVATE d2x_presentation d2x_persistence d2x_local_client)
target_link_libraries(d2x PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(d2x PRIVATE advapi32 dbghelp)
endif()
add_executable(d2x_assets src/asset_tool.cpp)
target_link_libraries(d2x_assets PRIVATE d2x_content d2x_world d2x_population d2x_persistence raylib)
foreach(target IN ITEMS d2x_resources d2x_navigation d2x_world d2x_items d2x_content d2x_gameplay d2x_population d2x_session d2x_persistence d2x_presentation d2x_client d2x_local_client d2x d2x_assets)
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
