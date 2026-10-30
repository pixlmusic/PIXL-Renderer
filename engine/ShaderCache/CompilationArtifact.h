#pragma once

// PIXL compilation artifacts. Owned by ShaderCache; no scheduler or device owner.
// Content-closure keys follow the technique documented in the Open Shaders audit.
// This implementation is original; includes retain the caller's resolver semantics.
#include <Windows.h>
#include <bcrypt.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace SIE
{
class CompilationArtifacts
{
    using Blob = Microsoft::WRL::ComPtr<ID3DBlob>;
    using Digest = std::array<unsigned char, 32>;
    static constexpr size_t MaxBytes = 64 * 1024 * 1024;
    // Compiler work is serialized within a bucket to coalesce identical
    // requests. Use 12 digest bits rather than 5: the old 32-bucket table made
    // unrelated permutations wait behind the same long-running FXC call.
    static constexpr size_t BucketCount = 1u << 12;

    class Hash
    {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
    public:
        Hash()
        {
            if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
                throw std::runtime_error("SHA256 provider unavailable");
            if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
                BCryptCloseAlgorithmProvider(algorithm, 0);
                throw std::runtime_error("SHA256 hash unavailable");
            }
        }
        ~Hash() { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); }
        Hash(const Hash&) = delete;
        Hash& operator=(const Hash&) = delete;
        void Add(const void* bytes, size_t count)
        {
            if (count > UINT32_MAX || BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<void*>(bytes)), static_cast<ULONG>(count), 0) < 0)
                throw std::runtime_error("SHA256 input failed");
        }
        void Field(std::string_view text)
        {
            const uint64_t size = text.size();
            Add(&size, sizeof(size));
            if (size) Add(text.data(), text.size());
        }
        Digest Finish()
        {
            Digest result{};
            if (BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0) < 0)
                throw std::runtime_error("SHA256 finish failed");
            return result;
        }
    };

    static Digest HashBytes(const void* bytes, size_t count) { Hash hash; hash.Add(bytes, count); return hash.Finish(); }
    static std::string Hex(const Digest& digest)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        for (auto b : digest) { result += digits[b >> 4]; result += digits[b & 15]; }
        return result;
    }
    static bool Read(const std::filesystem::path& path, std::vector<char>& bytes)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return false;
        const auto size = file.tellg();
        if (size < 0 || size > static_cast<std::streamoff>(MaxBytes)) return false;
        bytes.resize(static_cast<size_t>(size));
        file.seekg(0);
        return bytes.empty() || static_cast<bool>(file.read(bytes.data(), size));
    }
    static Digest CompilerIdentity()
    {
        // Hash the loaded compiler implementation, not a hard-coded SDK version.
        static const Digest identity = [] {
            HMODULE module{};
            wchar_t path[32768]{};
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&D3DCompile), &module))
                throw std::runtime_error("Cannot identify shader compiler");
            const DWORD count = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
            std::vector<char> bytes;
            if (!count || count >= std::size(path) || !Read(path, bytes))
                throw std::runtime_error("Cannot read shader compiler identity");
            return HashBytes(bytes.data(), bytes.size());
        }();
        return identity;
    }
    struct RecordingInclude : ID3DInclude
    {
        ID3DInclude& inner;
        Hash& hash;
        RecordingInclude(ID3DInclude& resolver, Hash& digest) : inner(resolver), hash(digest) {}
        HRESULT Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* size) override
        {
            const auto hr = inner.Open(type, name, parent, data, size);
            try {
                hash.Field(name ? name : "");
                hash.Add(&hr, sizeof(hr));
                if (SUCCEEDED(hr) && *size) hash.Field(std::string_view(static_cast<const char*>(*data), *size));
            } catch (...) {
                if (SUCCEEDED(hr)) inner.Close(*data);
                return E_FAIL;
            }
            return hr;
        }
        HRESULT Close(LPCVOID data) override { return inner.Close(data); }
    };
    struct Metadata
    {
        uint32_t magic = 0x43415850; // PXAC
        uint32_t version = 1;
        uint64_t size = 0;
        Digest key{};
        Digest payload{};
    };
    static bool ReadArtifact(const std::filesystem::path& path, const Digest& key, Blob& result)
    {
        std::vector<char> metadata, payload;
        if (!Read(path.wstring() + L".identity", metadata) || metadata.size() != sizeof(Metadata)) return false;
        Metadata header{};
        memcpy(&header, metadata.data(), sizeof(header));
        if (header.magic != 0x43415850 || header.version != 1 || header.key != key || header.size < 32 || header.size > MaxBytes) return false;
        if (!Read(path, payload) || payload.size() != header.size || memcmp(payload.data(), "DXBC", 4) || HashBytes(payload.data(), payload.size()) != header.payload) return false;
        if (FAILED(D3DCreateBlob(payload.size(), result.ReleaseAndGetAddressOf()))) return false;
        memcpy(result->GetBufferPointer(), payload.data(), payload.size());
        return true;
    }
    static bool AtomicWrite(const std::filesystem::path& path, const void* bytes, size_t size)
    {
        static std::atomic<uint64_t> serial{};
        const auto temp = path.wstring() + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++serial);
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file) return false;
            file.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(size));
            file.flush();
            if (!file) { file.close(); DeleteFileW(temp.c_str()); return false; }
        }
        if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        DeleteFileW(temp.c_str());
        return false;
    }
    struct Bucket
    {
        std::mutex mutex;
        uint64_t generation = 0;
        std::string key;
        Blob blob;
        std::unordered_set<std::string> failures;
    };
    struct RequestState
    {
        std::mutex mutex;
        std::condition_variable ready;
        std::string key;
        uint64_t generation{};
        bool completed{};
        HRESULT result{ E_FAIL };
        Blob shader;
        Blob diagnostics;
        bool diskHit{};
    };
    struct RequestBucket
    {
        std::mutex mutex;
        std::shared_ptr<RequestState> active;
    };
    std::unique_ptr<Bucket[]> buckets{ std::make_unique<Bucket[]>(BucketCount) };
    std::unique_ptr<RequestBucket[]> requestBuckets{ std::make_unique<RequestBucket[]>(BucketCount) };
    std::atomic<uint64_t> generation{ 1 };
    std::atomic<uint64_t> compileCount{};
    std::atomic<uint64_t> requests{};
    std::atomic<uint64_t> coalescedRequests{};
    std::atomic<uint64_t> preprocessingCalls{};
    std::atomic<uint64_t> preprocessingNanoseconds{};
    std::atomic<uint64_t> compilerNanoseconds{};
    std::atomic<uint64_t> preprocessedBytes{};
    std::atomic<uint64_t> inMemoryHits{};
    std::atomic<uint64_t> diskHits{};
    std::atomic<uint64_t> actualCompiles{};
    std::atomic<uint64_t> failures{};
    std::shared_mutex publication;

public:
    struct TimingSnapshot
    {
        uint64_t requests{};
        uint64_t coalescedRequests{};
        uint64_t preprocessingCalls{};
        uint64_t preprocessingNanoseconds{};
        uint64_t compilerNanoseconds{};
        uint64_t preprocessedBytes{};
        uint64_t inMemoryHits{};
        uint64_t diskHits{};
        uint64_t actualCompiles{};
        uint64_t failures{};
    };

    std::shared_lock<std::shared_mutex> LockPublication() { return std::shared_lock(publication); }
    uint64_t Generation() const { return generation.load(std::memory_order_acquire); }
    uint64_t CompileCount() const { return compileCount.load(std::memory_order_relaxed); }
    TimingSnapshot GetTimingSnapshot() const
    {
        return { requests.load(std::memory_order_relaxed), coalescedRequests.load(std::memory_order_relaxed),
            preprocessingCalls.load(std::memory_order_relaxed), preprocessingNanoseconds.load(std::memory_order_relaxed),
            compilerNanoseconds.load(std::memory_order_relaxed), preprocessedBytes.load(std::memory_order_relaxed),
            inMemoryHits.load(std::memory_order_relaxed), diskHits.load(std::memory_order_relaxed),
            actualCompiles.load(std::memory_order_relaxed), failures.load(std::memory_order_relaxed) };
    }
    void Invalidate() { std::unique_lock lock(publication); generation.fetch_add(1, std::memory_order_acq_rel); }

    // Raw stage-shard DXBC paths stay intact. A versioned identity sidecar makes
    // legacy/mixed/truncated pairs miss safely. Standalones use a content key path.
    HRESULT Compile(const std::filesystem::path& source, const D3D_SHADER_MACRO* macros, ID3DInclude& include,
        const char* entry, const char* profile, UINT flags, const std::filesystem::path& stagePath,
        bool diskEnabled, ID3DBlob** output, ID3DBlob** errors, bool* diskHit = nullptr)
    {
        if (!output || !errors || !entry || !profile) return E_INVALIDARG;
        requests.fetch_add(1, std::memory_order_relaxed);
        *output = nullptr;
        *errors = nullptr;
        if (diskHit) *diskHit = false;
        const auto epoch = Generation();

        try {
            // Build a short-lived request identity before include expansion. It
            // deliberately covers root source bytes, macros, compiler settings,
            // stage destination, and generation. Matching callers share the
            // leader's in-flight preprocess/compile result; completed entries
            // are removed immediately, so later file edits always re-enter the
            // content-closure cache below.
            std::vector<char> sourceBytes;
            const bool sourceReadable = Read(source, sourceBytes);
            Hash requestHash;
            requestHash.Field("PIXL.CompilationRequest.1");
            requestHash.Field(source.lexically_normal().generic_string());
            requestHash.Add(&sourceReadable, sizeof(sourceReadable));
            requestHash.Field(std::string_view(sourceBytes.data(), sourceBytes.size()));
            requestHash.Field(entry);
            requestHash.Field(profile);
            requestHash.Add(&flags, sizeof(flags));
            const auto compiler = CompilerIdentity();
            requestHash.Add(compiler.data(), compiler.size());
            requestHash.Field(stagePath.lexically_normal().generic_string());
            requestHash.Add(&diskEnabled, sizeof(diskEnabled));
            requestHash.Add(&epoch, sizeof(epoch));
            if (macros) for (auto macro = macros; macro->Name; ++macro) {
                const bool hasDefinition = macro->Definition != nullptr;
                requestHash.Field(macro->Name);
                requestHash.Add(&hasDefinition, sizeof(hasDefinition));
                requestHash.Field(macro->Definition ? macro->Definition : "");
            }
            const auto requestDigest = requestHash.Finish();
            const auto requestKey = Hex(requestDigest);
            const size_t requestBucketIndex =
                (static_cast<size_t>(requestDigest[0]) | (static_cast<size_t>(requestDigest[1]) << 8) |
                    (static_cast<size_t>(requestDigest[2]) << 16)) & (BucketCount - 1);
            auto& requestBucket = requestBuckets[requestBucketIndex];
            std::shared_ptr<RequestState> request;
            bool leader = false;
            {
                std::lock_guard lock(requestBucket.mutex);
                if (requestBucket.active && requestBucket.active->generation == epoch &&
                    requestBucket.active->key == requestKey) {
                    request = requestBucket.active;
                } else {
                    request = std::make_shared<RequestState>();
                    request->key = requestKey;
                    request->generation = epoch;
                    requestBucket.active = request;
                    leader = true;
                }
            }

            if (!leader) {
                coalescedRequests.fetch_add(1, std::memory_order_relaxed);
                std::unique_lock lock(request->mutex);
                request->ready.wait(lock, [&] {
                    return request->completed || epoch != Generation();
                });
                if (epoch != Generation()) return E_ABORT;
                const auto result = request->result;
                if (request->shader) request->shader.CopyTo(output);
                if (request->diagnostics) request->diagnostics.CopyTo(errors);
                if (diskHit) *diskHit = request->diskHit;
                return result;
            }

            const HRESULT result = CompileUncoalesced(source, sourceBytes, sourceReadable, macros, include,
                entry, profile, flags, stagePath, diskEnabled, epoch, output, errors, diskHit);
            {
                std::lock_guard lock(request->mutex);
                request->result = result;
                if (*output) request->shader = *output;
                if (*errors) request->diagnostics = *errors;
                request->diskHit = diskHit && *diskHit;
                request->completed = true;
            }
            {
                std::lock_guard lock(requestBucket.mutex);
                if (requestBucket.active == request) requestBucket.active.reset();
            }
            request->ready.notify_all();
            return result;
        } catch (...) {
            failures.fetch_add(1, std::memory_order_relaxed);
            return E_FAIL;
        }
    }

private:
    HRESULT CompileUncoalesced(const std::filesystem::path& source, const std::vector<char>& sourceBytes,
        bool sourceReadable, const D3D_SHADER_MACRO* macros, ID3DInclude& include,
        const char* entry, const char* profile, UINT flags, const std::filesystem::path& stagePath,
        bool diskEnabled, uint64_t epoch, ID3DBlob** output, ID3DBlob** errors, bool* diskHit)
    {
        if (!output || !errors || !entry || !profile) return E_INVALIDARG;
        *output = nullptr;
        *errors = nullptr;
        if (diskHit) *diskHit = false;
        try {
            Hash hash;
            hash.Field("PIXL.CompilationArtifact.1");
            hash.Field(source.lexically_normal().generic_string());
            hash.Add(&sourceReadable, sizeof(sourceReadable));
            hash.Field(entry); hash.Field(profile); hash.Add(&flags, sizeof(flags));
            const auto compiler = CompilerIdentity(); hash.Add(compiler.data(), compiler.size());
            if (macros) for (auto macro = macros; macro->Name; ++macro) {
                const bool hasDefinition = macro->Definition != nullptr;
                hash.Field(macro->Name); hash.Add(&hasDefinition, sizeof(hasDefinition));
                hash.Field(macro->Definition ? macro->Definition : "");
            }
            hash.Field(std::string_view(sourceBytes.data(), sourceBytes.size()));
            RecordingInclude recorder(include, hash);
            Blob preprocessed, diagnostics;
            const auto preprocessStart = std::chrono::steady_clock::now();
            preprocessingCalls.fetch_add(1, std::memory_order_relaxed);
            HRESULT hr = sourceReadable ? D3DPreprocess(sourceBytes.data(), sourceBytes.size(), source.string().c_str(), macros, &recorder,
                preprocessed.GetAddressOf(), diagnostics.GetAddressOf()) : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
            preprocessingNanoseconds.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - preprocessStart).count()), std::memory_order_relaxed);
            if (SUCCEEDED(hr) && preprocessed)
                hash.Field(std::string_view(static_cast<const char*>(preprocessed->GetBufferPointer()), preprocessed->GetBufferSize()));
            if (SUCCEEDED(hr) && preprocessed)
                preprocessedBytes.fetch_add(preprocessed->GetBufferSize(), std::memory_order_relaxed);
            const auto digest = hash.Finish();
            const auto key = Hex(digest);
            const size_t bucketIndex =
                (static_cast<size_t>(digest[0]) | (static_cast<size_t>(digest[1]) << 8) |
                    (static_cast<size_t>(digest[2]) << 16)) & (BucketCount - 1);
            auto& bucket = buckets[bucketIndex];
            std::unique_lock workLock(bucket.mutex);
            if (epoch != Generation()) return E_ABORT;
            if (bucket.generation != epoch) { bucket.key.clear(); bucket.blob.Reset(); bucket.failures.clear(); bucket.generation = epoch; }
            if (bucket.failures.contains(key)) return E_PENDING;
            if (FAILED(hr)) {
                failures.fetch_add(1, std::memory_order_relaxed);
                if (bucket.failures.size() >= 64) bucket.failures.clear();
                bucket.failures.insert(key);
                *errors = diagnostics.Detach();
                return hr;
            }
            if (bucket.key == key && bucket.blob) {
                inMemoryHits.fetch_add(1, std::memory_order_relaxed);
                return bucket.blob.CopyTo(output);
            }
            const auto path = stagePath.empty() ? std::filesystem::path(L"Data/PIXL/PipelineLibrary/Standalone/v1") / (key + ".pixlbin") : stagePath;
            Blob blob;
            bool hit = diskEnabled && ReadArtifact(path, digest, blob);
            if (!hit) {
                diagnostics.Reset();
                compileCount.fetch_add(1, std::memory_order_relaxed);
                actualCompiles.fetch_add(1, std::memory_order_relaxed);
                // Compile exactly the source snapshot that produced the digest.
                // Reopening files here would allow edits during compilation to poison the cache.
                const auto compilerStart = std::chrono::steady_clock::now();
                hr = D3DCompile(preprocessed->GetBufferPointer(), preprocessed->GetBufferSize(), source.string().c_str(),
                    nullptr, nullptr, entry, profile, flags, 0, blob.GetAddressOf(), diagnostics.GetAddressOf());
                compilerNanoseconds.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - compilerStart).count()), std::memory_order_relaxed);
            } else {
                diskHits.fetch_add(1, std::memory_order_relaxed);
            }
            std::shared_lock publishLock(publication);
            if (epoch != Generation()) return E_ABORT;
            if (FAILED(hr) || !blob) {
                failures.fetch_add(1, std::memory_order_relaxed);
                if (bucket.failures.size() >= 64) bucket.failures.clear();
                bucket.failures.insert(key);
                *errors = diagnostics.Detach();
                return FAILED(hr) ? hr : E_FAIL;
            }
            if (!hit && diskEnabled) {
                std::error_code ec;
                std::filesystem::create_directories(path.parent_path(), ec);
                if (!ec) {
                    Metadata metadata{};
                    metadata.key = digest; metadata.size = blob->GetBufferSize();
                    metadata.payload = HashBytes(blob->GetBufferPointer(), blob->GetBufferSize());
                    if (AtomicWrite(path, blob->GetBufferPointer(), blob->GetBufferSize()))
                        AtomicWrite(path.wstring() + L".identity", &metadata, sizeof(metadata));
                }
            }
            // At most 32 MiB retained; oversized bytecode still has disk reuse.
            bucket.key = key;
            bucket.blob = blob->GetBufferSize() <= 1024 * 1024 ? blob : Blob{};
            if (diskHit) *diskHit = hit;
            *errors = diagnostics.Detach();
            *output = blob.Detach();
            return S_OK;
        } catch (...) { return E_FAIL; }
    }
};
}
