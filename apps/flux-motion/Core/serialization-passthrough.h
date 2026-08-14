#pragma once

#include <memory>
#include <string>
#include <utility>

/*
 * OpaqueSerializationPassthrough
 * --------------------------------
 * Serialization/migration metadata must survive model round-trips, but it is
 * not render data. Layers, effects, transitions, cameras and whole titles are
 * copied frequently by editor snapshots and compatibility render paths. A raw
 * std::string here made every copy duplicate the full source JSON payload.
 *
 * This immutable shared payload keeps those copies O(1). Replacing the payload
 * creates a new shared string, so authored/runtime objects remain value-like
 * without exposing mutable shared state to worker threads.
 */
class OpaqueSerializationPassthrough {
public:
    OpaqueSerializationPassthrough() noexcept = default;
    OpaqueSerializationPassthrough(const OpaqueSerializationPassthrough &) noexcept = default;
    OpaqueSerializationPassthrough(OpaqueSerializationPassthrough &&) noexcept = default;
    OpaqueSerializationPassthrough &operator=(const OpaqueSerializationPassthrough &) noexcept = default;
    OpaqueSerializationPassthrough &operator=(OpaqueSerializationPassthrough &&) noexcept = default;

    explicit OpaqueSerializationPassthrough(std::string payload)
    {
        assign(std::move(payload));
    }

    OpaqueSerializationPassthrough &operator=(std::string payload)
    {
        assign(std::move(payload));
        return *this;
    }

    OpaqueSerializationPassthrough &operator=(const char *payload)
    {
        assign(payload ? std::string(payload) : std::string());
        return *this;
    }

    void assign(std::string payload)
    {
        if (payload.empty()) {
            payload_.reset();
            return;
        }
        payload_ = std::make_shared<const std::string>(std::move(payload));
    }

    void clear() noexcept { payload_.reset(); }
    bool empty() const noexcept { return !payload_ || payload_->empty(); }
    std::size_t size() const noexcept { return payload_ ? payload_->size() : 0u; }

    const std::string &str() const noexcept
    {
        static const std::string empty_payload;
        return payload_ ? *payload_ : empty_payload;
    }

    /* Read-only conversion keeps existing serializer helpers source-compatible. */
    operator const std::string &() const noexcept { return str(); }

    long shared_owner_count_for_diagnostics() const noexcept
    {
        return payload_ ? payload_.use_count() : 0L;
    }

private:
    std::shared_ptr<const std::string> payload_;
};

static_assert(sizeof(OpaqueSerializationPassthrough) <= sizeof(void *) * 2,
              "Serialization passthrough must stay a cheap render-snapshot field");
