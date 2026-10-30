#define NOMINMAX
#include "ShaderCache/CompilationArtifact.h"
#include <functional>
#include <future>
#include <iostream>
#include <iomanip>
#include <unordered_map>

namespace fs = std::filesystem;
using Blob = Microsoft::WRL::ComPtr<ID3DBlob>;
static void Require(bool test, const char* message) { if (!test) throw std::runtime_error(message); }
static void Write(const fs::path& path, std::string_view text) { std::ofstream f(path, std::ios::binary); f << text; }
struct Include : ID3DInclude
{
    fs::path root;
    std::function<void()> onOpen;
    std::unordered_map<const void*, fs::path> opened;
    explicit Include(fs::path path) : root(std::move(path)) {}
    HRESULT Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
    {
        if (onOpen) onOpen();
        fs::path path = root / name;
        if (parent) {
            const auto parentPath = opened.find(parent);
            if (parentPath != opened.end()) {
                const auto relativePath = parentPath->second.parent_path() / name;
                if (fs::exists(relativePath)) path = relativePath;
            }
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) return E_FAIL;
        std::string bytes((std::istreambuf_iterator<char>(file)), {});
        auto memory = new char[bytes.size() + 1];
        memcpy(memory, bytes.data(), bytes.size());
        *data = memory; *size = static_cast<UINT>(bytes.size());
        opened.emplace(memory, std::move(path));
        return S_OK;
    }
    HRESULT Close(LPCVOID data) override { opened.erase(data); delete[] static_cast<const char*>(data); return S_OK; }
};

struct BenchmarkCase
{
    const char* name;
    const char* file;
    const char* profile;
    const D3D_SHADER_MACRO* defines;
};

static int RunBenchmark(const fs::path& shaderRoot, const fs::path& outputRoot)
{
    static const D3D_SHADER_MACRO lighting[]{
        {"WINPC", nullptr}, {"DX11", nullptr}, {"PSHADER", nullptr}, {"SPECULAR", nullptr},
        {"MATERIAL_FORGE", nullptr}, {"RAIN_RESPONSE", nullptr}, {"WORLD_PROBES", nullptr},
        {"SKY_BOUNCE", nullptr}, {"GROUND_RESPONSE", nullptr}, {"DEFERRED", nullptr}, {nullptr, nullptr}};
    static const D3D_SHADER_MACRO terrainSnow[]{
        {"WINPC", nullptr}, {"DX11", nullptr}, {"PSHADER", nullptr}, {"LANDSCAPE", nullptr},
        {"MATERIAL_FORGE", nullptr}, {"SNOW", nullptr}, {"RAIN_RESPONSE", nullptr},
        {"WORLD_PROBES", nullptr}, {"SKY_BOUNCE", nullptr}, {"GROUND_RESPONSE", nullptr},
        {"DEFERRED", nullptr}, {nullptr, nullptr}};
    static const D3D_SHADER_MACRO effect[]{
        {"WINPC", nullptr}, {"DX11", nullptr}, {"PSHADER", nullptr}, {"SOFT", nullptr},
        {"NORMALS", nullptr}, {"TEXTURE", nullptr}, {"FALLOFF", nullptr}, {"VC", nullptr}, {nullptr, nullptr}};
    static const D3D_SHADER_MACRO water[]{
        {"WINPC", nullptr}, {"DX11", nullptr}, {"PSHADER", nullptr}, {"WATER", nullptr}, {"FOG", nullptr},
        {"RAIN_RESPONSE", nullptr}, {"WATER_OPTICS", nullptr}, {"SPECULAR", nullptr},
        {"REFLECTIONS", nullptr}, {"REFRACTIONS", nullptr}, {nullptr, nullptr}};
    const BenchmarkCase cases[]{
        {"Lighting.Deferred", "Lighting.hlsl", "ps_5_0", lighting},
        {"Lighting.TerrainSnow", "Lighting.hlsl", "ps_5_0", terrainSnow},
        {"Effect.Volumetric", "Effect.hlsl", "ps_5_0", effect},
        {"Water.Rain", "Water.hlsl", "ps_5_0", water}};

    fs::create_directories(outputRoot);
    struct Result { std::string name; double wallMs{}; double preprocessMs{}; double compilerMs{}; uint64_t sourceBytes{}; uint64_t artifactBytes{}; bool diskHit{}; };
    std::vector<Result> coldResults, warmResults;
    const auto flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

    auto runPass = [&](bool warm, std::vector<Result>& results) -> bool {
        SIE::CompilationArtifacts artifacts;
        for (const auto& test : cases) {
            Result result; result.name = test.name;
            Blob code, errors;
            bool diskHit = false;
            Include include(shaderRoot);
            const auto source = shaderRoot / test.file;
            const auto cachePath = outputRoot / (std::string(test.name) + ".pixlbin");
            const auto before = artifacts.GetTimingSnapshot();
            const auto begin = std::chrono::steady_clock::now();
            const HRESULT hr = artifacts.Compile(source, test.defines, include, "main", test.profile,
                flags, cachePath, true, code.GetAddressOf(), errors.GetAddressOf(), &diskHit);
            result.wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            const auto after = artifacts.GetTimingSnapshot();
            result.preprocessMs = static_cast<double>(after.preprocessingNanoseconds - before.preprocessingNanoseconds) / 1.0e6;
            result.compilerMs = static_cast<double>(after.compilerNanoseconds - before.compilerNanoseconds) / 1.0e6;
            result.sourceBytes = after.preprocessedBytes - before.preprocessedBytes;
            result.artifactBytes = code ? code->GetBufferSize() : 0;
            result.diskHit = diskHit;
            if (FAILED(hr) || !code || (warm && !diskHit)) {
                std::cerr << (warm ? "warm" : "cold") << " benchmark failed for " << test.name << " (HRESULT 0x"
                    << std::hex << static_cast<uint32_t>(hr) << std::dec << ")\n";
                if (errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
                return false;
            }
            results.push_back(std::move(result));
        }
        return true;
    };

    if (!runPass(false, coldResults) || !runPass(true, warmResults)) return 1;
    const auto csvPath = outputRoot / "results.csv";
    std::ofstream csv(csvPath, std::ios::binary | std::ios::trunc);
    if (!csv) throw std::runtime_error("Cannot write benchmark results");
    csv << "pass,name,wall_ms,preprocess_ms,fxc_ms,preprocessed_bytes,artifact_bytes,disk_hit\n" << std::fixed << std::setprecision(3);
    auto write = [&](const char* label, const std::vector<Result>& results) {
        for (const auto& r : results)
            csv << label << ',' << r.name << ',' << r.wallMs << ',' << r.preprocessMs << ',' << r.compilerMs << ','
                << r.sourceBytes << ',' << r.artifactBytes << ',' << (r.diskHit ? 1 : 0) << '\n';
    };
    write("cold", coldResults);
    write("warm", warmResults);
    double coldTotal = 0, warmTotal = 0, coldCompile = 0, warmCompile = 0;
    for (const auto& r : coldResults) { coldTotal += r.wallMs; coldCompile += r.compilerMs; }
    for (const auto& r : warmResults) { warmTotal += r.wallMs; warmCompile += r.compilerMs; }
    std::cout << "Shader compiler benchmark: " << std::size(cases) << " real staged requests; cold " << coldTotal
        << " ms (FXC " << coldCompile << " ms), warm " << warmTotal << " ms (FXC " << warmCompile
        << " ms); results: " << csvPath.string() << '\n';
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 4 && std::string_view(argv[1]) == "--benchmark")
        return RunBenchmark(fs::path(argv[2]), fs::path(argv[3]));
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
        Write(source, "#include \"include.hlsli\"\nfloat4 main():SV_Target{return COLOR;}\n");
        SIE::CompilationArtifacts concurrent;
        std::vector<std::future<HRESULT>> jobs;
        std::atomic_bool allowPreprocess{};
        for (int i = 0; i < 12; ++i) jobs.emplace_back(std::async(std::launch::async, [&] {
            Include include(root);
            include.onOpen = [&] {
                while (!allowPreprocess.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            };
            Blob code, errors;
            return concurrent.Compile(source, macros, include, "main", "ps_5_0", 0, {}, false, code.GetAddressOf(), errors.GetAddressOf());
        }));
        const auto coalesceDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (concurrent.GetTimingSnapshot().requests < 12 &&
               std::chrono::steady_clock::now() < coalesceDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        allowPreprocess.store(true, std::memory_order_release);
        Require(concurrent.GetTimingSnapshot().requests == 12, "all duplicate requests reached in-flight window");
        for (auto& job : jobs) Require(SUCCEEDED(job.get()), "concurrent completion");
        Require(concurrent.CompileCount() == 1, "duplicate compiles coalesced with disk disabled");
        const auto concurrentStats = concurrent.GetTimingSnapshot();
        Require(concurrentStats.requests == 12 && concurrentStats.coalescedRequests == 11 &&
                    concurrentStats.preprocessingCalls == 1,
            "identical in-flight requests coalesce before preprocessing");
        Include canceled(root); canceled.onOpen = [&] { concurrent.Invalidate(); };
        Blob errors;
        Require(concurrent.Compile(source, macros, canceled, "main", "ps_5_0", 0, {}, false, next.ReleaseAndGetAddressOf(), errors.GetAddressOf()) == E_ABORT && !next,
            "generation invalidates in-flight snapshot");
        Include retry(root); errors.Reset();
        Require(SUCCEEDED(concurrent.Compile(source, macros, retry, "main", "ps_5_0", 0, {}, false,
                    next.ReleaseAndGetAddressOf(), errors.GetAddressOf())) && next,
            "superseded request can be retried in the new generation");
        fs::remove_all(root);
        std::cout << "Compilation artifacts: content, corruption, macros/profile/entry/flags, failure recovery, concurrency and generation cases passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
