#pragma once
#include <cstdint>

class UUID {
public:
    UUID();
    UUID(uint64_t uuid);
    UUID(const UUID&) = default;

    operator uint64_t() const { return m_UUID; }
private:
    uint64_t m_UUID;
};

// Injection dans std::hash pour pouvoir utiliser les UUID comme clés dans un std::unordered_map
namespace std {
    template <typename T> struct hash;
    template<>
    struct hash<UUID> {
        std::size_t operator()(const UUID& uuid) const {
            return (uint64_t)uuid;
        }
    };
}