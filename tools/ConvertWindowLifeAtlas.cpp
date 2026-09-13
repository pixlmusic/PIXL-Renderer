// Offline format conversion only. Uses the renderer's existing DirectXTex dependency.
#include <Windows.h>
#include <DirectXTex.h>
#include <cstdio>
int wmain(int argc, wchar_t** argv)
{
    if (argc != 4) return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 3;
    DirectX::ScratchImage source, resized, mips, compressed;
    HRESULT hr = DirectX::LoadFromWICFile(argv[1], DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, source);
    if (SUCCEEDED(hr)) hr = DirectX::Resize(*source.GetImage(0, 0, 0), 2048, 2048, DirectX::TEX_FILTER_FANT, resized);
    if (SUCCEEDED(hr)) hr = DirectX::SaveToWICFile(*resized.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), argv[3]);
    // Stop before mips collapse adjacent atlas cells; shader clamps to mip 7.
    if (SUCCEEDED(hr)) hr = DirectX::GenerateMipMaps(*resized.GetImage(0, 0, 0), DirectX::TEX_FILTER_BOX, 8, mips);
    if (SUCCEEDED(hr)) hr = DirectX::Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), DXGI_FORMAT_BC1_UNORM_SRGB, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, compressed);
    if (SUCCEEDED(hr)) hr = DirectX::SaveToDDSFile(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(), DirectX::DDS_FLAGS_NONE, argv[2]);
    std::printf("Atlas conversion HRESULT: %08lX\n", static_cast<unsigned long>(hr));
    CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}
