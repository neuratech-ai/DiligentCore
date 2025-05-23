/*
 *  Copyright 2019-2025 Diligent Graphics LLC
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

#include "ArchiverImpl.hpp"
#include "Archiver_Inc.hpp"

#include <vector>

#include "PSOSerializer.hpp"

namespace Diligent
{

static DeviceObjectArchive::ResourceType PipelineTypeToArchiveResourceType(PIPELINE_TYPE PipelineType)
{
    using ResourceType = DeviceObjectArchive::ResourceType;
    static_assert(PIPELINE_TYPE_COUNT == 5, "Please handle the new pipeline type below");
    switch (PipelineType)
    {
        case PIPELINE_TYPE_GRAPHICS:
        case PIPELINE_TYPE_MESH:
            return ResourceType::GraphicsPipeline;

        case PIPELINE_TYPE_COMPUTE:
            return ResourceType::ComputePipeline;

        case PIPELINE_TYPE_RAY_TRACING:
            return ResourceType::RayTracingPipeline;

        case PIPELINE_TYPE_TILE:
            return ResourceType::TilePipeline;

        default:
            UNEXPECTED("Unexpected pipeline type");
            return ResourceType::Undefined;
    }
}

ArchiverImpl::ArchiverImpl(IReferenceCounters*      pRefCounters,
                           SerializationDeviceImpl* pDevice) :
    TBase{pRefCounters},
    m_pSerializationDevice{pDevice}
{}

ArchiverImpl::~ArchiverImpl()
{
}

Bool ArchiverImpl::SerializeToBlob(Uint32 ContentVersion, IDataBlob** ppBlob)
{
    DEV_CHECK_ERR(ppBlob != nullptr, "ppBlob must not be null");
    if (ppBlob == nullptr)
        return false;

    DeviceObjectArchive Archive{ContentVersion};

    // A hash map that maps shader byte code to the index in the archive, for each device type
    std::array<std::unordered_map<size_t, Uint32>, static_cast<size_t>(DeviceType::Count)> BytecodeHashToIdx;

    // Add pipelines and patched shaders
    for (const auto& pso_it : m_Pipelines)
    {
        const char*                  Name    = pso_it.first.GetName();
        const ResourceType           ResType = pso_it.first.GetType();
        SerializedPipelineStateImpl& SrcPSO  = *pso_it.second;

        const PIPELINE_STATE_STATUS PSOStatus = SrcPSO.GetStatus(/*WaitForCompletion = */ true);
        if (PSOStatus != PIPELINE_STATE_STATUS_READY)
        {
            LOG_ERROR_MESSAGE("Pipeline state '", Name, "' is in ", GetPipelineStateStatusString(PSOStatus),
                              " state and cannot be serialized. Only ready pipeline states can be serialized."
                              " Use GetStatus() to check the pipeline state status before calling SerializeToBlob().");
            continue;
        }

        if (!SrcPSO.GetData().DoNotPackSignatures)
        {
            const SerializedPipelineStateImpl::SignaturesVector& Signatures = SrcPSO.GetSignatures();
            for (const RefCntAutoPtr<IPipelineResourceSignature>& pSign : Signatures)
            {
                if (!AddPipelineResourceSignature(pSign))
                {
                    LOG_ERROR_MESSAGE("Failed to add pipeline resource signature '", pSign->GetDesc().Name, "' to the archive.");
                }
            }
        }

        const SerializedPipelineStateImpl::Data& SrcData = SrcPSO.GetData();
        VERIFY_EXPR(SafeStrEqual(Name, SrcPSO.GetDesc().Name));
        VERIFY_EXPR(ResType == PipelineTypeToArchiveResourceType(SrcPSO.GetDesc().PipelineType));

        ResourceData& DstData = Archive.GetResourceData(ResType, Name);
        // Add PSO common data
        // NB: since the Archive object is temporary, we do not need to copy the data
        DstData.Common = SerializedData{SrcData.Common.Ptr(), SrcData.Common.Size()};

        // Add shaders for each device type, if present
        for (size_t device_type = 0; device_type < SrcData.Shaders.size(); ++device_type)
        {
            const auto& SrcShaders = SrcData.Shaders[device_type];
            if (SrcShaders.empty())
                continue; // No shaders for this device type

            auto& DstShaders = Archive.GetDeviceShaders(static_cast<DeviceType>(device_type));

            std::vector<Uint32> ShaderIndices;
            ShaderIndices.reserve(SrcShaders.size());
            for (const SerializedPipelineStateImpl::Data::ShaderInfo& SrcShader : SrcShaders)
            {
                VERIFY_EXPR(SrcShader.Data);

                auto it_inserted = BytecodeHashToIdx[device_type].emplace(SrcShader.Hash, StaticCast<Uint32>(DstShaders.size()));
                if (it_inserted.second)
                {
                    // New byte code - add it
                    DstShaders.emplace_back(SrcShader.Data.Ptr(), SrcShader.Data.Size());
                }
                ShaderIndices.emplace_back(it_inserted.first->second);
            }

            DeviceObjectArchive::ShaderIndexArray Indices{ShaderIndices.data(), StaticCast<Uint32>(ShaderIndices.size())};

            // For pipelines, device-specific data is the shader indices
            SerializedData& SerializedIndices = DstData.DeviceSpecific[device_type];

            Serializer<SerializerMode::Measure> MeasureSer;
            PSOSerializer<SerializerMode::Measure>::SerializeShaderIndices(MeasureSer, Indices, nullptr);
            SerializedIndices = MeasureSer.AllocateData(GetRawAllocator());

            Serializer<SerializerMode::Write> Ser{SerializedIndices};
            PSOSerializer<SerializerMode::Write>::SerializeShaderIndices(Ser, Indices, nullptr);
            VERIFY_EXPR(Ser.IsEnded());
        }
    }

    // Add resource signatures
    for (const auto& sign_it : m_Signatures)
    {
        const char*                            Name    = sign_it.first.GetStr();
        const SerializedResourceSignatureImpl& SrcSign = *sign_it.second;
        VERIFY_EXPR(SafeStrEqual(Name, SrcSign.GetDesc().Name));
        const SerializedData& SrcCommonData = SrcSign.GetCommonData();

        ResourceData& DstData = Archive.GetResourceData(ResourceType::ResourceSignature, Name);
        // NB: since the Archive object is temporary, we do not need to copy the data
        DstData.Common = SerializedData{SrcCommonData.Ptr(), SrcCommonData.Size()};

        for (size_t device_type = 0; device_type < static_cast<size_t>(DeviceType::Count); ++device_type)
        {
            if (const SerializedData* pMem = SrcSign.GetDeviceData(static_cast<DeviceType>(device_type)))
                DstData.DeviceSpecific[device_type] = SerializedData{pMem->Ptr(), pMem->Size()};
        }
    }

    // Add render passes
    for (const auto& rp_it : m_RenderPasses)
    {
        const char*                     Name  = rp_it.first.GetStr();
        const SerializedRenderPassImpl& SrcRP = *rp_it.second;
        VERIFY_EXPR(SafeStrEqual(Name, SrcRP.GetDesc().Name));
        const SerializedData& SrcData = SrcRP.GetCommonData();

        ResourceData& DstData = Archive.GetResourceData(ResourceType::RenderPass, Name);
        DstData.Common        = SerializedData{SrcData.Ptr(), SrcData.Size()};
    }

    // Add standalone shaders
    for (const auto& shader_it : m_Shaders)
    {
        const char*           Name      = shader_it.first.GetStr();
        SerializedShaderImpl& SrcShader = *shader_it.second;
        {
            const SHADER_STATUS Status = SrcShader.GetStatus(/*WaitForCompletion = */ true);
            if (Status != SHADER_STATUS_READY)
            {
                LOG_ERROR_MESSAGE("Shader '", Name, "' is in ", GetShaderStatusString(Status),
                                  " state and cannot be serialized. Only ready shaders can be serialized."
                                  " Use GetStatus() to check the shader status before calling SerializeToBlob().");
                continue;
            }
        }
        VERIFY_EXPR(SafeStrEqual(Name, SrcShader.GetDesc().Name));

        ResourceData& DstData = Archive.GetResourceData(ResourceType::StandaloneShader, Name);
        DstData.Common        = SrcShader.GetCommonData();

        for (size_t device_type = 0; device_type < static_cast<size_t>(DeviceType::Count); ++device_type)
        {
            SerializedData DeviceData = SrcShader.GetDeviceData(static_cast<DeviceType>(device_type));
            if (!DeviceData)
                continue;

            auto& DstShaders  = Archive.GetDeviceShaders(static_cast<DeviceType>(device_type));
            auto  it_inserted = BytecodeHashToIdx[device_type].emplace(DeviceData.GetHash(), StaticCast<Uint32>(DstShaders.size()));
            if (it_inserted.second)
            {
                // New byte code
                DstShaders.emplace_back(std::move(DeviceData));
            }
            const Uint32 Index = it_inserted.first->second;

            // For shaders, device-specific data is the serialized shader bytecode index
            SerializedData& SerializedIndex = DstData.DeviceSpecific[device_type];

            Serializer<SerializerMode::Measure> MeasureSer;
            MeasureSer(Index);
            SerializedIndex = MeasureSer.AllocateData(GetRawAllocator());

            Serializer<SerializerMode::Write> Ser{SerializedIndex};
            Ser(Index);
            VERIFY_EXPR(Ser.IsEnded());
        }
    }

    Archive.Serialize(ppBlob);

    return *ppBlob != nullptr;
}


Bool ArchiverImpl::SerializeToStream(Uint32 ContentVersion, IFileStream* pStream)
{
    DEV_CHECK_ERR(pStream != nullptr, "pStream must not be null");
    if (pStream == nullptr)
        return false;

    RefCntAutoPtr<IDataBlob> pDataBlob;
    if (!SerializeToBlob(ContentVersion, &pDataBlob))
        return false;

    return pStream->Write(pDataBlob->GetConstDataPtr(), pDataBlob->GetSize());
}

template <typename ObjectImplType,
          typename IfaceType>
bool AddObjectToArchive(IfaceType*                                                           pObject,
                        const char*                                                          ObjectTypeStr,
                        const INTERFACE_ID&                                                  SerializedObjIID,
                        std::mutex&                                                          Mtx,
                        std::unordered_map<HashMapStringKey, RefCntAutoPtr<ObjectImplType>>& Objects)
{
    DEV_CHECK_ERR(pObject != nullptr, ObjectTypeStr, " must not be null");
    if (pObject == nullptr)
        return false;

    RefCntAutoPtr<ObjectImplType> pSerializedObj{pObject, SerializedObjIID};
    if (!pSerializedObj)
    {
        UNEXPECTED(ObjectTypeStr, " '", pObject->GetDesc().Name, "' was not created by a serialization device.");
        return false;
    }
    const char* Name = pSerializedObj->GetDesc().Name;

    std::lock_guard<std::mutex> Guard{Mtx};

    auto it_inserted = Objects.emplace(HashMapStringKey{Name, true}, pSerializedObj);
    if (!it_inserted.second && it_inserted.first->second != pSerializedObj)
    {
        if (*it_inserted.first->second != *pSerializedObj)
        {
            LOG_ERROR_MESSAGE(ObjectTypeStr, " with name '", Name, "' is already present in the archive. All objects must use distinct names.");
            return false;
        }
    }

    return true;
}

Bool ArchiverImpl::AddShader(IShader* pShader)
{
    if (pShader == nullptr)
        return false;

    return AddObjectToArchive<SerializedShaderImpl>(pShader, "Shader", IID_SerializedShader, m_ShadersMtx, m_Shaders);
}

bool ArchiverImpl::AddPipelineResourceSignature(IPipelineResourceSignature* pPRS)
{
    if (pPRS == nullptr)
        return false;

    return AddObjectToArchive<SerializedResourceSignatureImpl>(pPRS, "Pipeline resource signature", IID_SerializedResourceSignature, m_SignaturesMtx, m_Signatures);
}

bool ArchiverImpl::AddRenderPass(IRenderPass* pRP)
{
    if (pRP == nullptr)
        return false;

    return AddObjectToArchive<SerializedRenderPassImpl>(pRP, "Render pass", IID_SerializedRenderPass, m_RenderPassesMtx, m_RenderPasses);
}

Bool ArchiverImpl::AddPipelineState(IPipelineState* pPSO)
{
    if (pPSO == nullptr)
        return false;

    RefCntAutoPtr<SerializedPipelineStateImpl> pSerializedPSO{pPSO, IID_SerializedPipelineState};
    if (!pSerializedPSO)
    {
        UNEXPECTED("Pipeline state '", pPSO->GetDesc().Name, "' was not created by a serialization device.");
        return false;
    }

    const PipelineStateDesc& Desc = pSerializedPSO->GetDesc();
    const char*              Name = Desc.Name;
    // Mesh pipelines are serialized as graphics pipelines
    const ResourceType ArchiveResType = PipelineTypeToArchiveResourceType(Desc.PipelineType);

    {
        std::lock_guard<std::mutex> Guard{m_PipelinesMtx};

        auto it_inserted = m_Pipelines.emplace(NamedResourceKey{ArchiveResType, Name, true}, pSerializedPSO);
        if (!it_inserted.second)
        {
            LOG_ERROR_MESSAGE("Pipeline state with name '", Name, "' is already present in the archive. All pipelines of the same type must have unique names.");
            return false;
        }
    }

    bool Res = true;
    if (IRenderPass* pRenderPass = pSerializedPSO->GetRenderPass())
    {
        if (!AddRenderPass(pRenderPass))
            Res = false;
    }

    return Res;
}

void ArchiverImpl::Reset()
{
    {
        std::lock_guard<std::mutex> Guard{m_SignaturesMtx};
        m_Signatures.clear();
    }

    {
        std::lock_guard<std::mutex> Guard{m_RenderPassesMtx};
        m_RenderPasses.clear();
    }

    {
        std::lock_guard<std::mutex> Guard{m_PipelinesMtx};
        m_Pipelines.clear();
    }

    {
        std::lock_guard<std::mutex> Guard{m_ShadersMtx};
        m_Shaders.clear();
    }
}

IShader* ArchiverImpl::GetShader(const char* Name)
{
    std::lock_guard<std::mutex> Guard{m_ShadersMtx};

    auto it = m_Shaders.find(Name);
    return it != m_Shaders.end() ? it->second.RawPtr() : nullptr;
}

static DeviceObjectArchive::ResourceType PiplineTypeToArchiveResourceType(PIPELINE_TYPE Type)
{
    using ResourceType = DeviceObjectArchive::ResourceType;

    static_assert(PIPELINE_TYPE_COUNT == 5, "Did you add a new pipeline type? Please handle it here.");
    switch (Type)
    {
        case PIPELINE_TYPE_GRAPHICS:
        case PIPELINE_TYPE_MESH:
            return ResourceType::GraphicsPipeline;

        case PIPELINE_TYPE_COMPUTE:
            return ResourceType::ComputePipeline;

        case PIPELINE_TYPE_RAY_TRACING:
            return ResourceType::RayTracingPipeline;

        case PIPELINE_TYPE_TILE:
            return ResourceType::TilePipeline;

        default:
            return ResourceType::Undefined;
    }
}

IPipelineState* ArchiverImpl::GetPipelineState(PIPELINE_TYPE PSOType,
                                               const char*   PSOName)
{
    std::lock_guard<std::mutex> Guard{m_PipelinesMtx};

    const ResourceType ResType = PiplineTypeToArchiveResourceType(PSOType);
    if (ResType == ResourceType::Undefined)
    {
        UNEXPECTED("Unexpected pipeline type");
        return nullptr;
    }

    auto it = m_Pipelines.find(NamedResourceKey{ResType, PSOName});
    return it != m_Pipelines.end() ? it->second.RawPtr() : nullptr;
}

IPipelineResourceSignature* ArchiverImpl::GetPipelineResourceSignature(const char* PRSName)
{
    std::lock_guard<std::mutex> Guard{m_SignaturesMtx};

    auto it = m_Signatures.find(PRSName);
    return it != m_Signatures.end() ? it->second.RawPtr() : nullptr;
}

} // namespace Diligent
