#include "area_preparation.hpp"
#include "resources/archive.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace d2x::hosting {
struct AreaPreparation::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::pair<uint64_t, AreaGenerationRequest>> requests;
    std::map<uint64_t, std::optional<Result>> results;
    uint64_t next{1};
    bool stopping{};
    std::thread worker;
    Impl(const Archives &source, std::shared_ptr<const ClassicData> content)
        : worker([this, paths = source.mountedPaths(), identity = source.mountIdentity(), content = std::move(content)] {
            Archives archives;
            std::string mountError;
            try {
                for (const auto &path : paths) archives.mount(path);
                if (archives.mountIdentity() != identity) throw std::runtime_error("MPQ set changed during area preparation");
            } catch (const std::exception &error) { mountError = error.what(); }
            for (;;) {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stopping || !requests.empty(); });
                if (stopping) return;
                const auto [id, request] = requests.front(); requests.pop_front();
                if (!results.contains(id)) continue;
                lock.unlock();
                Result result;
                try {
                    if (!mountError.empty()) throw std::runtime_error(mountError);
                    result.area = prepareWorldArea(archives, *content, request);
                } catch (const std::exception &error) { result.error = error.what(); }
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
uint64_t AreaPreparation::submit(AreaGenerationRequest request) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->next == UINT64_MAX) throw std::runtime_error("Area preparation ticket capacity exhausted");
    const auto id = impl_->next++;
    impl_->results.emplace(id, std::nullopt);
    impl_->requests.emplace_back(id, request);
    impl_->wake.notify_one();
    return id;
}
std::optional<AreaPreparation::Result> AreaPreparation::take(uint64_t id) {
    std::lock_guard lock(impl_->mutex);
    auto found = impl_->results.find(id);
    if (found == impl_->results.end()) throw std::logic_error("Unknown area preparation ticket");
    if (!found->second) return {};
    auto result = std::move(found->second);
    impl_->results.erase(found);
    return result;
}
void AreaPreparation::cancel(uint64_t id) {
    std::lock_guard lock(impl_->mutex);
    impl_->results.erase(id);
    std::erase_if(impl_->requests, [=](const auto &request) { return request.first == id; });
}
}
