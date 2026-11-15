#include "ShaderCache/CompilationArtifact.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

namespace {
struct Include final : ID3DInclude {
    std::filesystem::path root;
    std::vector<char> buffer;
    explicit Include(std::filesystem::path path) : root(std::move(path)) {}
    HRESULT Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* size) override {
        std::ifstream file(root / name, std::ios::binary | std::ios::ate);
        if (!file) return E_FAIL;
        const auto length = file.tellg();
        if (length < 0) return E_FAIL;
        buffer.resize(static_cast<size_t>(length));
        file.seekg(0);
        if (!buffer.empty() && !file.read(buffer.data(), length)) return E_FAIL;
        *data = buffer.data();
        *size = static_cast<UINT>(buffer.size());
        return S_OK;
    }
    HRESULT Close(LPCVOID) override { return S_OK; }
};
void Write(const std::filesystem::path& path, const char* text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("PIXL-fast-artifact-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    const auto source = root / "test.hlsl";
    const auto include = root / "color.hlsli";
    const auto stage = root / "test.pixlbin";
    Write(source, "#include \"color.hlsli\"\nfloat4 main() : SV_Target { return Color(); }\n");
    Write(include, "float4 Color() { return float4(1,0,0,1); }\n");
    const D3D_SHADER_MACRO macros[] = { { "PSHADER", "1" }, { nullptr, nullptr } };
    auto run = [&](SIE::CompilationArtifacts& artifacts) {
        Include handler(root);
        ID3DBlob* shader = nullptr;
        ID3DBlob* errors = nullptr;
        bool diskHit = false;
        const auto hr = artifacts.Compile(source, macros, handler, "main", "ps_5_0", 0,
            stage, true, &shader, &errors, &diskHit, true);
        if (errors) errors->Release();
        assert(SUCCEEDED(hr) && shader);
        std::vector<char> bytes(static_cast<char*>(shader->GetBufferPointer()),
            static_cast<char*>(shader->GetBufferPointer()) + shader->GetBufferSize());
        shader->Release();
        return std::pair{ std::move(bytes), diskHit };
    };
    SIE::CompilationArtifacts first;
    const auto [original, firstHit] = run(first);
    assert(!firstHit && first.GetTimingSnapshot().actualCompiles == 1);
    assert(std::filesystem::exists(stage.wstring() + L".fast"));
    SIE::CompilationArtifacts second;
    const auto [cached, secondHit] = run(second);
    assert(secondHit && cached == original && second.GetTimingSnapshot().preprocessingCalls == 0);
    const auto [memoized, memoHit] = run(second);
    assert(memoHit && memoized == original);
    assert(second.GetTimingSnapshot().includeReads == 1);
    assert(second.GetTimingSnapshot().includeMemoHits >= 1);
    Write(include, "float4 Color() { return float4(0,1,0,1); }\n");
    SIE::CompilationArtifacts changed;
    const auto [updated, thirdHit] = run(changed);
    assert(!thirdHit && updated != original && changed.GetTimingSnapshot().preprocessingCalls == 1);
    Write(source, "#include \"color.hlsli\"\nfloat4 main() : SV_Target { return Color() * 0.5; }\n");
    SIE::CompilationArtifacts rootChanged;
    const auto [rootUpdated, fourthHit] = run(rootChanged);
    assert(!fourthHit && rootUpdated != updated && rootChanged.GetTimingSnapshot().preprocessingCalls == 1);
    Write(include, "float4 Color() { return float4(0,0,1,1); }\n");
    second.Invalidate();
    const auto [invalidated, invalidatedHit] = run(second);
    assert(!invalidatedHit && invalidated != rootUpdated && second.GetTimingSnapshot().preprocessingCalls == 1);
    std::filesystem::remove_all(root);
}
