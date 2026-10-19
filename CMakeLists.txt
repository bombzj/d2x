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

add_library(d2x_navigation src/world/navigation.cpp src/world/navigation_astar.cpp src/world/navigation_wall_follow.cpp src/world/native_path_step.cpp src/world/room_activation.cpp src/world/native_room_activation.cpp)
target_link_libraries(d2x_navigation PUBLIC d2x_core PRIVATE d2x_rules)
add_library(d2x_world src/world/map.cpp src/world/map_assembly.cpp src/world/maze.cpp src/world/outdoor/outdoor.cpp src/world/region.cpp src/world/region_catalog.cpp src/world/exits.cpp src/world/world_report.cpp)
target_sources(d2x_world PRIVATE src/world/object_population.cpp)
target_sources(d2x_world PRIVATE src/world/chest.cpp)
target_sources(d2x_world PRIVATE src/world/shrine_catalog.cpp)
target_sources(d2x_world PRIVATE src/world/native_map.cpp)
target_sources(d2x_world PRIVATE src/world/generated_area.cpp)
target_sources(d2x_world PRIVATE src/world/room_terrain.cpp)
target_sources(d2x_world PRIVATE src/world/outdoor/native_act_layout.cpp src/world/retail/outdoor_grid.cpp
  src/world/outdoor/native_jungle_layout.cpp src/world/outdoor/native_connections.cpp src/world/retail/boundary.cpp
  src/world/retail/border_presets.cpp src/world/retail/secondary_border.cpp
  src/world/retail/wilderness.cpp src/world/retail/desert.cpp src/world/retail/jungle.cpp
  src/world/retail/mesa.cpp src/world/retail/barricade.cpp src/world/retail/dirt_paths.cpp
  src/world/retail/outdoor_layout.cpp src/world/retail/rooms.cpp
  src/world/retail/preset_scan.cpp src/world/retail/room_grids.cpp
  src/world/retail/room_themes.cpp src/world/retail/tile_selector.cpp
  src/world/retail/room_data.cpp src/world/retail/preset_room.cpp
  src/world/retail/tile_materialization.cpp src/world/retail/room_logicals.cpp)
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
add_library(d2x_rules
  src/gameplay/units/direction.cpp
  src/gameplay/rules/arithmetic.cpp
  src/gameplay/rules/formula.cpp
  src/gameplay/stats/stat_list.cpp)
target_sources(d2x_rules PRIVATE src/gameplay/stats/modifier_values.cpp)
target_sources(d2x_rules PRIVATE src/gameplay/combat/unit_values.cpp)
target_sources(d2x_rules PRIVATE src/gameplay/combat/weapon_snapshot.cpp)
target_sources(d2x_rules PRIVATE src/gameplay/combat/drain.cpp)
target_link_libraries(d2x_rules PUBLIC d2x_core)
add_library(d2x_items src/gameplay/items/definitions.cpp
  src/gameplay/items/stat_evaluation.cpp
  src/gameplay/items/property_program.cpp
  src/gameplay/items/skill_sources.cpp
  src/gameplay/items/inventory_error.cpp src/gameplay/items/equipment_rules.cpp src/gameplay/items/equipment_combat.cpp)
target_link_libraries(d2x_items PUBLIC d2x_core d2x_rules)
add_library(d2x_content src/content/classic_data.cpp src/content/lod_data.cpp src/content/world/world_catalog.cpp src/content/monsters/monster_catalog.cpp)
target_sources(d2x_content PRIVATE
    src/content/skills/description_formula.cpp src/content/skills/description_data.cpp
    src/content/skills/formula_data.cpp
    src/content/skills/description_values.cpp src/content/skills/skill_description.cpp)
target_sources(d2x_content PRIVATE src/content/character/character_progression.cpp)
target_sources(d2x_content PRIVATE src/content/character/character_attributes.cpp)
target_sources(d2x_content PRIVATE src/content/character/death_data.cpp)
target_sources(d2x_content PRIVATE src/content/language.cpp src/content/string_table.cpp src/content/skills/skill_data.cpp
  src/content/skills/skill_eligibility.cpp src/content/skills/table_values.cpp)
target_sources(d2x_content PRIVATE src/content/skills/defensive_buff_data.cpp src/content/skills/sorceress_data.cpp src/content/skills/frozen_orb_data.cpp src/content/skills/blizzard_data.cpp src/content/skills/glacial_spike_data.cpp src/content/skills/weapon_skill_data.cpp src/content/skills/necromancer_data.cpp src/content/skills/necro_summon_data.cpp src/content/skills/curse_data.cpp
    src/content/skills/bone_data.cpp src/content/skills/barbarian_data.cpp src/content/skills/amazon_bow_data.cpp src/content/skills/amazon_spear_data.cpp src/content/skills/amazon_magic_data.cpp src/content/skills/amazon_summon_data.cpp)
target_sources(d2x_content PRIVATE src/content/skills/skill_animation.cpp)
target_sources(d2x_content PRIVATE src/content/skills/assassin_data.cpp)
target_sources(d2x_content PRIVATE src/content/skills/druid_data.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_projectiles.cpp src/content/skills/missile_effects.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_experience.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_combat.cpp src/content/monsters/monster_type_data.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/monster_difficulty_combat.cpp src/content/monsters/monster_enchantment.cpp)
target_sources(d2x_content PRIVATE src/content/monsters/mode_stat_data.cpp)
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
target_sources(d2x_content PRIVATE src/content/npc/pet_portrait_data.cpp)
target_sources(d2x_content PRIVATE src/content/world/automap_data.cpp)
target_sources(d2x_content PRIVATE src/content/npc/vendor_data.cpp src/content/npc/vendor_stock.cpp)
target_link_libraries(d2x_content PUBLIC d2x_resources d2x_items)

# Rules and state compile without raylib, StormLib, a window or an archive.
# Pure display, geometry and resource-tool calculations. No simulation/executor.
add_library(d2x_gameplay
  src/gameplay/actions/timing.cpp
  src/gameplay/actions/timeline.cpp
  src/gameplay/actions/monster_timing.cpp
  src/gameplay/actions/recovery.cpp
  src/gameplay/actions/volley.cpp
  src/gameplay/units/distance.cpp
  src/gameplay/effects/queries.cpp
  src/gameplay/skills/use_source.cpp
  src/gameplay/skills/activation_program.cpp
  src/gameplay/skills/assassin_progressive.cpp
  src/gameplay/skills/assassin_summon.cpp
  src/gameplay/skills/druid_summon.cpp
  src/gameplay/skills/native_damage.cpp
  src/gameplay/skills/item_trigger_policy.cpp
  src/gameplay/missiles/damage.cpp
  src/gameplay/missiles/motion.cpp
    src/gameplay/missiles/damage_data.cpp
    src/gameplay/missiles/damage_program.cpp
  src/gameplay/combat/mitigation.cpp
  src/gameplay/combat/shield.cpp
  src/gameplay/combat/reception.cpp
  src/gameplay/combat/scaling.cpp
  src/gameplay/combat/relation.cpp
  src/gameplay/items/equipment_loadout.cpp src/gameplay/items/equipment_requirements.cpp
  src/gameplay/items/equipment_contributions.cpp src/gameplay/items/equipment_stats.cpp
  src/gameplay/items/native_equipment_stats.cpp
  src/gameplay/items/native_sources.cpp
  src/gameplay/character/attributes.cpp src/gameplay/character/skill_rank.cpp
  src/gameplay/combat/stat_modifiers.cpp src/gameplay/combat/geometry.cpp
  src/gameplay/combat/attack_element_ranges.cpp src/gameplay/combat/weapon_hit.cpp
  src/gameplay/combat/accuracy.cpp src/gameplay/combat/damage_resolution.cpp src/gameplay/actions/weapon_timing.cpp src/gameplay/actions/shape_timing.cpp
  src/gameplay/combat/poison.cpp
  src/gameplay/skills/cast_timing.cpp src/gameplay/skills/resolve.cpp src/gameplay/skills/curse_resolve.cpp
  src/gameplay/skills/summon_resolve.cpp src/gameplay/skills/amazon_summon_resolve.cpp
  src/gameplay/skills/projectile_path.cpp src/gameplay/skills/damage_curve.cpp
  src/gameplay/skills/formula_parameters.cpp
  src/gameplay/skills/mana_payment.cpp
  src/gameplay/skills/passive_stats.cpp
  src/gameplay/monsters/mode_stats.cpp
  src/gameplay/monsters/life_stats.cpp
  src/gameplay/items/equipment_activation.cpp
  src/gameplay/items/weapon_registration.cpp
    src/gameplay/items/inventory_order.cpp
  src/gameplay/environment/time.cpp
  src/gameplay/skills/rank_sources.cpp src/gameplay/skills/rank_bonus.cpp src/gameplay/skills/aura_resolve.cpp
    src/gameplay/skills/stat_callbacks.cpp
    src/gameplay/skills/equipment.cpp
  src/gameplay/skills/passive.cpp src/gameplay/skills/targeting.cpp src/gameplay/skills/weapon_damage.cpp src/gameplay/skills/amazon_magic.cpp
  src/gameplay/monsters/melee_decision.cpp src/gameplay/monsters/skirmish_decision.cpp
  src/gameplay/monsters/act2_decision.cpp
  src/gameplay/monsters/curse_decision.cpp
    src/gameplay/monsters/ranged_decision.cpp
    src/gameplay/monsters/special_decision.cpp
    src/gameplay/monsters/shaman_decision.cpp src/gameplay/monsters/movement_math.cpp src/gameplay/monsters/projectile_math.cpp src/gameplay/monsters/damage.cpp src/gameplay/monsters/components.cpp src/gameplay/monsters/region_generation.cpp src/gameplay/monsters/implementation.cpp src/gameplay/monsters/rank.cpp
  src/gameplay/loot/treasure.cpp src/gameplay/loot/quality.cpp
  src/gameplay/loot/special.cpp src/gameplay/loot/affix.cpp
  src/gameplay/rewards/experience.cpp)
target_link_libraries(d2x_gameplay PUBLIC d2x_navigation d2x_items d2x_rules)
target_sources(d2x_content PRIVATE src/content/skills/state_data.cpp src/content/world/shrine_data.cpp
  src/content/skills/aura_data.cpp)
target_link_libraries(d2x_content PUBLIC d2x_gameplay)
target_sources(d2x_navigation PRIVATE src/world/interaction_geometry.cpp)

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
# Authority and scheduling remain headless: no archive, graphics or sockets.
add_library(d2x_server src/server/diagnostics.cpp src/server/game_instance.cpp src/server/movement.cpp src/server/player_projection.cpp src/server/character_admission.cpp)
target_sources(d2x_server PRIVATE
  src/server/player_store.cpp
  src/server/runtime/system_catalog.cpp
  src/server/runtime/game_systems.cpp
  src/server/runtime/command_dispatch.cpp
  src/server/runtime/simulation.cpp
  src/server/systems/actions/state.cpp
  src/server/runtime/events.cpp
  src/server/systems/world/system.cpp
    src/server/systems/world/quest_placement.cpp
    src/server/systems/quests/orb.cpp
    src/server/systems/effects/redemption.cpp
    src/server/systems/effects/damage_absorption.cpp
  src/server/systems/world/rooms.cpp
  src/server/area_store.cpp
  src/server/systems/spatial/system.cpp
  src/server/systems/items/system.cpp
  src/server/systems/inventory/system.cpp
  src/server/systems/inventory/planning.cpp
  src/server/systems/inventory/placement.cpp
  src/server/systems/inventory/equipment.cpp
  src/server/systems/inventory/quantities.cpp
  src/server/systems/inventory/eligibility.cpp
  src/server/systems/attributes/system.cpp
  src/server/systems/attributes/calculation.cpp
    src/server/systems/attributes/skill_refresh.cpp
  src/server/systems/attributes/stat_sources.cpp
  src/server/systems/attributes/monster_sources.cpp
  src/server/systems/crafting/system.cpp
  src/server/systems/loot/system.cpp
  src/server/systems/population/system.cpp
  src/server/systems/monsters/system.cpp
    src/server/systems/monsters/inactive.cpp
    src/server/systems/monsters/special_actions.cpp
    src/server/systems/monsters/act2_skills.cpp
    src/server/systems/monsters/diablo_prison.cpp
    src/server/systems/monsters/imp.cpp
    src/server/systems/monsters/leap.cpp
    src/server/systems/monsters/controlled.cpp
  src/server/systems/ai/system.cpp src/server/systems/ai/family_actions.cpp
    src/server/systems/ai/act2_actions.cpp
    src/server/systems/ai/nest_family.cpp
    src/server/systems/ai/vulture_family.cpp
    src/server/systems/ai/frog_family.cpp
    src/server/systems/ai/willowisp_family.cpp
    src/server/systems/ai/zakarum_zealot.cpp
    src/server/systems/ai/zakarum_priest.cpp
    src/server/systems/ai/high_priest.cpp
    src/server/systems/ai/vile_family.cpp
    src/server/systems/ai/finger_mage.cpp
    src/server/systems/ai/regurgitator.cpp
    src/server/systems/ai/abyss_knight.cpp
    src/server/systems/ai/oblivion_knight.cpp
    src/server/systems/ai/siege_beast.cpp
    src/server/systems/ai/imp.cpp
    src/server/systems/ai/generic_spawner.cpp
    src/server/systems/ai/minion.cpp
    src/server/systems/ai/suicide_minion.cpp
    src/server/systems/ai/overseer.cpp
    src/server/systems/ai/minion_spawner.cpp
    src/server/systems/ai/succubus.cpp
    src/server/systems/ai/izual.cpp
    src/server/systems/ai/mephisto.cpp
    src/server/systems/ai/diablo.cpp
    src/server/systems/ai/ancient_talic.cpp
    src/server/systems/ai/ancient_korlic.cpp
    src/server/systems/ai/evil_hole.cpp
    src/server/systems/ai/tentacle.cpp
    src/server/systems/ai/baal_tentacle.cpp
    src/server/systems/ai/baal.cpp
    src/server/systems/ai/baal_to_stairs.cpp
    src/server/systems/ai/baal_throne.cpp
    src/server/systems/ai/baal_taunt.cpp
    src/server/systems/ai/hell_meteor.cpp
    src/server/systems/ai/trapped_soul.cpp
    src/server/systems/ai/catapult.cpp
    src/server/systems/ai/npc_barb.cpp
    src/server/systems/ai/catapult_spotter.cpp
    src/server/systems/ai/ancient_madawc.cpp
    src/server/systems/ai/nihlathak.cpp
    src/server/systems/ai/putrid_defiler.cpp
    src/server/systems/ai/baal_minion.cpp
    src/server/systems/ai/frozen_horror.cpp
    src/server/systems/ai/reanimated_horde.cpp
    src/server/systems/ai/megademon.cpp
    src/server/systems/monsters/pregnancy.cpp
    src/server/systems/monsters/hydra.cpp
  src/server/systems/skills/system.cpp src/server/systems/skills/monster_actions.cpp src/server/systems/skills/casting.cpp
  src/server/systems/skills/action_events.cpp
  src/server/systems/skills/monster_action_events.cpp
  src/server/systems/skills/monster_inferno.cpp
  src/server/systems/skills/monster_spell.cpp
  src/server/systems/skills/imp.cpp
    src/server/systems/skills/monster_mosquito.cpp
  src/server/systems/skills/monster_leap.cpp
  src/server/systems/skills/monster_run.cpp
  src/server/systems/skills/monster_whirlwind.cpp
  src/server/systems/skills/monster_leap_attack.cpp
    src/server/systems/skills/activation.cpp
    src/server/systems/skills/shape.cpp
    src/server/systems/skills/druid_active.cpp
    src/server/systems/skills/druid_pet.cpp
    src/server/systems/skills/druid_melee.cpp
    src/server/systems/skills/weapon_melee.cpp
    src/server/systems/skills/druid.cpp
    src/server/systems/skills/assassin.cpp
    src/server/systems/skills/assassin_pet.cpp
    src/server/systems/skills/assassin_kick.cpp
    src/server/systems/skills/native_caster.cpp
    src/server/systems/ai/target_query.cpp
    src/server/systems/companions/native_summon.cpp
    src/server/systems/companions/native_pet_move.cpp
    src/server/systems/companions/druid_corpse.cpp
    src/server/systems/companions/druid_ai.cpp
    src/server/systems/companions/assassin_ai.cpp
    src/server/systems/companions/shadow_ai.cpp
    src/server/systems/effects/corpse_explosion.cpp
    src/server/systems/effects/native_area.cpp
    src/server/systems/combat/native_skill_contact.cpp
    src/server/systems/combat/native_smite.cpp
    src/server/systems/combat/monster_melee.cpp
    src/server/systems/combat/progressive_contact.cpp
    src/server/systems/missiles/native_weapon_contact.cpp
    src/server/systems/effects/druid.cpp
    src/server/systems/effects/state_group.cpp
    src/server/systems/effects/assassin.cpp
    src/server/systems/effects/blade_shield.cpp
    src/server/systems/effects/mind_blast.cpp
    src/server/systems/skills/evaluation.cpp
    src/server/systems/skills/weapon.cpp src/server/systems/inventory/weapon_cost.cpp src/server/systems/inventory/replenishment.cpp
  src/server/systems/missiles/system.cpp
    src/server/systems/missiles/launch.cpp src/server/systems/missiles/monster.cpp
    src/server/systems/missiles/direct.cpp
    src/server/systems/missiles/programs.cpp
    src/server/systems/missiles/weapon.cpp
  src/server/systems/effects/system.cpp
  src/server/systems/effects/contact.cpp
  src/server/systems/effects/paladin.cpp
  src/server/systems/effects/hirelings.cpp
    src/server/systems/effects/skills.cpp
    src/server/systems/effects/amazon.cpp
    src/server/systems/effects/necro.cpp
    src/server/systems/effects/units.cpp
    src/server/systems/effects/periodic.cpp
    src/server/systems/reactions/item_triggers.cpp
  src/server/systems/combat/system.cpp src/server/systems/combat/spell_damage.cpp src/server/systems/combat/missile_contact.cpp src/server/systems/combat/missile_drain.cpp src/server/systems/effects/missile_delay.cpp
  src/server/systems/combat/player_damage.cpp
  src/server/systems/combat/participants.cpp
  src/server/systems/combat/weapon_hit.cpp
  src/server/systems/combat/environment.cpp
  src/server/systems/combat/follow_up.cpp
  src/server/systems/death/system.cpp
  src/server/systems/death/rewards.cpp
  src/server/systems/companions/system.cpp
  src/server/systems/companions/lifecycle.cpp
  src/server/systems/companions/hireling_control.cpp
    src/server/systems/companions/hireling_equipment.cpp
  src/server/systems/companions/amazon.cpp
    src/server/systems/companions/necro.cpp
  src/server/systems/companions/amazon_ai.cpp
  src/server/systems/objects/system.cpp
  src/server/systems/objects/frame_events.cpp
  src/server/systems/objects/population.cpp
  src/server/systems/objects/shrines.cpp
  src/server/systems/objects/hazards.cpp
  src/server/systems/objects/traps.cpp
  src/server/systems/objects/tower.cpp
  src/server/systems/npc/system.cpp
  src/server/systems/merchant/system.cpp
  src/server/systems/quests/system.cpp
  src/server/systems/quests/act_one.cpp
  src/server/systems/quests/progress.cpp
  src/server/systems/quests/act_two.cpp
  src/server/systems/quests/act_three.cpp
  src/server/systems/quests/act_four.cpp
  src/server/systems/quests/act_five.cpp
  src/server/systems/quests/ancients.cpp
  src/server/systems/quests/baal.cpp
  src/server/systems/quests/act_two_dialogue.cpp
  src/server/systems/quests/act_two_objects.cpp
  src/server/systems/quests/staff.cpp
  src/server/systems/quests/rewards.cpp
  src/server/systems/progression/system.cpp
  src/server/systems/travel/system.cpp src/server/systems/travel/position_sync.cpp src/server/systems/travel/waypoints.cpp src/server/systems/travel/portals.cpp
  src/server/systems/social/system.cpp
  src/server/systems/social/party.cpp
  src/server/systems/trade/system.cpp
  src/server/systems/transactions/system.cpp
    src/server/systems/transactions/character_group.cpp src/server/systems/transactions/resources.cpp
  src/server/systems/replication/system.cpp
)
target_link_libraries(d2x_server PUBLIC d2x_gameplay d2x_items d2x_navigation)
add_library(d2x_host src/hosting/game_host.cpp src/hosting/administration.cpp)
target_link_libraries(d2x_host PUBLIC d2x_server)
# MPQ preparation is outside both the authority and scheduler libraries.
add_library(d2x_game_content src/hosting/skill_content.cpp src/hosting/combat_content.cpp src/hosting/environment_content.cpp src/hosting/monster_actions_content.cpp src/hosting/act2_monster_skills.cpp src/hosting/game_content.cpp src/hosting/character_content.cpp src/hosting/character_creation.cpp)
target_link_libraries(d2x_game_content PUBLIC d2x_world d2x_server PRIVATE d2x_persistence)
add_library(d2x_character_host src/hosting/character_store.cpp src/hosting/automap_store.cpp src/hosting/embedded_realm.cpp
  src/hosting/native_game_wire.cpp src/hosting/native_combat_wire.cpp src/hosting/detail/monster_replication.cpp src/hosting/native_item_wire.cpp src/hosting/native_character_wire.cpp src/hosting/native_quest_wire.cpp
  src/hosting/detail/native_realm_service.cpp src/hosting/detail/realm_protocol.cpp
  src/hosting/detail/realm_characters.cpp src/hosting/detail/realm_games.cpp
  src/hosting/detail/game_protocol.cpp src/hosting/detail/host_administration.cpp
  src/hosting/detail/game_events.cpp src/hosting/detail/native_realm_host.cpp src/hosting/area_preparation.cpp
  src/hosting/detail/realm_connection.cpp src/hosting/detail/lan_realm.cpp
  src/hosting/detail/world_replication.cpp src/hosting/detail/player_replication.cpp
  src/hosting/protocol/message_catalog.cpp src/hosting/protocol/gameplay_dispatch.cpp
  src/hosting/protocol/handlers_movement.cpp src/hosting/protocol/handlers_combat.cpp
  src/hosting/protocol/handlers_inventory.cpp src/hosting/protocol/handlers_interaction.cpp
  src/hosting/protocol/inventory_requests.cpp
  src/hosting/protocol/handlers_progression.cpp src/hosting/protocol/handlers_social.cpp)
target_link_libraries(d2x_character_host PUBLIC d2x_host d2x_game_content d2x_network PRIVATE d2x_persistence d2x_d2gs_protocol nlohmann_json::nlohmann_json)
include(cmake/Networking.cmake)
target_link_libraries(d2x_character_host PRIVATE Threads::Threads)
add_library(d2x_client src/client/inventory_view.cpp src/client/automap_exploration.cpp
  src/client/character_projection.cpp src/client/quest_projection.cpp src/client/pet_projection.cpp)
target_link_libraries(d2x_client PUBLIC d2x_client_api PRIVATE d2x_content)
add_library(d2x_remote_scene src/client/remote_town.cpp src/client/remote_control.cpp src/client/remote_inventory.cpp src/client/remote_combat.cpp src/client/remote_ui_clients.cpp)
target_link_libraries(d2x_remote_scene PUBLIC d2x_client d2x_world d2x_remote_client)
target_sources(d2x_world PRIVATE src/world/preset_identity.cpp)
target_sources(d2x_content PRIVATE src/content/items/item_display.cpp src/content/character/character_display.cpp src/content/character/combat_display.cpp)
target_sources(d2x_content PRIVATE src/content/character/realm_portrait.cpp)

add_library(d2x_presentation
  src/presentation/graphics/graphics.cpp src/presentation/audio/audio.cpp src/presentation/graphics/primitives.cpp
  src/presentation/graphics/original_menu.cpp
  src/presentation/scene_assets.cpp src/presentation/actors/actor_animation.cpp src/presentation/scene_view.cpp
  src/presentation/hud/skill_assets.cpp src/presentation/hud/hud_controls.cpp src/presentation/hud/skill_controller.cpp
  src/presentation/hud/classic_hud.cpp
  src/presentation/hud/chat_view.cpp
  src/presentation/hud/escape_menu.cpp
  src/presentation/hud/trade_invite_view.cpp
  src/presentation/hud/party_view.cpp
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
  src/presentation/npc/client_views.cpp src/presentation/npc/commerce_controller.cpp src/presentation/hud/pet_portraits.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/automap_assets.cpp src/presentation/world/automap_view.cpp
  src/presentation/world/map_view.cpp src/presentation/world/map_controller.cpp src/presentation/world/object_hint.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/lighting_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/palette_blend_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/tile_visibility.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/audio/audio_emitters.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/audio/scene_streams.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/projectile_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/world/client_missile_assets.cpp
  src/presentation/world/client_missile_view.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/inventory/item_tooltip.cpp
  src/presentation/npc/inventory_interactions.cpp)

add_executable(d2x src/main.cpp src/app/application.cpp src/app/options.cpp src/app/input.cpp
  src/app/frontend.cpp src/app/online_login_memory.cpp src/app/client_preferences.cpp src/app/crash_report.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/frontend/realm_frontend.cpp)
target_sources(d2x_presentation PRIVATE src/presentation/remote/remote_scene.cpp src/presentation/remote/monster_effects.cpp)
target_link_libraries(d2x_presentation PRIVATE d2x_remote_scene)
target_sources(d2x PRIVATE src/app/debug/debug_pipe.cpp src/app/debug/ui_input.cpp)
target_sources(d2x PRIVATE src/app/debug/online_commands.cpp src/app/debug/server_commands.cpp src/app/debug/server_snapshot.cpp)
target_link_libraries(d2x PRIVATE d2x_presentation d2x_remote_client d2x_character_host)
target_link_libraries(d2x PRIVATE nlohmann_json::nlohmann_json)
if(WIN32)
  target_link_libraries(d2x PRIVATE advapi32 dbghelp)
endif()
add_executable(d2x_assets src/asset_tool.cpp)
add_executable(d2x_pvpgn src/server_main.cpp src/hosting/pvpgn_server.cpp)
set_target_properties(d2x_pvpgn PROPERTIES OUTPUT_NAME d2x_server)
target_link_libraries(d2x_pvpgn PRIVATE d2x_character_host d2x_persistence d2x_d2gs_protocol nlohmann_json::nlohmann_json)
target_link_libraries(d2x_assets PRIVATE d2x_content d2x_world d2x_persistence raylib nlohmann_json::nlohmann_json)
foreach(target IN ITEMS d2x_resources d2x_navigation d2x_world d2x_items d2x_content d2x_gameplay d2x_persistence d2x_presentation d2x_client d2x_network d2x_d2gs_protocol d2x_remote_client d2x_remote_scene d2x_server d2x_host d2x_game_content d2x_character_host d2x d2x_assets d2x_pvpgn)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endforeach()
if(MINGW)
  # Debug template instantiations for the domain fact variant exceed COFF's
  # 32767-section object limit. Keep this format option off other toolchains.
  target_compile_options(d2x_server PRIVATE -Wa,-mbig-obj)
  foreach(app IN ITEMS d2x d2x_assets d2x_pvpgn)
    target_link_options(${app} PRIVATE -static -static-libgcc -static-libstdc++)
  endforeach()
endif()

target_sources(d2x_content PRIVATE src/content/items/cube_data.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/items/cube.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/items/staffmods.cpp)
target_sources(d2x_game_content PRIVATE src/hosting/crafting_content.cpp)
target_sources(d2x_content PRIVATE src/content/npc/gamble_stock.cpp)

target_sources(d2x_server PRIVATE src/server/systems/inventory/ground.cpp)
target_sources(d2x_game_content PRIVATE src/hosting/item_content.cpp src/hosting/object_content.cpp src/hosting/npc_content.cpp src/hosting/merchant_content.cpp src/hosting/companion_content.cpp)
target_sources(d2x_game_content PRIVATE src/hosting/assassin_summon_content.cpp)
target_sources(d2x_game_content PRIVATE src/hosting/druid_summon_content.cpp)
target_sources(d2x_character_host PRIVATE src/hosting/loot_content.cpp)

target_sources(d2x_gameplay PRIVATE src/gameplay/effects/state.cpp src/gameplay/units/restoration.cpp src/gameplay/units/resources.cpp src/gameplay/consumables/potions.cpp src/gameplay/events/frame_queue.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/effects/native_status.cpp)
target_sources(d2x_server PRIVATE src/server/systems/effects/frame_events.cpp src/server/systems/effects/damage_states.cpp src/server/systems/effects/player_regeneration.cpp src/server/systems/effects/skill_state.cpp)
target_sources(d2x_server PRIVATE src/server/systems/monsters/frame_events.cpp src/server/systems/monsters/regeneration.cpp src/server/systems/monsters/reinitialize.cpp src/server/systems/skills/overseer.cpp)
target_sources(d2x_server PRIVATE src/server/runtime/frame_event_dispatch.cpp)
target_sources(d2x_server PRIVATE src/server/systems/missiles/frame_events.cpp
    src/server/systems/missiles/rooms.cpp
    src/server/systems/missiles/native_programs.cpp
    src/server/systems/missiles/native_hit.cpp
    src/server/systems/missiles/motion.cpp
    src/server/systems/missiles/child_creation.cpp
    src/server/systems/missiles/damage_sources.cpp)
target_sources(d2x_server PRIVATE src/server/systems/effects/item_auras.cpp)
target_sources(d2x_server PRIVATE src/server/systems/effects/storm.cpp
    src/server/systems/effects/right_aura.cpp
    src/server/systems/effects/unit_aura_selection.cpp)

target_sources(d2x_server PRIVATE src/server/systems/inventory/consumption.cpp src/server/systems/inventory/identification.cpp src/server/systems/inventory/storage.cpp)

target_sources(d2x_server PRIVATE src/server/systems/death/settlement.cpp src/server/systems/death/recovery.cpp)

target_sources(d2x_gameplay PRIVATE src/gameplay/quest/den_of_evil.cpp
  src/gameplay/character/respec.cpp
  src/gameplay/quest/burial_grounds.cpp src/gameplay/quest/search_for_cain.cpp
  src/gameplay/quest/forgotten_tower.cpp src/gameplay/quest/tools_of_trade.cpp
  src/gameplay/quest/sisters_to_slaughter.cpp)
target_sources(d2x_game_content PRIVATE src/hosting/quest_content.cpp)
target_sources(d2x_game_content PRIVATE src/hosting/quest_world_content.cpp)
target_sources(d2x_game_content PRIVATE src/hosting/hireling_content.cpp)
target_sources(d2x_server PRIVATE src/server/systems/companions/hireling.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/monsters/boss_decision.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/monsters/diablo_decision.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/monsters/baal_decision.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/monsters/enchantment_damage.cpp)
target_sources(d2x_gameplay PRIVATE src/gameplay/monsters/unique_modifiers.cpp)
target_sources(d2x_server PRIVATE src/server/systems/effects/monster_pregnancy.cpp src/server/systems/effects/monster_enchantments.cpp src/server/systems/effects/suicide_minion.cpp)
target_sources(d2x_server PRIVATE src/server/systems/ai/unique_actions.cpp)
