#pragma once
#include "image_slots.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
namespace single_overlay::native {
inline bool InsideLoader() noexcept {
    using Fn = BOOLEAN (NTAPI*)();
    static auto fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIsThreadWithinLoaderCallout"));
    return !fn || fn();
}
inline bool PinInterface(IUnknown* object, size_t last) noexcept {
    if (!object || last > 64) return false;
    auto** table = *reinterpret_cast<void***>(object);
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(table), &owner)
        || !slots::ImageSlot(owner, table) || !slots::ImageSlot(owner, table + last)) return false;
    for (size_t i = 0; i <= last; ++i) {
        HMODULE callable = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(table[i]), &callable) || !slots::ImageEntry(callable, table[i])) return false;
    }
    return true;
}
inline bool SystemDxgiObject(IUnknown* object, size_t last) noexcept {
    wchar_t path[MAX_PATH]{};
    if (!object || !GetSystemDirectoryW(path, MAX_PATH) || wcscat_s(path, L"\\dxgi.dll")) return false;
    const HMODULE dxgi = GetModuleHandleW(path);
    auto** table = *reinterpret_cast<void***>(object);
    return slots::ImageSlot(dxgi, table) && slots::ImageSlot(dxgi, table + last) && PinInterface(object, last);
}
// A producer may supply a valid COM object with an object-owned vtable (for
// example Agility or an overlay). Only borrowed call parameters use this
// policy: the caller keeps the object/table alive for the duration of the call.
// Pin executable implementations, rather than requiring the table itself to
// reside in a DLL. Factory/long-lived hook admission keeps PinInterface above.
inline bool ReadableSpan(const void* pointer,size_t bytes) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    const auto address=reinterpret_cast<uintptr_t>(pointer);
    if(!pointer || !bytes || VirtualQuery(pointer,&memory,sizeof(memory))!=sizeof(memory)
        || memory.State!=MEM_COMMIT || (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    constexpr DWORD readable=PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY
        |PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY;
    const auto start=reinterpret_cast<uintptr_t>(memory.BaseAddress);
    return (memory.Protect&readable) && address>=start && bytes<=memory.RegionSize
        && address-start<=memory.RegionSize-bytes;
}
inline bool PinBorrowedInterface(IUnknown* object,size_t last) noexcept {
    if(last>64 || !ReadableSpan(object,sizeof(void*))) return false;
    auto** table=*reinterpret_cast<void***>(object);
    if(reinterpret_cast<uintptr_t>(table)%alignof(void*) || !ReadableSpan(table,(last+1)*sizeof(void*))) return false;
    for(size_t i=0;i<=last;++i) {
        HMODULE owner{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(table[i]),&owner) || !slots::ImageEntry(owner,table[i])) return false;
    }
    return true;
}
// NVIDIA, ReShade and Special K's public COM unwrapping contracts. No private
// object offsets, no replacement of an application's interface, and bounded
// cycle detection.
template<class T,bool Borrowed=false> bool Unwrap(IUnknown* input, Microsoft::WRL::ComPtr<T>& output) noexcept {
    using Microsoft::WRL::ComPtr;
    constexpr GUID baseGuid = {0xadec44e2,0x61f0,0x45c3,{0xad,0x9f,0x1b,0x37,0x37,0x92,0x84,0xff}};
    // ReShade 6.8.0 source/com_utils.hpp: IID_UnwrappedObject. The returned
    // original owns an AddRef, as with any successful QueryInterface call.
    constexpr GUID reshadeGuid = {0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    // Special K 26.9.25 include/SpecialK/render/dxgi/dxgi_swapchain.h:
    // IID_IUnwrappedDXGISwapChain. Only its swapchain wrapper answers, with its
    // AddRef'd real swapchain; it refuses Streamline's base IID by design and
    // passes other IIDs to that real swapchain.
    constexpr GUID specialKGuid = {0xe8a33b4a,0x1405,0x424c,{0xae,0x88,0x0d,0x3e,0x9d,0x46,0xc9,0x14}};
    output.Reset();
    if constexpr(Borrowed) if(!PinBorrowedInterface(input,2)) return false;
    ComPtr<IUnknown> current = input;
    std::array<ComPtr<IUnknown>, 4> visited;
    for (size_t i = 0; current && i < visited.size(); ++i) {
        const bool callable=Borrowed?PinBorrowedInterface(current.Get(),2):PinInterface(current.Get(),2);
        if (!callable || FAILED(current.As(&visited[i]))) return false;
        for (size_t j = 0; j < i; ++j) if (visited[j].Get() == visited[i].Get()) return false;
        ComPtr<IUnknown> base;
        HRESULT hr = current->QueryInterface(baseGuid, reinterpret_cast<void**>(base.GetAddressOf()));
        for (const GUID& next : {reshadeGuid, specialKGuid}) {
            if (hr != E_NOINTERFACE) break;
            if (base) return false;
            hr = current->QueryInterface(next, reinterpret_cast<void**>(base.GetAddressOf()));
        }
        if (hr == E_NOINTERFACE && base) return false;
        if (hr == E_NOINTERFACE) return SUCCEEDED(current.As(&output)) && output;
        if (hr != S_OK || !base) return false;
        current = base;
    }
    return false;
}
inline bool UnwrapProducerCommand(IUnknown* input,Microsoft::WRL::ComPtr<ID3D12CommandList>& output) noexcept {
    return Unwrap<ID3D12CommandList,true>(input,output);
}
}
