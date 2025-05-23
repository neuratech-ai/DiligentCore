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

#include "VAOCache.hpp"

#include <unordered_set>

#include "RenderDeviceGLImpl.hpp"
#include "BufferGLImpl.hpp"
#include "PipelineStateGLImpl.hpp"

#include "GLObjectWrapper.hpp"
#include "GLTypeConversions.hpp"
#include "GLContextState.hpp"
#include "BasicMath.hpp"
#include "PlatformMisc.hpp"

namespace Diligent
{

VAOCache::VAOCache() :
    m_EmptyVAO{true}
{
    m_Cache.max_load_factor(0.5f);
    m_PSOToKey.max_load_factor(0.5f);
    m_BuffToKey.max_load_factor(0.5f);
}

VAOCache::~VAOCache()
{
    VERIFY(m_Cache.empty(), "VAO cache is not empty. Are there any unreleased objects?");
    VERIFY(m_PSOToKey.empty(), "PSOToKey hash is not empty");
    VERIFY(m_BuffToKey.empty(), "BuffToKey hash is not empty");
}

void VAOCache::OnDestroyBuffer(const BufferGLImpl& Buffer)
{
    // Collect all stale keys that use this buffer.
    std::vector<VAOHashKey> StaleKeys;

    Threading::SpinLockGuard CacheGuard{m_CacheLock};

    const auto it = m_BuffToKey.find(Buffer.GetUniqueID());
    if (it != m_BuffToKey.end())
    {
        for (const VAOHashKey& Key : it->second)
        {
            StaleKeys.push_back(Key);
            m_Cache.erase(Key);
        }
        m_BuffToKey.erase(it);
    }

    // Clear stale entries in m_PSOToKey and m_BuffToKey that refer to dead VAOs
    // to avoid memory leaks.
    ClearStaleKeys(StaleKeys);
}

void VAOCache::OnDestroyPSO(const PipelineStateGLImpl& PSO)
{
    // Collect all stale keys that use this PSO.
    std::vector<VAOHashKey> StaleKeys;

    Threading::SpinLockGuard CacheGuard{m_CacheLock};

    const auto it = m_PSOToKey.find(PSO.GetUniqueID());
    if (it != m_PSOToKey.end())
    {
        for (const VAOHashKey& Key : it->second)
        {
            StaleKeys.push_back(Key);
            m_Cache.erase(Key);
        }
        m_PSOToKey.erase(it);
    }

    // Clear stale entries in m_PSOToKey and m_BuffToKey that refer to dead VAOs
    // to avoid memory leaks.
    ClearStaleKeys(StaleKeys);
}

void VAOCache::Clear()
{
    Threading::SpinLockGuard CacheGuard{m_CacheLock};

    m_Cache.clear();
    m_PSOToKey.clear();
    m_BuffToKey.clear();
}

void VAOCache::ClearStaleKeys(const std::vector<VAOHashKey>& StaleKeys)
{
    // Collect unique PSOs and buffers used in stale keys.
    std::unordered_set<UniqueIdentifier> CandidatePSOs;
    std::unordered_set<UniqueIdentifier> CandidateBuffers;
    for (const VAOHashKey& StaleKey : StaleKeys)
    {
        CandidatePSOs.emplace(StaleKey.PsoUId);

        if (StaleKey.IndexBufferUId != 0)
            CandidateBuffers.emplace(StaleKey.IndexBufferUId);

        for (Uint32 SlotMask = StaleKey.UsedSlotsMask; SlotMask != 0;)
        {
            const Uint32 SlotBit = ExtractLSB(SlotMask);
            const Uint32 Slot    = PlatformMisc::GetLSB(SlotBit);
            VERIFY_EXPR(StaleKey.Streams[Slot].BufferUId >= 0);
            CandidateBuffers.emplace(StaleKey.Streams[Slot].BufferUId);
        }
    }

    auto RemoveStaleEntries = [this](const std::unordered_set<UniqueIdentifier>&                    CandidateIds,
                                     std::unordered_map<UniqueIdentifier, std::vector<VAOHashKey>>& IdToKey) //
    {
        auto EraseSwap = [](std::vector<VAOHashKey>& Keys, size_t Index) //
        {
            if (Index != Keys.size() - 1)
                std::swap(Keys[Index], Keys.back());
            Keys.pop_back();
        };

        // Delete stale entries that reference dead keys
        for (const UniqueIdentifier Id : CandidateIds)
        {
            const auto it = IdToKey.find(Id);
            if (it != IdToKey.end())
            {
                // This vector may shrink during iteration
                auto& Keys = it->second;
                for (size_t i = 0; i < Keys.size();)
                {
                    if (m_Cache.find(Keys[i]) == m_Cache.end())
                        EraseSwap(Keys, i); // There is no more VAO with this key
                    else
                        ++i;
                }
            }
        }
    };
    RemoveStaleEntries(CandidatePSOs, m_PSOToKey);
    RemoveStaleEntries(CandidateBuffers, m_BuffToKey);
}

VAOCache::VAOHashKey::VAOHashKey(const VAOAttribs& Attribs) :
    // clang-format off
    PsoUId         {Attribs.PSO.GetUniqueID()},
    IndexBufferUId {Attribs.pIndexBuffer ? Attribs.pIndexBuffer->GetUniqueID() : 0}
// clang-format on
{
#ifdef DILIGENT_DEBUG
    for (Uint32 i = 0; i < _countof(Streams); ++i)
        Streams[i].BufferUId = -1;
#endif

    const InputLayoutDesc& InputLayout    = Attribs.PSO.GetGraphicsPipelineDesc().InputLayout;
    const LayoutElement*   LayoutElements = InputLayout.LayoutElements;

    Hash = ComputeHash(PsoUId, IndexBufferUId);
    for (Uint32 i = 0; i < InputLayout.NumElements; ++i)
    {
        const LayoutElement& LayoutElem = LayoutElements[i];
        const Uint32         BufferSlot = LayoutElem.BufferSlot;
        VERIFY_EXPR(BufferSlot < MAX_BUFFER_SLOTS);
        DEV_CHECK_ERR(BufferSlot < Attribs.NumVertexStreams, "Input layout requires at least ", BufferSlot + 1,
                      " buffer", (BufferSlot > 0 ? "s" : ""), ", but only ", Attribs.NumVertexStreams, ' ',
                      (Attribs.NumVertexStreams == 1 ? "is" : "are"), " bound.");

        const auto& SrcStream = Attribs.VertexStreams[BufferSlot];
        DEV_CHECK_ERR(SrcStream.pBuffer, "VAO requires buffer at slot ", BufferSlot, ", but none is bound in the context.");

        const Int32  BuffId  = SrcStream.pBuffer ? SrcStream.pBuffer->GetUniqueID() : 0;
        const Uint32 SlotBit = 1u << BufferSlot;
        if ((UsedSlotsMask & SlotBit) == 0)
        {
            VAOHashKey::StreamAttribs& DstStream{Streams[BufferSlot]};
            DstStream.BufferUId = BuffId;
            DstStream.Offset    = SrcStream.Offset;
            UsedSlotsMask |= SlotBit;
            HashCombine(Hash, DstStream.BufferUId, DstStream.Offset);
        }
        else
        {
            VAOHashKey::StreamAttribs& DstStream{Streams[BufferSlot]};
            // The slot has already been initialized
            VERIFY_EXPR(DstStream.BufferUId == BuffId);
            VERIFY_EXPR(DstStream.Offset == SrcStream.Offset);
        }
    }
    HashCombine(Hash, UsedSlotsMask);
}

bool VAOCache::VAOHashKey::operator==(const VAOHashKey& Key) const noexcept
{
    if (Hash != Key.Hash)
        return false;

    // clang-format off
    if (PsoUId         != Key.PsoUId         ||
        IndexBufferUId != Key.IndexBufferUId ||
        UsedSlotsMask  != Key.UsedSlotsMask)
        return false;
    // clang-format on

    for (Uint32 SlotMask = Key.UsedSlotsMask; SlotMask != 0;)
    {
        const Uint32 SlotBit = ExtractLSB(SlotMask);
        const Uint32 Slot    = PlatformMisc::GetLSB(SlotBit);
        VERIFY_EXPR(Streams[Slot].BufferUId >= 0 && Key.Streams[Slot].BufferUId >= 0);
        if (Streams[Slot] != Key.Streams[Slot])
            return false;
    }

    return true;
}

const GLObjectWrappers::GLVertexArrayObj& VAOCache::GetVAO(const VAOAttribs& Attribs,
                                                           GLContextState&   GLState)
{
    // Lock the cache
    Threading::SpinLockGuard CacheGuard{m_CacheLock};

    // Construct the key
    VAOHashKey Key{Attribs};

    for (Uint32 SlotMask = Key.UsedSlotsMask; SlotMask != 0;)
    {
        const Uint32 SlotBit = ExtractLSB(SlotMask);
        const Uint32 Slot    = PlatformMisc::GetLSB(SlotBit);

        RefCntAutoPtr<BufferGLImpl>& pBuffer = Attribs.VertexStreams[Slot].pBuffer;
        VERIFY_EXPR(pBuffer);
        VERIFY_EXPR(Key.Streams[Slot].BufferUId == pBuffer->GetUniqueID());

        pBuffer->BufferMemoryBarrier(
            MEMORY_BARRIER_VERTEX_BUFFER, // Vertex data sourced from buffer objects after the barrier
                                          // will reflect data written by shaders prior to the barrier.
                                          // The set of buffer objects affected by this bit is derived
                                          // from the GL_VERTEX_ARRAY_BUFFER_BINDING bindings
            GLState);
    }

    if (Attribs.pIndexBuffer)
    {
        Attribs.pIndexBuffer->BufferMemoryBarrier(
            MEMORY_BARRIER_INDEX_BUFFER, // Vertex array indices sourced from buffer objects after the barrier
                                         // will reflect data written by shaders prior to the barrier.
                                         // The buffer objects affected by this bit are derived from the
                                         // ELEMENT_ARRAY_BUFFER binding.
            GLState);
    }

    // Try to find VAO in the map
    auto It = m_Cache.find(Key);
    if (It != m_Cache.end())
    {
        return It->second;
    }
    else
    {
        // Create a new VAO
        GLObjectWrappers::GLVertexArrayObj NewVAO{true};

        // Initialize VAO
        GLState.BindVAO(NewVAO);

        const InputLayoutDesc& InputLayout = Attribs.PSO.GetGraphicsPipelineDesc().InputLayout;
        const LayoutElement*   LayoutElems = InputLayout.LayoutElements;
        for (Uint32 i = 0; i < InputLayout.NumElements; ++i)
        {
            const LayoutElement& LayoutElem = LayoutElems[i];
            const Uint32         BuffSlot   = LayoutElem.BufferSlot;
            VERIFY_EXPR(BuffSlot < Attribs.NumVertexStreams);

            // Get buffer through the strong reference. Note that we are not
            // using pointers stored in the key for safety
            const auto&   CurrStream = Attribs.VertexStreams[BuffSlot];
            const Uint32  Stride     = Attribs.PSO.GetBufferStride(BuffSlot);
            BufferGLImpl* pBuffer    = Attribs.VertexStreams[BuffSlot].pBuffer;

            constexpr bool ResetVAO = false;
            GLState.BindBuffer(GL_ARRAY_BUFFER, pBuffer->m_GlBuffer, ResetVAO);
            GLvoid* DataStartOffset = reinterpret_cast<GLvoid*>(StaticCast<size_t>(CurrStream.Offset) + static_cast<size_t>(LayoutElem.RelativeOffset));

            const GLenum GlType = TypeToGLType(LayoutElem.ValueType);
            if (!LayoutElem.IsNormalized &&
                (LayoutElem.ValueType == VT_INT8 ||
                 LayoutElem.ValueType == VT_INT16 ||
                 LayoutElem.ValueType == VT_INT32 ||
                 LayoutElem.ValueType == VT_UINT8 ||
                 LayoutElem.ValueType == VT_UINT16 ||
                 LayoutElem.ValueType == VT_UINT32))
                glVertexAttribIPointer(LayoutElem.InputIndex, LayoutElem.NumComponents, GlType, Stride, DataStartOffset);
            else
                glVertexAttribPointer(LayoutElem.InputIndex, LayoutElem.NumComponents, GlType, LayoutElem.IsNormalized, Stride, DataStartOffset);

            if (LayoutElem.Frequency == INPUT_ELEMENT_FREQUENCY_PER_INSTANCE)
            {
                // If divisor is zero, then the attribute acts like normal, being indexed by the array or index
                // buffer. If divisor is non-zero, then the current instance is divided by this divisor, and
                // the result of that is used to access the attribute array.
                glVertexAttribDivisor(LayoutElem.InputIndex, LayoutElem.InstanceDataStepRate);
            }
            glEnableVertexAttribArray(LayoutElem.InputIndex);
        }
        if (Attribs.pIndexBuffer)
        {
            constexpr bool ResetVAO = false;
            GLState.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, Attribs.pIndexBuffer->m_GlBuffer, ResetVAO);
        }

        auto NewElems = m_Cache.emplace(std::make_pair(Key, std::move(NewVAO)));
        // New element must be actually inserted
        VERIFY(NewElems.second, "New element was not inserted into the cache");
        VERIFY_EXPR(Key.PsoUId == Attribs.PSO.GetUniqueID());
        m_PSOToKey[Key.PsoUId].push_back(Key);

        if (Attribs.pIndexBuffer)
        {
            VERIFY_EXPR(Key.IndexBufferUId == Attribs.pIndexBuffer->GetUniqueID());
            m_BuffToKey[Key.IndexBufferUId].push_back(Key);
        }

        for (Uint32 SlotMask = Key.UsedSlotsMask; SlotMask != 0;)
        {
            const Uint32 SlotBit = ExtractLSB(SlotMask);
            const Uint32 Slot    = PlatformMisc::GetLSB(SlotBit);

#ifdef DILIGENT_DEBUG
            {
                const RefCntAutoPtr<BufferGLImpl>& pBuffer = Attribs.VertexStreams[Slot].pBuffer;
                VERIFY_EXPR(pBuffer);
                VERIFY_EXPR(Key.Streams[Slot].BufferUId == pBuffer->GetUniqueID());
            }
#endif

            m_BuffToKey[Key.Streams[Slot].BufferUId].push_back(Key);
        }
        return NewElems.first->second;
    }
}

const GLObjectWrappers::GLVertexArrayObj& VAOCache::GetEmptyVAO()
{
    return m_EmptyVAO;
}

} // namespace Diligent
