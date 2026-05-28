#pragma once

#include "spp/base/status.h"

#include <new>
#include <type_traits>
#include <utility>

namespace spp {

template <class T>
class [[nodiscard]] Expected {
 public:
    Expected(T value) : has_value_(true) {
        new (&storage_.value) T(std::move(value));
    }
    Expected(Status status) : has_value_(false) {
        new (&storage_.status) Status(std::move(status));
    }
    Expected(const Expected& other) : has_value_(other.has_value_) {
        if (has_value_)
            new (&storage_.value) T(other.storage_.value);
        else
            new (&storage_.status) Status(other.storage_.status);
    }
    Expected(Expected&& other) noexcept : has_value_(other.has_value_) {
        if (has_value_)
            new (&storage_.value) T(std::move(other.storage_.value));
        else
            new (&storage_.status) Status(std::move(other.storage_.status));
    }
    Expected& operator=(const Expected& other) {
        if (this == &other)
            return *this;
        destroy();
        has_value_ = other.has_value_;
        if (has_value_)
            new (&storage_.value) T(other.storage_.value);
        else
            new (&storage_.status) Status(other.storage_.status);
        return *this;
    }
    Expected& operator=(Expected&& other) noexcept {
        if (this == &other)
            return *this;
        destroy();
        has_value_ = other.has_value_;
        if (has_value_)
            new (&storage_.value) T(std::move(other.storage_.value));
        else
            new (&storage_.status) Status(std::move(other.storage_.status));
        return *this;
    }
    ~Expected() {
        destroy();
    }

    bool ok() const noexcept {
        return has_value_;
    }
    explicit operator bool() const noexcept {
        return has_value_;
    }

    T& value() & noexcept {
        return storage_.value;
    }
    const T& value() const& noexcept {
        return storage_.value;
    }
    T&& value() && noexcept {
        return std::move(storage_.value);
    }

    T* operator->() noexcept {
        return &storage_.value;
    }
    const T* operator->() const noexcept {
        return &storage_.value;
    }
    T& operator*() & noexcept {
        return storage_.value;
    }
    const T& operator*() const& noexcept {
        return storage_.value;
    }
    T&& operator*() && noexcept {
        return std::move(storage_.value);
    }

    const Status& status() const& noexcept {
        return storage_.status;
    }
    Status&& status() && noexcept {
        return std::move(storage_.status);
    }

 private:
    void destroy() noexcept {
        if (has_value_)
            storage_.value.~T();
        else
            storage_.status.~Status();
    }

    union Storage {
        Storage() {}
        ~Storage() {}
        T value;
        Status status;
    };
    Storage storage_;
    bool has_value_;
};

}  // namespace spp

// Convenience: bail out on error, propagating the Status from inside an Expected<T> producer.
// Usage:  SPP_ASSIGN_OR_RETURN(auto x, MakeX());
#define SPP_INTERNAL_CAT_INNER(a, b) a##b
#define SPP_INTERNAL_CAT(a, b) SPP_INTERNAL_CAT_INNER(a, b)

#define SPP_ASSIGN_OR_RETURN(decl, expr)                                  \
    auto SPP_INTERNAL_CAT(_spp_tmp_, __LINE__) = (expr);                  \
    if (!SPP_INTERNAL_CAT(_spp_tmp_, __LINE__).ok())                      \
        return std::move(SPP_INTERNAL_CAT(_spp_tmp_, __LINE__)).status(); \
    decl = std::move(*SPP_INTERNAL_CAT(_spp_tmp_, __LINE__))

#define SPP_RETURN_IF_ERROR(expr)                                    \
    do {                                                             \
        ::spp::Status SPP_INTERNAL_CAT(_spp_st_, __LINE__) = (expr); \
        if (!SPP_INTERNAL_CAT(_spp_st_, __LINE__).ok())              \
            return SPP_INTERNAL_CAT(_spp_st_, __LINE__);             \
    } while (0)
