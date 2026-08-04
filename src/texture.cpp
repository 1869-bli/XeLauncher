#include "texture.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <d3d11.h>

#include <vector>

using Microsoft::WRL::ComPtr;

TextureCache::~TextureCache() { releaseAll(); }

void TextureCache::releaseAll() {
    for (auto& kv : views)
        if (kv.second) kv.second->Release();
    views.clear();
}

static ComPtr<IWICBitmapSource> decodeImage(const std::string& path) {
    if (!path.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
        std::wstring wpath(n, 0);
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), wpath.data(), n);

        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory))))
            return nullptr;

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(wpath.c_str(), nullptr,
                                                      GENERIC_READ,
                                                      WICDecodeMetadataCacheOnLoad, &decoder)))
            return nullptr;

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) return nullptr;

        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter))) return nullptr;
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                         WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom)))
            return nullptr;
        return converter;
    }
    return nullptr;
}

ID3D11ShaderResourceView* TextureCache::get(const std::string& path) {
    if (!device || path.empty()) return nullptr;
    auto it = views.find(path);
    if (it != views.end()) return it->second;

    ComPtr<IWICBitmapSource> src = decodeImage(path);
    if (!src) return nullptr;

    UINT w = 0, h = 0;
    if (FAILED(src->GetSize(&w, &h)) || w == 0 || h == 0) return nullptr;

    std::vector<unsigned char> pixels((size_t)w * h * 4);
    if (FAILED(src->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data())))
        return nullptr;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA srd = {};
    srd.pSysMem = pixels.data();
    srd.SysMemPitch = w * 4;

    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device->CreateTexture2D(&td, &srd, &tex))) return nullptr;

    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, &srv))) return nullptr;
    views[path] = srv;
    return srv;
}
