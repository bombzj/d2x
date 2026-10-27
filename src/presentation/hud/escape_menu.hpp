#pragma once
#include "presentation/graphics/primitives.hpp"
#include "presentation/audio/audio_settings.hpp"
#include "presentation/control_bindings.hpp"
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace d2x {
class PaletteBlendView;
enum class EscapeMenuPage { Main, Options, Automap, Sound, Video, Controls };

// Native English artwork and geometry. Selection and supported setting changes
// remain in the scene controller; this view never edits gameplay or preferences.
class EscapeMenu {
    struct Art {
        GpuAnimation normal, disabled;
        int width = 0, height = 0;
    };
    struct Row {
        std::string label;
        std::vector<std::string> values;
        bool enabled = false, slider = false;
        int value = 0;
        int sliderSteps = 21;
        bool centeredSlider = false;
    };
    struct Binding {
        std::string label;
        ControlAction action = ControlAction::Count;
    };
    Graphics graphics_;
    std::map<std::string, Art, std::less<>> art_;
    std::array<std::vector<Row>, 5> pages_;
    std::array<ClassicFont, 6> fonts_;
    std::vector<Binding> bindings_;
    std::map<uint16_t, std::string> keyNames_;
    std::array<std::string, 3> headers_, buttons_;
    GpuAnimation marker_, border_, scroll_;
    int markerWidth_ = 0;
    const Art &art(std::string_view name) const;
    void image(std::string_view name, float x, float baseline, int alignment, bool enabled) const;
    void text(std::string_view value, float x, float baseline, int color) const;
    int textWidth(std::string_view value) const;
    void drawControls(int selected, int first, int scrollPressed, const ControlBindings &, int action,
                      int column, bool capturing, float elapsed, const PaletteBlendView &) const;

  public:
    explicit EscapeMenu(Archives &);
    int count(EscapeMenuPage) const;
    bool enabled(EscapeMenuPage, int row) const;
    Rectangle bounds(EscapeMenuPage, int row) const;
    int hit(EscapeMenuPage, Vec mouse) const;
    bool sliderHit(int row, Vec mouse) const;
    int sliderValue(float mouseX) const;
    int controlScrollLimit() const;
    Rectangle controlTrack() const;
    int controlThumbHeight() const;
    std::pair<int, int> controlAt(Vec mouse, int first) const;
    int controlRow(ControlAction) const;
    std::string keyName(uint16_t) const;
    std::string bindingName(const ControlBindings &, ControlAction) const;
    void draw(EscapeMenuPage, int selected, float elapsed, const std::array<int, 5> &automap, const AudioSettings &,
              bool lighting, int controlScroll, int scrollPressed, const ControlBindings &, int action,
              int column, bool capturing, const PaletteBlendView &) const;
};
} // namespace d2x
