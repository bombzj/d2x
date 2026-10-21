#pragma once
namespace d2x {
// Remote clients prepare resources and display metadata without constructing
// original server skill/AI/drop programs for a modified content build.
enum class ContentPreparation { OriginalRules, RemoteDisplay };
}
