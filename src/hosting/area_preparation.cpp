#include "area_preparation.hpp"
#include "resources/archive.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

namespace d2x::hosting {
namespace {
uint64_t preparationMilliseconds() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}
struct AreaPreparation::Impl {
    struct Request {uint64_t id{};AreaGenerationRequest value;uint64_t submittedMilliseconds{};std::vector<RoomTerrainId> roomInputs;};
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Request> requests;
    std::map<uint64_t, std::optional<Result>> results;
    uint64_t next{1};
    bool stopping{};
    std::thread worker;
    Impl(const Archives &source, std::shared_ptr<const ClassicData> content)
        : worker([this, paths = source.mountedPaths(), identity = source.mountIdentity(),
                  pd2Presets=source.pd2Presets,content = std::move(content)] {
            Archives archives;
            archives.pd2Presets=pd2Presets;
            std::string mountError;
            try {
                for (const auto &path : paths) archives.mount(path);
                if (archives.mountIdentity() != identity) throw std::runtime_error("MPQ set changed during area preparation");
            } catch (const std::exception &error) { mountError = error.what(); }
            for (;;) {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || !requests.empty(); });
                if (stopping) return;
                auto [id, request, submitted, roomInputs] = std::move(requests.front()); requests.pop_front();
                if (!results.contains(id)) continue;
                lock.unlock();
                Result result;
                result.level=request.level;
                result.submittedMilliseconds=submitted;
                result.startedMilliseconds=preparationMilliseconds();
                try {
                    if (!mountError.empty()) throw std::runtime_error(mountError);
                    if(roomInputs.empty()) result.area = prepareWorldArea(archives, *content, request);
                    else result.roomInputs=generateRoomInputs(archives,request,roomInputs);
                } catch (const std::exception &error) { result.error = error.what(); }
                result.completedMilliseconds=preparationMilliseconds();
                lock.lock();
                if (auto found = results.find(id); found != results.end()) found->second = std::move(result);
            }
        }) {}
    ~Impl() {
        { std::lock_guard lock(mutex); stopping = true; }
        wake.notify_one(); worker.join();
    }
};
AreaPreparation::AreaPreparation(const Archives &source, std::shared_ptr<const ClassicData> content)
    : impl_(std::make_unique<Impl>(source, std::move(content))) {}
AreaPreparation::~AreaPreparation() = default;
uint64_t AreaPreparation::submit(AreaGenerationRequest request, std::vector<RoomTerrainId> roomInputs) {
    std::unique_lock lock(impl_->mutex);
    if (impl_->next == UINT64_MAX) throw std::runtime_error("Area preparation ticket capacity exhausted");
    const auto id = impl_->next++;
    impl_->results.emplace(id, std::nullopt);
    const auto submitted=preparationMilliseconds();
    const auto roomCount=roomInputs.size();
    impl_->requests.push_back({id,request,submitted,std::move(roomInputs)});
    const auto queued=impl_->requests.size();
    lock.unlock();
    impl_->wake.notify_one();
    std::clog << "[host-content] submit steady_ms=" << submitted << " ticket=" << id
        << " level=" << request.level << " queued=" << queued << " room_inputs=" << roomCount << '\n';
    return id;
}
std::optional<AreaPreparation::Result> AreaPreparation::take(uint64_t id) {
    std::unique_lock lock(impl_->mutex);
    auto found = impl_->results.find(id);
    if (found == impl_->results.end()) throw std::logic_error("Unknown area preparation ticket");
    if (!found->second) return {};
    auto result = std::move(found->second);
    impl_->results.erase(found);
    lock.unlock();
    const auto now=preparationMilliseconds();
    std::clog << "[host-content] ready steady_ms=" << now << " ticket=" << id << " level=" << result->level
        << " queue_ms=" << result->startedMilliseconds-result->submittedMilliseconds
        << " work_ms=" << result->completedMilliseconds-result->startedMilliseconds
        << " delivery_ms=" << now-result->completedMilliseconds
        << " total_ms=" << now-result->submittedMilliseconds << " failed=" << !result->error.empty() << '\n';
    return result;
}
void AreaPreparation::cancel(uint64_t id) {
    std::lock_guard lock(impl_->mutex);
    impl_->results.erase(id);
    std::erase_if(impl_->requests, [=](const auto &request) { return request.id == id; });
}
}
