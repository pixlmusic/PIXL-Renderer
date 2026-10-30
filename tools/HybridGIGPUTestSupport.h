#pragma once
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D failure: " + std::to_string(hr)); }
static std::string Read(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error(path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
struct Texture {
    unsigned rowBytes{4096};
    ComPtr<ID3D11Texture2D> resource, staging;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    void Create(ID3D11Device* device, DXGI_FORMAT format=DXGI_FORMAT_R32_UINT) {
        rowBytes = format==DXGI_FORMAT_R16G16B16A16_FLOAT ? 8192 : 4096;
        D3D11_TEXTURE2D_DESC d{}; d.Width=1024; d.Height=64; d.MipLevels=1; d.ArraySize=1;
        d.Format=format; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        Check(device->CreateTexture2D(&d,nullptr,&resource));
        Check(device->CreateShaderResourceView(resource.Get(),nullptr,&srv));
        Check(device->CreateUnorderedAccessView(resource.Get(),nullptr,&uav));
        d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Check(device->CreateTexture2D(&d,nullptr,&staging));
    }
    std::vector<std::uint32_t> Readback(ID3D11DeviceContext* context) {
        context->CopyResource(staging.Get(), resource.Get());
        D3D11_MAPPED_SUBRESOURCE m{}; Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));
        std::vector<std::uint32_t> result(64*rowBytes/4);
        for(unsigned y=0;y<64;++y) std::memcpy(result.data()+y*rowBytes/4,static_cast<const char*>(m.pData)+y*m.RowPitch,rowBytes);
        context->Unmap(staging.Get(),0); return result;
    }
};
struct Buffer {
    ComPtr<ID3D11Buffer> resource;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    void Create(ID3D11Device* device,unsigned count,unsigned stride) {
        D3D11_BUFFER_DESC d{}; d.ByteWidth=count*stride; d.StructureByteStride=stride;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS; d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        Check(device->CreateBuffer(&d,nullptr,&resource));
        Check(device->CreateShaderResourceView(resource.Get(),nullptr,&srv));
        Check(device->CreateUnorderedAccessView(resource.Get(),nullptr,&uav));
    }
};
