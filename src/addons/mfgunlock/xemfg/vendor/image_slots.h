#pragma once
#include <windows.h>
#include <cstdint>
namespace single_overlay::slots {
inline bool ImageSlot(HMODULE image, void** slot) noexcept
{
    MEMORY_BASIC_INFORMATION m{};
    const auto address = reinterpret_cast<uintptr_t>(slot);
    return image && slot && !(address % alignof(void*))
        && VirtualQuery(slot, &m, sizeof(m)) == sizeof(m)
        && m.State == MEM_COMMIT && m.Type == MEM_IMAGE && m.AllocationBase == image
        && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_EXECUTE
            | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
        && address >= reinterpret_cast<uintptr_t>(m.BaseAddress)
        && m.RegionSize >= sizeof(void*)
        && address - reinterpret_cast<uintptr_t>(m.BaseAddress) <= m.RegionSize - sizeof(void*);
}

inline bool ImageEntry(HMODULE image, void* entry) noexcept
{
    MEMORY_BASIC_INFORMATION m{};
    return image && entry && VirtualQuery(entry, &m, sizeof(m)) == sizeof(m)
        && m.Type == MEM_IMAGE && m.State == MEM_COMMIT && m.AllocationBase == image
        && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        && (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}


}
