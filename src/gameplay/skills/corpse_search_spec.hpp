#pragma once
#include <array>
namespace d2x {
struct CorpseSearchSpec {
    bool potion = true;
    int minimumChance{}, maximumChance{}, chance{};
    int manaPotionChance{}, rejuvenationChance{}, noSelectState{-1};
    // SkillBar's original act/difficulty potion table is code, not a TXT table.
    std::array<std::array<const char *, 3>, 15> potions{{
        {{"hp2","mp2","rvs"}},{{"hp3","mp3","rvs"}},{{"hp3","mp3","rvs"}},
        {{"hp4","mp4","rvl"}},{{"hp4","mp4","rvl"}},{{"hp4","mp4","rvl"}},
        {{"hp4","mp5","rvl"}},{{"hp5","mp5","rvl"}},{{"hp5","mp5","rvl"}},
        {{"hp5","mp5","rvl"}},{{"hp5","mp5","rvl"}},{{"hp5","mp5","rvl"}},
        {{"hp5","mp5","rvl"}},{{"hp5","mp5","rvl"}},{{"hp5","mp5","rvl"}}}};
};
}
