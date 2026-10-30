// Offscreen validation of Water.hlsl. Native permutations are the active path;
// the HS/DS exercise below is an isolated test, not proof of in-game binding.
#include "Modules/OceanDynamicsGeometry.h"
#include <cstring>

void ValidateOceanGeometry(ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* displacement)
{
    for (const auto* technique : { "SPECULAR", "STENCIL", "UNDERWATER", "LOD" }) {
        for (const bool pixel : { false, true }) {
            const D3D_SHADER_MACRO defines[]{
                {pixel ? "PSHADER" : "VSHADER", "1"}, {technique, "1"},
                {"WINPC", "1"}, {"DX11", "1"}, {"NUM_SPECULAR_LIGHTS", "0"},
                {"WATERBODY", "1"}, {"FOG", "1"}, {"OCEAN_DYNAMICS", "1"},
                // LOD/stencil layouts do not carry refraction MPosition.
                {(std::strcmp(technique, "SPECULAR") == 0 || std::strcmp(technique, "UNDERWATER") == 0) ?
                    "REFRACTIONS" : "OCEAN_TEST_NO_REFRACTIONS", "1"}, {nullptr, nullptr} };
            ComPtr<ID3DBlob> code, errors;
            const auto result = D3DCompileFromFile(L"distribution/PIXL-Shaders/Water.hlsl", defines,
                D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", pixel ? "ps_5_0" : "vs_5_0",
                D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &errors);
            if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            Check(result);
        }
    }
    std::cout << "Native water VS/PS permutations: specular, stencil, underwater and LOD compiled\n";

    // Ocean spray is compiled at runtime by OceanDynamics, so validate the
    // packaged source entry points here as well as the FFT and Water stages.
    const auto originalDirectory = std::filesystem::current_path();
    struct RestoreWorkingDirectory
    {
        std::filesystem::path path;
        ~RestoreWorkingDirectory() { std::error_code error; std::filesystem::current_path(path, error); }
    } restoreDirectory{originalDirectory};
    std::filesystem::current_path(originalDirectory / "distribution" / "PIXL-Shaders");
    for (const auto [entry, target] : std::array<std::array<const char*, 2>, 3>{{
        {{"UpdateSpray", "cs_5_0"}}, {{"SprayVS", "vs_5_0"}}, {{"SprayPS", "ps_5_0"}} }}) {
        const char* stageMacro = target[0] == 'c' ? "COMPUTESHADER" :
            (target[0] == 'v' ? "VSHADER" : "PSHADER");
        // Match Util::CompileShader, which publishes the active shader stage to
        // source files before selecting an entry point. OceanSpray uses that
        // contract to expose its RW particle state only to the compute stage.
        const D3D_SHADER_MACRO macros[]{
            {"WINPC", "1"}, {"DX11", "1"}, {stageMacro, "1"}, {nullptr, nullptr}};
        ComPtr<ID3DBlob> code, errors;
        const auto result = D3DCompileFromFile(
            L"OceanDynamics/OceanSpray.hlsl", macros,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target,
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
                D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, &code, &errors);
        if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        Check(result);
    }
    std::cout << "Ocean spray compute, vertex and pixel stages compiled\n";
    std::filesystem::current_path(originalDirectory);

    std::vector<OceanGeometry::LodTile> lodTiles;
    if (!OceanGeometry::BuildLodTileRings(-98000.0f, 136000.0f, 512.0f, 8, lodTiles) ||
        lodTiles.size() != 100)
        throw std::runtime_error("Crest-style ocean LOD rings did not produce the expected 100 tiles");
    std::array<std::uint32_t, 8> tilesPerLod{};
    double coveredArea = 0.0;
    for (const auto& tile : lodTiles) {
        if (!std::isfinite(tile.centerX) || !std::isfinite(tile.centerY) ||
            !std::isfinite(tile.size) || tile.size != 512.0f * std::ldexp(1.0f, static_cast<int>(tile.level)) ||
            tile.level >= tilesPerLod.size())
            throw std::runtime_error("Crest-style ocean LOD ring emitted an invalid tile");
        ++tilesPerLod[tile.level];
        coveredArea += static_cast<double>(tile.size) * tile.size;
    }
    if (tilesPerLod[0] != 16 || std::any_of(tilesPerLod.begin() + 1, tilesPerLod.end(),
        [](std::uint32_t count) { return count != 12; }))
        throw std::runtime_error("Crest-style LOD levels must have a 4x4 core and 12-tile outer rings");
    // Eight LODs cover a square with half-extent 256 base tiles.
    constexpr double expectedArea = 512.0 * 512.0 * 262144.0;
    if (std::abs(coveredArea - expectedArea) > expectedArea * 1.0e-6)
        throw std::runtime_error("Crest-style ocean LOD rings do not cover the expected nested area");

    for (const auto [x, y] : std::array<std::array<float, 2>, 2>{{{{-98000.0f, 136000.0f}}, {{140000.0f, -42000.0f}}}}) {
        constexpr float root = 81920.0f;
        std::vector<OceanGeometry::QuadtreePatch> patches;
        if (!OceanGeometry::BuildQuadtree(x, y, root, 160.0f, 1536, patches) || patches.empty())
            throw std::runtime_error("Ocean quadtree did not produce a bounded patch set");
        double area = 0.0;
        for (const auto& patch : patches) {
            if (!std::isfinite(patch.centerX) || !std::isfinite(patch.centerY) ||
                !std::isfinite(patch.size) || patch.size < 160.0f)
                throw std::runtime_error("Ocean quadtree emitted invalid patch geometry");
            area += static_cast<double>(patch.size) * patch.size;
        }
        if (std::abs(area - static_cast<double>(root) * root) > static_cast<double>(root) * root * 1.0e-6)
            throw std::runtime_error("Ocean quadtree has a gap or overlapping leaf area");
    }

    auto compileStage = [](const char* stage, const char* entry, const char* target) {
        const D3D_SHADER_MACRO macros[]{ {stage, "1"}, {"WINPC", "1"}, {"DX11", "1"},
            {"SPECULAR", "1"}, {"NUM_SPECULAR_LIGHTS", "0"}, {"WATERBODY", "1"},
            {"FOG", "1"}, {"OCEAN_DYNAMICS", "1"}, {"OCEAN_TESSELLATION", "1"}, {nullptr, nullptr} };
        ComPtr<ID3DBlob> code, errors;
        const auto hr = D3DCompileFromFile(L"distribution/PIXL-Shaders/Water.hlsl", macros,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target,
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, &code, &errors);
        if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        Check(hr);
        return code;
    };
    auto vsCode = compileStage("VSHADER", "main", "vs_5_0");
    auto hsCode = compileStage("HULLSHADER", "OceanHull", "hs_5_0");
    auto dsCode = compileStage("DOMAINSHADER", "OceanDomain", "ds_5_0");
    // The visible top-side clipmap and underwater surface must share the same
    // displaced domain. Compile the underwater hull/domain separately because
    // that runtime permutation intentionally does not define SPECULAR.
    for (const auto [stage, entry, target] : std::array<std::array<const char*, 3>, 2>{{
        {{"HULLSHADER", "OceanHull", "hs_5_0"}},
        {{"DOMAINSHADER", "OceanDomain", "ds_5_0"}} }}) {
        const D3D_SHADER_MACRO macros[]{ {stage, "1"}, {"WINPC", "1"}, {"DX11", "1"},
            {"UNDERWATER", "1"}, {"WATERBODY", "1"}, {"FOG", "1"},
            {"OCEAN_DYNAMICS", "1"}, {"OCEAN_TESSELLATION", "1"}, {nullptr, nullptr} };
        ComPtr<ID3DBlob> code, errors;
        const auto hr = D3DCompileFromFile(L"distribution/PIXL-Shaders/Water.hlsl", macros,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target,
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS |
                D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, &code, &errors);
        if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        Check(hr);
    }
    std::cout << "Underwater clipmap hull/domain permutations compiled with shared swell displacement\n";
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11HullShader> hs;
    ComPtr<ID3D11DomainShader> ds;
    Check(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs));
    Check(device->CreateHullShader(hsCode->GetBufferPointer(), hsCode->GetBufferSize(), nullptr, &hs));
    Check(device->CreateDomainShader(dsCode->GetBufferPointer(), dsCode->GetBufferSize(), nullptr, &ds));
    const char pixelSource[] = "float4 main(float4 p:SV_POSITION, float4 world:TEXCOORD0):SV_Target { return float4(world.xyz,1); }";
    ComPtr<ID3DBlob> psCode;
    Check(D3DCompile(pixelSource, sizeof(pixelSource) - 1, nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psCode, nullptr));
    ComPtr<ID3D11PixelShader> ps;
    Check(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps));
    const D3D11_INPUT_ELEMENT_DESC input{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    ComPtr<ID3D11InputLayout> layout;
    Check(device->CreateInputLayout(&input, 1, vsCode->GetBufferPointer(), vsCode->GetBufferSize(), &layout));
    D3D11_TEXTURE2D_DESC targetDesc{};
    targetDesc.Width = targetDesc.Height = 64;
    targetDesc.MipLevels = targetDesc.ArraySize = targetDesc.SampleDesc.Count = 1;
    targetDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target, readback;
    Check(device->CreateTexture2D(&targetDesc, nullptr, &target));
    ComPtr<ID3D11RenderTargetView> rtv;
    Check(device->CreateRenderTargetView(target.Get(), nullptr, &rtv));
    targetDesc.BindFlags = 0;
    targetDesc.Usage = D3D11_USAGE_STAGING;
    targetDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(device->CreateTexture2D(&targetDesc, nullptr, &readback));
    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> raster;
    Check(device->CreateRasterizerState(&rasterDesc, &raster));
    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    Check(device->CreateSamplerState(&samplerDesc, &sampler));
    auto* rawSampler = sampler.Get();
    context->HSSetSamplers(15, 1, &rawSampler);
    context->DSSetSamplers(15, 1, &rawSampler);
    ID3D11ShaderResourceView* waves[]{displacement, displacement};
    context->HSSetShaderResources(120, 2, waves);
    context->DSSetShaderResources(120, 2, waves);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->HSSetShader(hs.Get(), nullptr, 0);
    context->DSSetShader(ds.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    context->IASetInputLayout(layout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
    context->RSSetState(raster.Get());
    const D3D11_VIEWPORT viewport{0, 0, 64, 64, 0, 1};
    context->RSSetViewports(1, &viewport);
    auto* rawTarget = rtv.Get();
    context->OMSetRenderTargets(1, &rawTarget, nullptr);
    const UINT indices[]{0, 1, 2, 0, 2, 3};
    D3D11_BUFFER_DESC indexDesc{};
    indexDesc.ByteWidth = sizeof(indices);
    indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA indexData{indices, 0, 0};
    ComPtr<ID3D11Buffer> indexBuffer;
    Check(device->CreateBuffer(&indexDesc, &indexData, &indexBuffer));
    context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    std::array<float, 24> material{};
    material[20] = material[21] = material[22] = material[23] = 1.0f;
    auto materialCB = MakeBuffer(device, material.data(), sizeof(material));
    auto* rawMaterial = materialCB.Get();
    context->VSSetConstantBuffers(1, 1, &rawMaterial);
    std::array<float, 20> shore{};
    shore[11] = 1.0f; shore[14] = 8.5f; shore[15] = 70.0f;
    auto shoreCB = MakeBuffer(device, shore.data(), sizeof(shore));
    auto* rawShore = shoreCB.Get();
    // Water.main reads b7 before it publishes OceanBasePosition for the hull and
    // domain stages. Match OceanDynamics::SetOceanData/BeginWaterDraw by binding
    // the bathymetry contract to every stage that consumes it.
    context->VSSetConstantBuffers(7, 1, &rawShore);
    context->HSSetConstantBuffers(7, 1, &rawShore);
    context->DSSetConstantBuffers(7, 1, &rawShore);
    for (float cameraX : {-98000.0f, 140000.0f}) for (float side : {-1.0f, 1.0f}) {
        const float cameraY = 136000.0f, sea = 1000.0f, cameraZ = sea + side * 512.0f;
        const auto center = OceanGeometry::ClipmapCenter(cameraX, cameraY, sea);
        // Nonidentity authored-water transform: local vertices must round-trip
        // through absolute world space before the shader subtracts the camera.
        constexpr float objectX = -98304.0f, objectY = 135168.0f, objectZ = 400.0f, scale = 2.0f;
        std::array<float, 16> vertices{};
        const float offsets[4][2]{{-256,-256},{256,-256},{256,256},{-256,256}};
        for (unsigned v = 0; v < 4; ++v) {
            vertices[v*4] = (center[0] + offsets[v][0] - objectX) / scale;
            vertices[v*4+1] = (center[1] + offsets[v][1] - objectY) / scale;
            vertices[v*4+2] = (center[2] - objectZ) / scale;
            vertices[v*4+3] = 1.0f;
        }
        D3D11_BUFFER_DESC vertexDesc{};
        vertexDesc.ByteWidth = sizeof(vertices); vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertexData{vertices.data(), 0, 0};
        ComPtr<ID3D11Buffer> vertexBuffer;
        Check(device->CreateBuffer(&vertexDesc, &vertexData, &vertexBuffer));
        auto* rawVertex = vertexBuffer.Get();
        const UINT stride = 16, offset = 0;
        context->IASetVertexBuffers(0, 1, &rawVertex, &stride, &offset);
        std::array<float, 56> geometry{};
        geometry[0] = geometry[5] = geometry[10] = scale; geometry[15] = 1;
        geometry[3] = objectX - cameraX; geometry[7] = objectY - cameraY; geometry[11] = objectZ - cameraZ;
        std::copy_n(geometry.begin(), 16, geometry.begin()+16);
        // WVP is overwritten by the domain stage, but keep valid control points.
        std::copy_n(geometry.begin(), 16, geometry.begin()+32);
        for (unsigned col = 0; col < 4; ++col) {
            geometry[32+col] /= 512; geometry[36+col] /= 512; geometry[40+col] *= -side / 2000;
        }
        auto geometryCB = MakeBuffer(device, geometry.data(), sizeof(geometry));
        auto* rawGeometry = geometryCB.Get();
        context->VSSetConstantBuffers(2, 1, &rawGeometry);
        std::array<float, 180> frame{};
        frame[32] = frame[37] = 1.0f / 512.0f; frame[42] = -side / 2000.0f; frame[47] = 1;
        frame[160] = cameraX; frame[161] = cameraY; frame[162] = cameraZ;
        auto frameCB = MakeBuffer(device, frame.data(), sizeof(frame));
        auto* rawFrame = frameCB.Get();
        context->HSSetConstantBuffers(12, 1, &rawFrame);
        context->DSSetConstantBuffers(12, 1, &rawFrame);
        const float clear[4]{};
        context->ClearRenderTargetView(rtv.Get(), clear);
        const D3D11_QUERY_DESC queryDesc{D3D11_QUERY_PIPELINE_STATISTICS, 0};
        ComPtr<ID3D11Query> stats;
        Check(device->CreateQuery(&queryDesc, &stats));
        context->Begin(stats.Get());
        context->DrawIndexed(6, 0, 0);
        context->End(stats.Get());
        context->CopyResource(readback.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        unsigned covered = 0, invalid = 0;
        for (unsigned y = 0; y < 64; ++y) {
            const auto* row = reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData) + y*mapped.RowPitch);
            for (unsigned x = 0; x < 64; ++x) if (row[x*4+3] > 0.5f) {
                ++covered;
                if (!std::isfinite(row[x*4+2]) || std::abs(row[x*4+2] + cameraZ - sea) > 200.0f) ++invalid;
            }
        }
        context->Unmap(readback.Get(), 0);
        D3D11_QUERY_DATA_PIPELINE_STATISTICS result{};
        if (context->GetData(stats.Get(), &result, sizeof(result), 0) != S_OK || result.DSInvocations <= 6)
            throw std::runtime_error("Actual ocean domain shader did not subdivide the surface");
        if (covered < 500 || invalid) {
            std::cerr << "Ocean geometry diagnostics: cameraX=" << cameraX << " side=" << side
                      << " covered=" << covered << " invalid=" << invalid
                      << " vs=" << result.VSInvocations << " hs=" << result.HSInvocations
                      << " ds=" << result.DSInvocations << " clipIn=" << result.CInvocations
                      << " clipOut=" << result.CPrimitives << " ps=" << result.PSInvocations << '\n';
            throw std::runtime_error("Ocean surface missing or misplaced in above/below camera regression");
        }
        std::cout << "WARP ocean geometry: cameraX=" << cameraX << " side=" << side
            << " covered=" << covered << " domainInvocations=" << result.DSInvocations << '\n';
    }
    context->ClearState();
}
