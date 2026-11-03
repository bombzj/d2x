#include "domain_fact.hpp"
#include "facts/resources.hpp"

namespace d2x::server {
DomainFact::DomainFact() : DomainFact(ManaFact{}) {}
DomainFact::DomainFact(const DomainFact &other) {
    if (other.operations_) other.operations_->copy(*this, other);
}
DomainFact::DomainFact(DomainFact &&other) noexcept {
    if (other.operations_) other.operations_->move(*this, other);
}
DomainFact &DomainFact::operator=(const DomainFact &other) {
    if (this != &other) {
        DomainFact copy(other); // Keep the original value if a snapshot copy fails.
        *this = std::move(copy);
    }
    return *this;
}
DomainFact &DomainFact::operator=(DomainFact &&other) noexcept {
    if (this != &other) {
        reset();
        if (other.operations_) other.operations_->move(*this, other);
    }
    return *this;
}
void DomainFact::reset() noexcept {
    if (operations_) operations_->destroy(*this);
    operations_ = nullptr;
    heap_ = nullptr;
}
DomainFact::~DomainFact() { reset(); }
}
