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
#include "DataBlobImpl.hpp"
#include "DefaultRawMemoryAllocator.hpp"

#include <cstring>

namespace Diligent
{

RefCntAutoPtr<DataBlobImpl> DataBlobImpl::Create(IMemoryAllocator* pAllocator, size_t InitialSize, const void* pData)
{
    if (pAllocator == nullptr)
        pAllocator = &DefaultRawMemoryAllocator::GetAllocator();
    return RefCntAutoPtr<DataBlobImpl>{MakeNewRCObj<DataBlobImpl>()(*pAllocator, InitialSize, pData)};
}

RefCntAutoPtr<DataBlobImpl> DataBlobImpl::Create(size_t InitialSize, const void* pData)
{
    return Create(nullptr, InitialSize, pData);
}

RefCntAutoPtr<DataBlobImpl> DataBlobImpl::Create(DataBufferType&& DataBuff) noexcept
{
    return RefCntAutoPtr<DataBlobImpl>{MakeNewRCObj<DataBlobImpl>()(std::move(DataBuff))};
}

RefCntAutoPtr<DataBlobImpl> DataBlobImpl::MakeCopy(const IDataBlob* pDataBlob)
{
    if (pDataBlob == nullptr)
        return {};

    return Create(pDataBlob->GetSize(), pDataBlob->GetConstDataPtr());
}

DataBlobImpl::DataBlobImpl(IReferenceCounters* pRefCounters,
                           IMemoryAllocator&   Allocator,
                           size_t              InitialSize,
                           const void*         pData) :
    TBase{pRefCounters},
    m_DataBuff{InitialSize, Uint8{}, STD_ALLOCATOR_RAW_MEM(Uint8, Allocator, "Allocator for vector<Uint8>")}
{
    if (!m_DataBuff.empty() && pData != nullptr)
    {
        std::memcpy(m_DataBuff.data(), pData, InitialSize);
    }
}

DataBlobImpl::DataBlobImpl(IReferenceCounters* pRefCounters,
                           DataBufferType&&    DataBuff) noexcept :
    TBase{pRefCounters},
    m_DataBuff{std::move(DataBuff)}
{
}

DataBlobImpl::~DataBlobImpl()
{}

/// Sets the size of the internal data buffer
void DataBlobImpl::Resize(size_t NewSize)
{
    m_DataBuff.resize(NewSize);
}

/// Returns the size of the internal data buffer
size_t DataBlobImpl::GetSize() const
{
    return m_DataBuff.size();
}

/// Returns the pointer to the internal data buffer
void* DataBlobImpl::GetDataPtr(size_t Offset)
{
    return !m_DataBuff.empty() ? &m_DataBuff[Offset] : nullptr;
}

/// Returns const pointer to the internal data buffer
const void* DataBlobImpl::GetConstDataPtr(size_t Offset) const
{
    return !m_DataBuff.empty() ? &m_DataBuff[Offset] : nullptr;
}

IMPLEMENT_QUERY_INTERFACE(DataBlobImpl, IID_DataBlob, TBase)


void* DataBlobAllocatorAdapter::Allocate(size_t Size, const Char* dbgDescription, const char* dbgFileName, const Int32 dbgLineNumber)
{
    VERIFY(!m_pDataBlob, "The data blob has already been created. The allocator does not support more than one blob.");
    m_pDataBlob = DataBlobImpl::Create(Size);
    return m_pDataBlob->GetDataPtr();
}

void DataBlobAllocatorAdapter::Free(void* Ptr)
{
    VERIFY(m_pDataBlob, "Memory has not been allocated");
    VERIFY(m_pDataBlob->GetDataPtr() == Ptr, "Incorrect memory pointer");
    m_pDataBlob.Release();
}

void* DataBlobAllocatorAdapter::AllocateAligned(size_t Size, size_t Alignment, const Char* dbgDescription, const char* dbgFileName, const Int32 dbgLineNumber)
{
    VERIFY(Alignment <= sizeof(void*), "Alignment (", Alignment, ") exceeds the default alignment (", sizeof(void*), ")");
    return Allocate(Size, dbgDescription, dbgFileName, dbgLineNumber);
}

void DataBlobAllocatorAdapter::FreeAligned(void* Ptr)
{
    Free(Ptr);
}

} // namespace Diligent
