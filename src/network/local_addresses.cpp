#include "local_addresses.hpp"
#include <asio.hpp>
#include <algorithm>
#include <cerrno>
#include <memory>
#include <system_error>
#ifdef _WIN32
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace d2x::net {
std::vector<std::string> localIpv4Addresses() {
    std::vector<asio::ip::address_v4> addresses;
    const auto add = [&](const sockaddr *value) {
        if (!value || value->sa_family != AF_INET) return;
        const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(value);
        asio::ip::address_v4::bytes_type bytes{};
        const auto *source = reinterpret_cast<const unsigned char *>(&ipv4->sin_addr);
        std::copy_n(source, bytes.size(), bytes.begin());
        const asio::ip::address_v4 address(bytes);
        if (!address.is_unspecified() && !address.is_multicast() && address.to_uint() != 0xffffffffu)
            addresses.push_back(address);
    };
#ifdef _WIN32
    ULONG size = 16384;
    std::vector<unsigned char> storage(size);
    constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    auto adapterResult = GetAdaptersAddresses(AF_INET, flags, nullptr,
        reinterpret_cast<IP_ADAPTER_ADDRESSES *>(storage.data()), &size);
    if (adapterResult == ERROR_BUFFER_OVERFLOW) {
        storage.resize(size);
        adapterResult = GetAdaptersAddresses(AF_INET, flags, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES *>(storage.data()), &size);
    }
    if (adapterResult == ERROR_NO_DATA) return {};
    if (adapterResult != NO_ERROR) throw std::system_error(int(adapterResult), std::system_category(), "Read local IPv4 interfaces");
    for (auto *adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(storage.data()); adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) continue;
        for (auto *entry = adapter->FirstUnicastAddress; entry; entry = entry->Next)
            add(entry->Address.lpSockaddr);
    }
#else
    ifaddrs *head = nullptr;
    if (getifaddrs(&head) != 0)
        throw std::system_error(errno, std::generic_category(), "Read local IPv4 interfaces");
    const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> owner(head, freeifaddrs);
    for (auto *entry = head; entry; entry = entry->ifa_next)
        if (entry->ifa_flags & IFF_UP) add(entry->ifa_addr);
#endif
    std::sort(addresses.begin(), addresses.end(), [](const auto &left, const auto &right) {
        if (left.is_loopback() != right.is_loopback()) return !left.is_loopback();
        return left.to_uint() < right.to_uint();
    });
    addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
    std::vector<std::string> result;
    for (const auto &address : addresses) result.push_back(address.to_string());
    return result;
}
}
