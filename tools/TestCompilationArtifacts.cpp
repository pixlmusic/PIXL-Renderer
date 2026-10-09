#define NOMINMAX
#include "ShaderCache/CompilationArtifact.h"
#include <functional>
#include <future>
#include <iostream>

namespace fs = std::filesystem;
using Blob = Microsoft::WRL::ComPtr<ID3DBlob>;
static void Require(bool test, const char* message) { if (!test) throw std::runtime_error(message); }
static void Write(const fs::path& path, std::string_view text) { std::ofstream f(path, std::ios::binary); f << text; }
struct Include : ID3DInclude
{
    fs::path root;
    std::function<void()> onOpen;
    explicit Include(fs::path path) : root(std::move(path)) {}
    HRESULT Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* size) override
    {
        if (onOpen) onOpen();
        std::ifstream file(root / name, std::ios::binary);
        if (!file) return E_FAIL;
        std::string bytes((std::istreambuf_iterator<char>(file)), {});
        auto memory = new char[bytes.size() + 1];
        memcpy(memory, bytes.data(), bytes.size());
        *data = memory; *size = static_cast<UINT>(bytes.size());
        return S_OK;
    }
    HRESULT Close(LPCVOID data) override { delete[] static_cast<const char*>(data); return S_OK; }
};
int main()
{
    const auto root = fs::temp_directory_path() / ("PIXL-Artifact-Test-" + std::to_string(GetCurrentProcessId()));
    try {
        fs::create_directories(root);
        const auto source = root / "root.hlsl", artifact = root / "stage.pixlbin";
        Write(root / "nested.hlsli", "#define COLOR 0.25\n");
        Write(root / "include.hlsli", "#include \"nested.hlsli\"\n");
        Write(source, "#include \"include.hlsli\"\nfloat4 main():SV_Target{return COLOR + EXTRA;}\nfloat4 alternate():SV_Target{return COLOR;}\n");
        const D3D_SHADER_MACRO macros[]{ {"EXTRA", "0.1"}, {nullptr, nullptr} };
        SIE::CompilationArtifacts cache;
        auto compile = [&](Blob& result, bool& hit, const char* entry = "main", UINT flags = D3DCOMPILE_ENABLE_STRICTNESS, const D3D_SHADER_MACRO* defs = nullptr, const char* profile = "ps_5_0") {
            Include include(root); Blob errors;
            const auto hr = cache.Compile(source, defs ? defs : macros, include, entry, profile, flags, artifact, true, result.ReleaseAndGetAddressOf(), errors.GetAddressOf(), &hit);
            if (FAILED(hr) && errors) std::cerr << static_cast<char*>(errors->GetBufferPointer());
            return hr;
        };
        Blob first; bool hit{};
        fs::rename(source, root / "saved.hlsl");
        Require(FAILED(compile(first, hit)) && !first, "missing root rejected");
        Require(compile(first, hit) == E_PENDING, "missing root failure latched");
        fs::rename(root / "saved.hlsl", source);
        Require(SUCCEEDED(compile(first, hit)) && !hit, "first compile");
        cache.Invalidate(); Blob next;
        Require(SUCCEEDED(compile(next, hit)) && hit, "disk reuse");
        const auto stamp = fs::last_write_time(root / "nested.hlsli");
        Write(root / "nested.hlsli", "#define COLOR 0.75\n");
        fs::last_write_time(root / "nested.hlsli", stamp);
        Require(SUCCEEDED(compile(next, hit)) && !hit, "nested same-timestamp edit invalidation");
        Require(first->GetBufferSize() != next->GetBufferSize() || memcmp(first->GetBufferPointer(), next->GetBufferPointer(), first->GetBufferSize()), "changed compiled bytecode");
        cache.Invalidate(); Write(artifact, "truncated");
        Require(SUCCEEDED(compile(next, hit)) && !hit, "truncated artifact rejected");
        cache.Invalidate();
        { std::fstream f(artifact, std::ios::in | std::ios::out | std::ios::binary); f.seekp(20); f.put('X'); }
        Require(SUCCEEDED(compile(next, hit)) && !hit, "corrupt artifact rejected");
        cache.Invalidate(); Write(artifact.wstring() + L".identity", "truncated metadata");
        Require(SUCCEEDED(compile(next, hit)) && !hit, "truncated identity rejected");
        Require(SUCCEEDED(compile(next, hit, "alternate")) && !hit, "entry identity");
        Require(SUCCEEDED(compile(next, hit, "alternate", D3DCOMPILE_SKIP_OPTIMIZATION)) && !hit, "flags identity");
        const D3D_SHADER_MACRO changed[]{ {"EXTRA", "0.3"}, {nullptr, nullptr} };
        Require(SUCCEEDED(compile(next, hit, "main", D3DCOMPILE_ENABLE_STRICTNESS, changed)) && !hit, "macro identity");
        Require(SUCCEEDED(compile(next, hit, "main", D3DCOMPILE_ENABLE_STRICTNESS, changed, "ps_4_0")) && !hit, "profile identity");
        Write(source, "invalid shader");
        const auto beforeFailure = cache.CompileCount();
        Require(FAILED(compile(next, hit)), "compile failure");
        Require(compile(next, hit) == E_PENDING && cache.CompileCount() == beforeFailure + 1, "failure latched");
        Write(source, "float4 main():SV_Target{return 1;}\n");
        Require(SUCCEEDED(compile(next, hit)), "edited failure recovers");
        SIE::CompilationArtifacts concurrent;
        std::vector<std::future<HRESULT>> jobs;
        for (int i = 0; i < 12; ++i) jobs.emplace_back(std::async(std::launch::async, [&] {
            Include include(root); Blob code, errors;
            return concurrent.Compile(source, macros, include, "main", "ps_5_0", 0, {}, false, code.GetAddressOf(), errors.GetAddressOf());
        }));
        for (auto& job : jobs) Require(SUCCEEDED(job.get()), "concurrent completion");
        Require(concurrent.CompileCount() == 1, "duplicate compiles coalesced with disk disabled");
        Write(source, "#include \"include.hlsli\"\nfloat4 main():SV_Target{return COLOR;}\n");
        Include canceled(root); canceled.onOpen = [&] { concurrent.Invalidate(); };
        Blob errors;
        Require(concurrent.Compile(source, macros, canceled, "main", "ps_5_0", 0, {}, false, next.ReleaseAndGetAddressOf(), errors.GetAddressOf()) == E_ABORT && !next,
            "generation invalidates in-flight snapshot");
        fs::remove_all(root);
        std::cout << "Compilation artifacts: content, corruption, macros/profile/entry/flags, failure recovery, concurrency and generation cases passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
