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
#include "Texture1D_D3D11.hpp"
#include "RenderDeviceD3D11Impl.hpp"
#include "D3D11TypeConversions.hpp"

namespace Diligent
{

Texture1D_D3D11::Texture1D_D3D11(IReferenceCounters*        pRefCounters,
                                 FixedBlockMemoryAllocator& TexViewObjAllocator,
                                 RenderDeviceD3D11Impl*     pRenderDeviceD3D11,
                                 const TextureDesc&         TexDesc,
                                 const TextureData*         pInitData /*= nullptr*/) :
    // clang-format off
    TextureBaseD3D11
    {
        pRefCounters,
        TexViewObjAllocator,
        pRenderDeviceD3D11,
        TexDesc,
        pInitData
    }
// clang-format on
{
    const DXGI_FORMAT D3D11TexFormat      = TexFormatToDXGI_Format(m_Desc.Format, m_Desc.BindFlags);
    const UINT        D3D11BindFlags      = BindFlagsToD3D11BindFlags(m_Desc.BindFlags);
    const UINT        D3D11CPUAccessFlags = CPUAccessFlagsToD3D11CPUAccessFlags(m_Desc.CPUAccessFlags);
    const D3D11_USAGE D3D11Usage          = UsageToD3D11Usage(m_Desc.Usage);
    UINT              MiscFlags           = MiscTextureFlagsToD3D11Flags(m_Desc.MiscFlags);

    if (m_Desc.Usage == USAGE_SPARSE)
        MiscFlags |= D3D11_RESOURCE_MISC_TILED;

    // clang-format off
    D3D11_TEXTURE1D_DESC Tex1DDesc =
    {
        m_Desc.Width,
        m_Desc.MipLevels,
        m_Desc.ArraySize,
        D3D11TexFormat,
        D3D11Usage,
        D3D11BindFlags,
        D3D11CPUAccessFlags,
        MiscFlags
    };
    // clang-format on

    std::vector<D3D11_SUBRESOURCE_DATA, STDAllocatorRawMem<D3D11_SUBRESOURCE_DATA>> D3D11InitData(STD_ALLOCATOR_RAW_MEM(D3D11_SUBRESOURCE_DATA, GetRawAllocator(), "Allocator for vector<D3D11_SUBRESOURCE_DATA>"));
    PrepareD3D11InitData(pInitData, Tex1DDesc.ArraySize * Tex1DDesc.MipLevels, D3D11InitData);

    ID3D11Device* pDeviceD3D11 = pRenderDeviceD3D11->GetD3D11Device();

    CComPtr<ID3D11Texture1D> ptex1D;
    HRESULT                  hr = pDeviceD3D11->CreateTexture1D(&Tex1DDesc, D3D11InitData.size() ? D3D11InitData.data() : nullptr, &ptex1D);
    CHECK_D3D_RESULT_THROW(hr, "Failed to create the Direct3D11 Texture1D");
    m_pd3d11Texture = std::move(ptex1D);

    if (*m_Desc.Name != 0)
    {
        hr = m_pd3d11Texture->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen(m_Desc.Name)), m_Desc.Name);
        DEV_CHECK_ERR(SUCCEEDED(hr), "Failed to set texture name");
    }

    if (m_Desc.Usage == USAGE_SPARSE)
        InitSparseProperties();
}

namespace
{

class TexDescFromD3D11Texture1D
{
public:
    TextureDesc operator()(ID3D11Texture1D* pd3d11Texture)
    {
        D3D11_TEXTURE1D_DESC D3D11TexDesc;
        pd3d11Texture->GetDesc(&D3D11TexDesc);

        TextureDesc TexDesc;

        UINT DataSize = 0;
        pd3d11Texture->GetPrivateData(WKPDID_D3DDebugObjectName, &DataSize, nullptr);
        if (DataSize > 0)
        {
            ObjectName.resize(size_t{DataSize} + 1); // Null terminator is not reported in DataSize
            pd3d11Texture->GetPrivateData(WKPDID_D3DDebugObjectName, &DataSize, ObjectName.data());
            TexDesc.Name = ObjectName.data();
        }
        else
            TexDesc.Name = "Texture1D_D3D11 from native d3d11 texture";

        TexDesc.Type           = D3D11TexDesc.ArraySize > 1 ? RESOURCE_DIM_TEX_1D_ARRAY : RESOURCE_DIM_TEX_1D;
        TexDesc.Width          = Uint32{D3D11TexDesc.Width};
        TexDesc.Height         = 1;
        TexDesc.ArraySize      = Uint32{D3D11TexDesc.ArraySize};
        TexDesc.Format         = DXGI_FormatToTexFormat(D3D11TexDesc.Format);
        TexDesc.MipLevels      = Uint32{D3D11TexDesc.MipLevels};
        TexDesc.SampleCount    = 1;
        TexDesc.Usage          = D3D11UsageToUsage(D3D11TexDesc.Usage);
        TexDesc.BindFlags      = D3D11BindFlagsToBindFlags(D3D11TexDesc.BindFlags);
        TexDesc.CPUAccessFlags = D3D11CPUAccessFlagsToCPUAccessFlags(D3D11TexDesc.CPUAccessFlags);
        TexDesc.MiscFlags      = D3D11MiscFlagsToMiscTextureFlags(D3D11TexDesc.MiscFlags);

        return TexDesc;
    }

private:
    std::vector<char> ObjectName;
};

} // namespace

Texture1D_D3D11::Texture1D_D3D11(IReferenceCounters*        pRefCounters,
                                 FixedBlockMemoryAllocator& TexViewObjAllocator,
                                 RenderDeviceD3D11Impl*     pDeviceD3D11,
                                 RESOURCE_STATE             InitialState,
                                 ID3D11Texture1D*           pd3d11Texture) :
    TextureBaseD3D11{pRefCounters, TexViewObjAllocator, pDeviceD3D11, TexDescFromD3D11Texture1D{}(pd3d11Texture), nullptr}
{
    m_pd3d11Texture = pd3d11Texture;
    SetState(InitialState);

    if (m_Desc.Usage == USAGE_SPARSE)
        InitSparseProperties();
}

Texture1D_D3D11::~Texture1D_D3D11()
{
}

void Texture1D_D3D11::CreateSRV(const TextureViewDesc& SRVDesc, ID3D11ShaderResourceView** ppD3D11SRV)
{
    VERIFY(ppD3D11SRV && *ppD3D11SRV == nullptr, "SRV pointer address is null or contains non-null pointer to an existing object");

    VERIFY(SRVDesc.ViewType == TEXTURE_VIEW_SHADER_RESOURCE, "Incorrect view type: shader resource is expected");
    if (!(SRVDesc.TextureDim == RESOURCE_DIM_TEX_1D || SRVDesc.TextureDim == RESOURCE_DIM_TEX_1D_ARRAY))
        LOG_ERROR_AND_THROW("Unsupported texture type. Only RESOURCE_DIM_TEX_1D or RESOURCE_DIM_TEX_1D_ARRAY is allowed");

    VERIFY_EXPR(SRVDesc.Format != TEX_FORMAT_UNKNOWN);

    D3D11_SHADER_RESOURCE_VIEW_DESC D3D11_SRVDesc;
    TextureViewDesc_to_D3D11_SRV_DESC(SRVDesc, D3D11_SRVDesc, m_Desc.SampleCount);

    ID3D11Device* pd3d11Device = GetDevice()->GetD3D11Device();
    CHECK_D3D_RESULT_THROW(pd3d11Device->CreateShaderResourceView(m_pd3d11Texture, &D3D11_SRVDesc, ppD3D11SRV),
                           "Failed to create D3D11 shader resource view");
}

void Texture1D_D3D11::CreateRTV(const TextureViewDesc& RTVDesc, ID3D11RenderTargetView** ppD3D11RTV)
{
    VERIFY(ppD3D11RTV && *ppD3D11RTV == nullptr, "RTV pointer address is null or contains non-null pointer to an existing object");

    VERIFY(RTVDesc.ViewType == TEXTURE_VIEW_RENDER_TARGET, "Incorrect view type: render target is expected");
    if (!(RTVDesc.TextureDim == RESOURCE_DIM_TEX_1D || RTVDesc.TextureDim == RESOURCE_DIM_TEX_1D_ARRAY))
        LOG_ERROR_AND_THROW("Unsupported texture type. Only RESOURCE_DIM_TEX_1D or RESOURCE_DIM_TEX_1D_ARRAY is allowed");

    VERIFY_EXPR(RTVDesc.Format != TEX_FORMAT_UNKNOWN);

    D3D11_RENDER_TARGET_VIEW_DESC D3D11_RTVDesc;
    TextureViewDesc_to_D3D11_RTV_DESC(RTVDesc, D3D11_RTVDesc, m_Desc.SampleCount);

    ID3D11Device* pd3d11Device = GetDevice()->GetD3D11Device();
    CHECK_D3D_RESULT_THROW(pd3d11Device->CreateRenderTargetView(m_pd3d11Texture, &D3D11_RTVDesc, ppD3D11RTV),
                           "Failed to create D3D11 render target view");
}

void Texture1D_D3D11::CreateDSV(const TextureViewDesc& DSVDesc, ID3D11DepthStencilView** ppD3D11DSV)
{
    VERIFY(ppD3D11DSV && *ppD3D11DSV == nullptr, "DSV pointer address is null or contains non-null pointer to an existing object");

    VERIFY(DSVDesc.ViewType == TEXTURE_VIEW_DEPTH_STENCIL || DSVDesc.ViewType == TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL,
           "Incorrect view type: depth-stencil or read-only depth-stencil is expected");
    if (!(DSVDesc.TextureDim == RESOURCE_DIM_TEX_1D || DSVDesc.TextureDim == RESOURCE_DIM_TEX_1D_ARRAY))
        LOG_ERROR_AND_THROW("Unsupported texture type. Only RESOURCE_DIM_TEX_1D or RESOURCE_DIM_TEX_1D_ARRAY is allowed");

    VERIFY_EXPR(DSVDesc.Format != TEX_FORMAT_UNKNOWN);

    D3D11_DEPTH_STENCIL_VIEW_DESC D3D11_DSVDesc;
    TextureViewDesc_to_D3D11_DSV_DESC(DSVDesc, D3D11_DSVDesc, m_Desc.SampleCount);

    ID3D11Device* pd3d11Device = GetDevice()->GetD3D11Device();
    CHECK_D3D_RESULT_THROW(pd3d11Device->CreateDepthStencilView(m_pd3d11Texture, &D3D11_DSVDesc, ppD3D11DSV),
                           "Failed to create D3D11 depth stencil view");
}

void Texture1D_D3D11::CreateUAV(const TextureViewDesc& UAVDesc, ID3D11UnorderedAccessView** ppD3D11UAV)
{
    VERIFY(ppD3D11UAV && *ppD3D11UAV == nullptr, "UAV pointer address is null or contains non-null pointer to an existing object");

    VERIFY(UAVDesc.ViewType == TEXTURE_VIEW_UNORDERED_ACCESS, "Incorrect view type: unordered access is expected");
    if (!(UAVDesc.TextureDim == RESOURCE_DIM_TEX_1D || UAVDesc.TextureDim == RESOURCE_DIM_TEX_1D_ARRAY))
        LOG_ERROR_AND_THROW("Unsupported texture type. Only RESOURCE_DIM_TEX_1D or RESOURCE_DIM_TEX_1D_ARRAY is allowed");

    VERIFY_EXPR(UAVDesc.Format != TEX_FORMAT_UNKNOWN);

    D3D11_UNORDERED_ACCESS_VIEW_DESC D3D11_UAVDesc;
    TextureViewDesc_to_D3D11_UAV_DESC(UAVDesc, D3D11_UAVDesc);

    ID3D11Device* pd3d11Device = GetDevice()->GetD3D11Device();
    CHECK_D3D_RESULT_THROW(pd3d11Device->CreateUnorderedAccessView(m_pd3d11Texture, &D3D11_UAVDesc, ppD3D11UAV),
                           "Failed to create D3D11 unordered access view");
}

} // namespace Diligent
