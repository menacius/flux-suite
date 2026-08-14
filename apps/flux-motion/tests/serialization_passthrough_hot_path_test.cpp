#include "serialization-passthrough.h"

#include <cassert>
#include <string>
#include <type_traits>
#include <vector>

int main()
{
    static_assert(std::is_nothrow_copy_constructible_v<OpaqueSerializationPassthrough>);
    static_assert(std::is_nothrow_copy_assignable_v<OpaqueSerializationPassthrough>);
    static_assert(sizeof(OpaqueSerializationPassthrough) <= sizeof(void *) * 2);

    std::string payload(4 * 1024 * 1024, 'x');
    OpaqueSerializationPassthrough original(payload);
    assert(original.size() == payload.size());

    std::vector<OpaqueSerializationPassthrough> snapshots;
    snapshots.reserve(4096);
    for (int i = 0; i < 4096; ++i)
        snapshots.push_back(original);

    assert(original.shared_owner_count_for_diagnostics() == 4097);
    assert(snapshots.front().str().data() == original.str().data());
    assert(snapshots.back().str().data() == original.str().data());

    snapshots.front() = std::string("replacement");
    assert(snapshots.front().str() == "replacement");
    assert(original.size() == payload.size());
    assert(original.shared_owner_count_for_diagnostics() == 4096);

    original.clear();
    assert(original.empty());
    assert(!snapshots.back().empty());
    return 0;
}
