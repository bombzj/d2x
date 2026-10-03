#include "presentation/scene_view.hpp"

namespace d2x {
const MapSceneView &SceneView::mapView() const {
    const auto &value = mapClient_.read();
    if (mapView_.revision != value.revision) mapView_ = value;
    return mapView_;
}
std::vector<TravelEntryView> SceneView::travelEntries() const {
    return mapClient_.travel(view_.waypointSource, view_.waypointAct).entries;
}
} // namespace d2x
