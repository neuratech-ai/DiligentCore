/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "pch.h"
#include "DescriptorHeap.hpp"
#include "RenderDeviceD3D12Impl.hpp"
#include "D3D12Utils.h"

namespace Diligent
{

// Creates a new descriptor heap and reference the entire heap
DescriptorHeapAllocationManager::DescriptorHeapAllocationManager(IMemoryAllocator&                 Allocator,
                                                                 RenderDeviceD3D12Impl&            DeviceD3D12Impl,
                                                                 IDescriptorAllocator&             ParentAllocator,
                                                                 size_t                            ThisManagerId,
                                                                 const D3D12_DESCRIPTOR_HEAP_DESC& HeapDesc) :
    DescriptorHeapAllocationManager //
    {
        Allocator,
        DeviceD3D12Impl,
        ParentAllocator,
        ThisManagerId,
        [&HeapDesc, &DeviceD3D12Impl]() -> CComPtr<ID3D12DescriptorHeap> //
        {
            ID3D12Device* pDevice = DeviceD3D12Impl.GetD3D12Device();

            CComPtr<ID3D12DescriptorHeap> pd3d12DescriptorHeap;
            pDevice->CreateDescriptorHeap(&HeapDesc, __uuidof(pd3d12DescriptorHeap), reinterpret_cast<void**>(static_cast<ID3D12DescriptorHeap**>(&pd3d12DescriptorHeap)));
            return pd3d12DescriptorHeap;
        }(),
        0,                      // First descriptor
        HeapDesc.NumDescriptors // Num descriptors
    }
{
}

#ifdef DILIGENT_DEVELOPMENT
namespace
{

CComPtr<ID3D12Resource> CreateDummyTexture(ID3D12Device* pd3d12Device, DXGI_FORMAT dxgiFmt, D3D12_RESOURCE_FLAGS d3d12ResourceFlags)
{
    D3D12_RESOURCE_DESC d3d12TexDesc{};
    d3d12TexDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d3d12TexDesc.Alignment        = 0;
    d3d12TexDesc.Width            = 128;
    d3d12TexDesc.Height           = 128;
    d3d12TexDesc.DepthOrArraySize = 1;
    d3d12TexDesc.MipLevels        = 1;
    d3d12TexDesc.Format           = dxgiFmt;
    d3d12TexDesc.SampleDesc       = DXGI_SAMPLE_DESC{1, 0};
    d3d12TexDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d3d12TexDesc.Flags            = d3d12ResourceFlags;

    D3D12_HEAP_PROPERTIES d3d12HeapProps{};
    d3d12HeapProps.Type                 = D3D12_HEAP_TYPE_DEFAULT;
    d3d12HeapProps.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    d3d12HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    d3d12HeapProps.CreationNodeMask     = 1;
    d3d12HeapProps.VisibleNodeMask      = 1;

    CComPtr<ID3D12Resource> pd3d12Texture;

    HRESULT hr = pd3d12Device->CreateCommittedResource(&d3d12HeapProps, D3D12_HEAP_FLAG_NONE,
                                                       &d3d12TexDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                       __uuidof(pd3d12Texture),
                                                       reinterpret_cast<void**>(static_cast<ID3D12Resource**>(&pd3d12Texture)));
    VERIFY_EXPR(SUCCEEDED(hr));
    (void)hr;

    return pd3d12Texture;
}

CComPtr<ID3D12DescriptorHeap> CreateInvalidDescriptorHeap(ID3D12Device* pd3d12Device, const D3D12_DESCRIPTOR_HEAP_DESC& InvalidHeapDesc)
{
    CComPtr<ID3D12DescriptorHeap> pd3d12InvalidDescriptorHeap;

    pd3d12Device->CreateDescriptorHeap(&InvalidHeapDesc, __uuidof(pd3d12InvalidDescriptorHeap), reinterpret_cast<void**>(static_cast<ID3D12DescriptorHeap**>(&pd3d12InvalidDescriptorHeap)));
    DEV_CHECK_ERR(pd3d12InvalidDescriptorHeap, "Failed to create Null descriptor heap");
    const UINT DescriptorSize = pd3d12Device->GetDescriptorHandleIncrementSize(InvalidHeapDesc.Type);
    // Initialize descriptors with invalid handle - create a view and then delete the resource.
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = pd3d12InvalidDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    if (InvalidHeapDesc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        CComPtr<ID3D12Resource> pDummyTex = CreateDummyTexture(pd3d12Device, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);

        for (Uint32 i = 0; i < InvalidHeapDesc.NumDescriptors; ++i, CPUHandle.ptr += DescriptorSize)
            pd3d12Device->CreateShaderResourceView(pDummyTex, nullptr, CPUHandle);
    }
    else if (InvalidHeapDesc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
    {
        CComPtr<ID3D12Resource> pDummyTex = CreateDummyTexture(pd3d12Device, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        for (Uint32 i = 0; i < InvalidHeapDesc.NumDescriptors; ++i, CPUHandle.ptr += DescriptorSize)
            pd3d12Device->CreateRenderTargetView(pDummyTex, nullptr, CPUHandle);
    }
    else if (InvalidHeapDesc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
    {
        CComPtr<ID3D12Resource> pDummyTex = CreateDummyTexture(pd3d12Device, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

        for (Uint32 i = 0; i < InvalidHeapDesc.NumDescriptors; ++i, CPUHandle.ptr += DescriptorSize)
            pd3d12Device->CreateDepthStencilView(pDummyTex, nullptr, CPUHandle);
    }
    else if (InvalidHeapDesc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        // Nothing can be done - sampler is not an object and initialized right in the heap.
        // It is impossible to create invalid sampler.
    }
    else
    {
        UNEXPECTED("Unexpected heap type");
    }

    return pd3d12InvalidDescriptorHeap;
}

} // namespace
#endif // DILIGENT_DEVELOPMENT

// Uses subrange of descriptors in the existing D3D12 descriptor heap
// that starts at offset FirstDescriptor and uses NumDescriptors descriptors
DescriptorHeapAllocationManager::DescriptorHeapAllocationManager(IMemoryAllocator&      Allocator,
                                                                 RenderDeviceD3D12Impl& DeviceD3D12Impl,
                                                                 IDescriptorAllocator&  ParentAllocator,
                                                                 size_t                 ThisManagerId,
                                                                 ID3D12DescriptorHeap*  pd3d12DescriptorHeap,
                                                                 Uint32                 FirstDescriptor,
                                                                 Uint32                 NumDescriptors) :
    // clang-format off
    m_ParentAllocator            {ParentAllocator},
    m_DeviceD3D12Impl            {DeviceD3D12Impl},
    m_ThisManagerId              {ThisManagerId  },
    m_HeapDesc                   {pd3d12DescriptorHeap->GetDesc()},
    m_DescriptorSize             {DeviceD3D12Impl.GetD3D12Device()->GetDescriptorHandleIncrementSize(m_HeapDesc.Type)},
    m_NumDescriptorsInAllocation {NumDescriptors},
    m_FreeBlockManager           {NumDescriptors, Allocator},
    m_pd3d12DescriptorHeap       {pd3d12DescriptorHeap}
// clang-format on
{
    m_FirstCPUHandle = pd3d12DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    m_FirstCPUHandle.ptr += SIZE_T{m_DescriptorSize} * SIZE_T{FirstDescriptor};

    if (m_HeapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        m_FirstGPUHandle = pd3d12DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        m_FirstGPUHandle.ptr += SIZE_T{m_DescriptorSize} * SIZE_T{FirstDescriptor};
    }

#ifdef DILIGENT_DEVELOPMENT
    {
        D3D12_DESCRIPTOR_HEAP_DESC InvalidHeapDesc = m_HeapDesc;
        InvalidHeapDesc.NumDescriptors             = InvalidDescriptorsCount;
        InvalidHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        m_pd3d12InvalidDescriptorHeap              = CreateInvalidDescriptorHeap(DeviceD3D12Impl.GetD3D12Device(), InvalidHeapDesc);
    }
#endif
}


DescriptorHeapAllocationManager::~DescriptorHeapAllocationManager()
{
    DEV_CHECK_ERR(m_AllocationsCounter == 0, m_AllocationsCounter, " allocations have not been released. If these allocations are referenced by release queue, the app will crash when DescriptorHeapAllocationManager::FreeAllocation() is called.");
    DEV_CHECK_ERR(m_FreeBlockManager.GetFreeSize() == m_NumDescriptorsInAllocation, "Not all descriptors were released");
}

DescriptorHeapAllocation DescriptorHeapAllocationManager::Allocate(uint32_t Count)
{
    VERIFY_EXPR(Count > 0);

    std::lock_guard<std::mutex> LockGuard(m_FreeBlockManagerMutex);
    // Methods of VariableSizeAllocationsManager class are not thread safe!

    // Use variable-size GPU allocations manager to allocate the requested number of descriptors
    VariableSizeAllocationsManager::Allocation Allocation = m_FreeBlockManager.Allocate(Count, 1);
    if (!Allocation.IsValid())
        return DescriptorHeapAllocation{};

    VERIFY_EXPR(Allocation.Size == Count);

    // Compute the first CPU and GPU descriptor handles in the allocation by
    // offsetting the first CPU and GPU descriptor handle in the range
    D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = m_FirstCPUHandle;
    CPUHandle.ptr += Allocation.UnalignedOffset * m_DescriptorSize;

    D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = m_FirstGPUHandle; // Will be null if the heap is not GPU-visible
    if (m_HeapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
        GPUHandle.ptr += Allocation.UnalignedOffset * m_DescriptorSize;

    m_MaxAllocatedSize = std::max(m_MaxAllocatedSize, m_FreeBlockManager.GetUsedSize());

#ifdef DILIGENT_DEVELOPMENT
    ++m_AllocationsCounter;
    // Copy invalid descriptors. If the descriptors are accessed, this will cause device removal.
    {
        ID3D12Device* const               pd3d12Device      = m_DeviceD3D12Impl.GetD3D12Device();
        const D3D12_CPU_DESCRIPTOR_HANDLE InvalidCPUHandles = m_pd3d12InvalidDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        for (Uint32 FisrtDescr = 0; FisrtDescr < Count; FisrtDescr += InvalidDescriptorsCount)
        {
            Uint32                      NumDescrsToCopy = std::min(Count - FisrtDescr, InvalidDescriptorsCount);
            D3D12_CPU_DESCRIPTOR_HANDLE DstCPUHandle    = CPUHandle;
            DstCPUHandle.ptr += SIZE_T{FisrtDescr} * SIZE_T{m_DescriptorSize};
            pd3d12Device->CopyDescriptorsSimple(NumDescrsToCopy, DstCPUHandle, InvalidCPUHandles, m_HeapDesc.Type);
        }
    }
#endif

    VERIFY(m_ThisManagerId < std::numeric_limits<Uint16>::max(), "ManagerID exceeds 16-bit range");
    return DescriptorHeapAllocation{m_ParentAllocator, m_pd3d12DescriptorHeap, CPUHandle, GPUHandle, Count, static_cast<Uint16>(m_ThisManagerId)};
}

void DescriptorHeapAllocationManager::FreeAllocation(DescriptorHeapAllocation&& Allocation)
{
    VERIFY(Allocation.GetAllocationManagerId() == m_ThisManagerId, "Invalid descriptor heap manager Id");

    if (Allocation.IsNull())
        return;

    std::lock_guard<std::mutex> LockGuard(m_FreeBlockManagerMutex);
    size_t                      DescriptorOffset = (Allocation.GetCpuHandle().ptr - m_FirstCPUHandle.ptr) / m_DescriptorSize;
    // Methods of VariableSizeAllocationsManager class are not thread safe!
    m_FreeBlockManager.Free(DescriptorOffset, Allocation.GetNumHandles());

    // Clear the allocation
    Allocation.Reset();
#ifdef DILIGENT_DEVELOPMENT
    --m_AllocationsCounter;
#endif
}



//
// CPUDescriptorHeap implementation
//
CPUDescriptorHeap::CPUDescriptorHeap(IMemoryAllocator&           Allocator,
                                     RenderDeviceD3D12Impl&      DeviceD3D12Impl,
                                     Uint32                      NumDescriptorsInHeap,
                                     D3D12_DESCRIPTOR_HEAP_TYPE  Type,
                                     D3D12_DESCRIPTOR_HEAP_FLAGS Flags) :
    // clang-format off
    m_MemAllocator   {Allocator      },
    m_DeviceD3D12Impl{DeviceD3D12Impl},
    m_HeapPool       (STD_ALLOCATOR_RAW_MEM(DescriptorHeapAllocationManager, GetRawAllocator(), "Allocator for vector<DescriptorHeapAllocationManager>")),
    m_AvailableHeaps (STD_ALLOCATOR_RAW_MEM(size_t, GetRawAllocator(), "Allocator for unordered_set<size_t>")),
    m_HeapDesc
    {
        Type,
        NumDescriptorsInHeap,
        Flags,
        1   // NodeMask
    },
    m_DescriptorSize{DeviceD3D12Impl.GetD3D12Device()->GetDescriptorHandleIncrementSize(Type)}
// clang-format on
{
    // Create one pool
    m_HeapPool.emplace_back(m_MemAllocator, m_DeviceD3D12Impl, *this, 0, m_HeapDesc);
    m_AvailableHeaps.insert(0);
}

CPUDescriptorHeap::~CPUDescriptorHeap()
{
    DEV_CHECK_ERR(m_CurrentSize == 0, "Not all allocations released");

    DEV_CHECK_ERR(m_AvailableHeaps.size() == m_HeapPool.size(), "Not all descriptor heap pools are released");
    Uint32 TotalDescriptors = 0;
    for (DescriptorHeapAllocationManager& Heap : m_HeapPool)
    {
        DEV_CHECK_ERR(Heap.GetNumAvailableDescriptors() == Heap.GetMaxDescriptors(), "Not all descriptors in the descriptor pool are released");
        TotalDescriptors += Heap.GetMaxDescriptors();
    }

    LOG_INFO_MESSAGE(std::setw(38), std::left, GetD3D12DescriptorHeapTypeLiteralName(m_HeapDesc.Type), " CPU heap allocated pool count: ", m_HeapPool.size(),
                     ". Max descriptors: ", m_MaxSize, '/', TotalDescriptors,
                     " (", std::fixed, std::setprecision(2), m_MaxSize * 100.0 / std::max(TotalDescriptors, 1u), "%).");
}

#ifdef DILIGENT_DEVELOPMENT
int32_t CPUDescriptorHeap::DvpGetTotalAllocationCount()
{
    int32_t AllocationCount = 0;

    std::lock_guard<std::mutex> LockGuard(m_HeapPoolMutex);
    for (DescriptorHeapAllocationManager& Heap : m_HeapPool)
        AllocationCount += Heap.DvpGetAllocationsCounter();
    return AllocationCount;
}
#endif

DescriptorHeapAllocation CPUDescriptorHeap::Allocate(uint32_t Count)
{
    std::lock_guard<std::mutex> LockGuard(m_HeapPoolMutex);
    // Note that every DescriptorHeapAllocationManager object instance is itself
    // thread-safe. Nested mutexes cannot cause a deadlock

    DescriptorHeapAllocation Allocation;
    // Go through all descriptor heap managers that have free descriptors
    auto AvailableHeapIt = m_AvailableHeaps.begin();
    while (AvailableHeapIt != m_AvailableHeaps.end())
    {
        auto NextIt = AvailableHeapIt;
        ++NextIt;
        // Try to allocate descriptor using the current descriptor heap manager
        Allocation = m_HeapPool[*AvailableHeapIt].Allocate(Count);
        // Remove the manager from the pool if it has no more available descriptors
        if (m_HeapPool[*AvailableHeapIt].GetNumAvailableDescriptors() == 0)
            m_AvailableHeaps.erase(*AvailableHeapIt);

        // Terminate the loop if descriptor was successfully allocated, otherwise
        // go to the next manager
        if (!Allocation.IsNull())
            break;
        AvailableHeapIt = NextIt;
    }

    // If there were no available descriptor heap managers or no manager was able
    // to suffice the allocation request, create a new manager
    if (Allocation.IsNull())
    {
        // Make sure the heap is large enough to accommodate the requested number of descriptors
        if (Count > m_HeapDesc.NumDescriptors)
        {
            LOG_INFO_MESSAGE("Number of requested CPU descriptors handles (", Count, ") exceeds the descriptor heap size (", m_HeapDesc.NumDescriptors, "). Increasing the number of descriptors in the heap");
        }
        m_HeapDesc.NumDescriptors = std::max(m_HeapDesc.NumDescriptors, static_cast<UINT>(Count));
        // Create a new descriptor heap manager. Note that this constructor creates a new D3D12 descriptor
        // heap and references the entire heap. Pool index is used as manager ID
        m_HeapPool.emplace_back(m_MemAllocator, m_DeviceD3D12Impl, *this, m_HeapPool.size(), m_HeapDesc);
        auto NewHeapIt = m_AvailableHeaps.insert(m_HeapPool.size() - 1);
        VERIFY_EXPR(NewHeapIt.second);

        // Use the new manager to allocate descriptor handles
        Allocation = m_HeapPool[*NewHeapIt.first].Allocate(Count);
    }

    m_CurrentSize += static_cast<Uint32>(Allocation.GetNumHandles());
    m_MaxSize = std::max(m_MaxSize, m_CurrentSize);

    return Allocation;
}

// Method is called from ~DescriptorHeapAllocation()
void CPUDescriptorHeap::Free(DescriptorHeapAllocation&& Allocation, Uint64 CmdQueueMask)
{
    struct StaleAllocation
    {
        DescriptorHeapAllocation Allocation;
        CPUDescriptorHeap*       Heap;

        // clang-format off
        StaleAllocation(DescriptorHeapAllocation&& _Allocation, CPUDescriptorHeap& _Heap)noexcept :
            Allocation{std::move(_Allocation)},
            Heap      {&_Heap                }
        {
        }

        StaleAllocation            (const StaleAllocation&)  = delete;
        StaleAllocation& operator= (const StaleAllocation&)  = delete;
        StaleAllocation& operator= (      StaleAllocation&&) = delete;

        StaleAllocation(StaleAllocation&& rhs)noexcept :
            Allocation {std::move(rhs.Allocation)},
            Heap       {rhs.Heap                 }
        {
            rhs.Heap  = nullptr;
        }
        // clang-format on

        ~StaleAllocation()
        {
            if (Heap != nullptr)
                Heap->FreeAllocation(std::move(Allocation));
        }
    };
    m_DeviceD3D12Impl.SafeReleaseDeviceObject(StaleAllocation{std::move(Allocation), *this}, CmdQueueMask);
}

void CPUDescriptorHeap::FreeAllocation(DescriptorHeapAllocation&& Allocation)
{
    std::lock_guard<std::mutex> LockGuard(m_HeapPoolMutex);
    size_t                      ManagerId = Allocation.GetAllocationManagerId();
    m_CurrentSize -= static_cast<Uint32>(Allocation.GetNumHandles());
    m_HeapPool[ManagerId].FreeAllocation(std::move(Allocation));
    // Return the manager to the pool of available managers
    VERIFY_EXPR(m_HeapPool[ManagerId].GetNumAvailableDescriptors() > 0);
    m_AvailableHeaps.insert(ManagerId);
}



GPUDescriptorHeap::GPUDescriptorHeap(IMemoryAllocator&           Allocator,
                                     RenderDeviceD3D12Impl&      Device,
                                     Uint32                      NumDescriptorsInHeap,
                                     Uint32                      NumDynamicDescriptors,
                                     D3D12_DESCRIPTOR_HEAP_TYPE  Type,
                                     D3D12_DESCRIPTOR_HEAP_FLAGS Flags) :
    // clang-format off
    m_DeviceD3D12Impl{Device},
    m_HeapDesc
    {
        Type,
        NumDescriptorsInHeap + NumDynamicDescriptors,
        Flags,
        1 // UINT NodeMask;
    },
    m_pd3d12DescriptorHeap
    {
        [&]
        {
              CComPtr<ID3D12DescriptorHeap> pHeap;
              Device.GetD3D12Device()->CreateDescriptorHeap(&m_HeapDesc, __uuidof(pHeap), reinterpret_cast<void**>(&pHeap));
              return pHeap;
        }()
    },
    m_DescriptorSize           {Device.GetD3D12Device()->GetDescriptorHandleIncrementSize(Type)},
    m_HeapAllocationManager    {Allocator, Device, *this, 0, m_pd3d12DescriptorHeap, 0, NumDescriptorsInHeap},
    m_DynamicAllocationsManager{Allocator, Device, *this, 1, m_pd3d12DescriptorHeap, NumDescriptorsInHeap, NumDynamicDescriptors}
// clang-format on
{
}

GPUDescriptorHeap::~GPUDescriptorHeap()
{
    Uint32 TotalStaticSize  = m_HeapAllocationManager.GetMaxDescriptors();
    Uint32 TotalDynamicSize = m_DynamicAllocationsManager.GetMaxDescriptors();
    size_t MaxStaticSize    = m_HeapAllocationManager.GetMaxAllocatedSize();
    size_t MaxDynamicSize   = m_DynamicAllocationsManager.GetMaxAllocatedSize();

    LOG_INFO_MESSAGE(std::setw(38), std::left, GetD3D12DescriptorHeapTypeLiteralName(m_HeapDesc.Type), " GPU heap max allocated size (static|dynamic): ",
                     MaxStaticSize, '/', TotalStaticSize, " (", std::fixed, std::setprecision(2), MaxStaticSize * 100.0 / TotalStaticSize, "%) | ",
                     MaxDynamicSize, '/', TotalDynamicSize, " (", std::fixed, std::setprecision(2), MaxDynamicSize * 100.0 / TotalDynamicSize, "%).");
}

void GPUDescriptorHeap::Free(DescriptorHeapAllocation&& Allocation, Uint64 CmdQueueMask)
{
    struct StaleAllocation
    {
        DescriptorHeapAllocation Allocation;
        GPUDescriptorHeap*       Heap;

        // clang-format off
        StaleAllocation(DescriptorHeapAllocation&& _Allocation, GPUDescriptorHeap& _Heap)noexcept :
            Allocation{std::move(_Allocation)},
            Heap      {&_Heap                }
        {
        }

        StaleAllocation            (const StaleAllocation&)  = delete;
        StaleAllocation& operator= (const StaleAllocation&)  = delete;
        StaleAllocation& operator= (      StaleAllocation&&) = delete;

        StaleAllocation(StaleAllocation&& rhs)noexcept :
            Allocation{std::move(rhs.Allocation)},
            Heap      {rhs.Heap                 }
        {
            rhs.Heap  = nullptr;
        }
        // clang-format on

        ~StaleAllocation()
        {
            if (Heap != nullptr)
            {
                size_t MgrId = Allocation.GetAllocationManagerId();
                VERIFY(MgrId == 0 || MgrId == 1, "Unexpected allocation manager ID");

                if (MgrId == 0)
                {
                    Heap->m_HeapAllocationManager.FreeAllocation(std::move(Allocation));
                }
                else
                {
                    Heap->m_DynamicAllocationsManager.FreeAllocation(std::move(Allocation));
                }
            }
        }
    };
    m_DeviceD3D12Impl.SafeReleaseDeviceObject(StaleAllocation{std::move(Allocation), *this}, CmdQueueMask);
}


DynamicSuballocationsManager::DynamicSuballocationsManager(IMemoryAllocator&  Allocator,
                                                           GPUDescriptorHeap& ParentGPUHeap,
                                                           Uint32             DynamicChunkSize,
                                                           String             ManagerName) :
    // clang-format off
    m_ParentGPUHeap   {ParentGPUHeap   },
    m_ManagerName     {std::move(ManagerName)},
    m_Suballocations  (STD_ALLOCATOR_RAW_MEM(DescriptorHeapAllocation, GetRawAllocator(), "Allocator for vector<DescriptorHeapAllocation>")),
    m_DynamicChunkSize{DynamicChunkSize}
// clang-format on
{
}

DynamicSuballocationsManager::~DynamicSuballocationsManager()
{
    DEV_CHECK_ERR(m_Suballocations.empty() && m_CurrDescriptorCount == 0 && m_CurrSuballocationsTotalSize == 0, "All dynamic suballocations must be released!");
    LOG_INFO_MESSAGE(m_ManagerName, " usage stats: peak descriptor count: ", m_PeakDescriptorCount, '/', m_PeakSuballocationsTotalSize);
}

void DynamicSuballocationsManager::ReleaseAllocations(Uint64 CmdQueueMask)
{
    // Clear the list and dispose all allocated chunks of GPU descriptor heap.
    // The chunks will be added to release queues and eventually returned to the
    // parent GPU heap.
    for (DescriptorHeapAllocation& Allocation : m_Suballocations)
    {
        m_ParentGPUHeap.Free(std::move(Allocation), CmdQueueMask);
    }
    m_Suballocations.clear();
    m_CurrDescriptorCount         = 0;
    m_CurrSuballocationsTotalSize = 0;
}

DescriptorHeapAllocation DynamicSuballocationsManager::Allocate(Uint32 Count)
{
    // This method is intentionally lock-free as it is expected to
    // be called through device context from single thread only

    // Check if there are no chunks or the last chunk does not have enough space
    if (m_Suballocations.empty() ||
        size_t{m_CurrentSuballocationOffset} + size_t{Count} > m_Suballocations.back().GetNumHandles())
    {
        // Request a new chunk from the parent GPU descriptor heap
        Uint32                   SuballocationSize       = std::max(m_DynamicChunkSize, Count);
        DescriptorHeapAllocation NewDynamicSubAllocation = m_ParentGPUHeap.AllocateDynamic(SuballocationSize);
        if (NewDynamicSubAllocation.IsNull())
        {
            LOG_ERROR("Dynamic space in ", GetD3D12DescriptorHeapTypeLiteralName(m_ParentGPUHeap.GetHeapDesc().Type), " GPU descriptor heap is exhausted.");
            return DescriptorHeapAllocation();
        }
        m_Suballocations.emplace_back(std::move(NewDynamicSubAllocation));
        m_CurrentSuballocationOffset = 0;

        m_CurrSuballocationsTotalSize += SuballocationSize;
        m_PeakSuballocationsTotalSize = std::max(m_PeakSuballocationsTotalSize, m_CurrSuballocationsTotalSize);
    }

    // Perform suballocation from the last chunk
    DescriptorHeapAllocation& CurrentSuballocation = m_Suballocations.back();

    size_t ManagerId = CurrentSuballocation.GetAllocationManagerId();
    VERIFY(ManagerId < std::numeric_limits<Uint16>::max(), "ManagerID exceed allowed limit");
    DescriptorHeapAllocation Allocation(*this,
                                        CurrentSuballocation.GetDescriptorHeap(),
                                        CurrentSuballocation.GetCpuHandle(m_CurrentSuballocationOffset),
                                        CurrentSuballocation.GetGpuHandle(m_CurrentSuballocationOffset),
                                        Count,
                                        static_cast<Uint16>(ManagerId));
    m_CurrentSuballocationOffset += Count;
    m_CurrDescriptorCount += Count;
    m_PeakDescriptorCount = std::max(m_PeakDescriptorCount, m_CurrDescriptorCount);

    return Allocation;
}

} // namespace Diligent
