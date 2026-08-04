#pragma once
#include <map>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

// Loads cover images (png/jpg/bmp via WIC) into D3D11 textures, cached by path.
// Must be created after the D3D11 device exists.
struct TextureCache {
    ID3D11Device* device = nullptr;
    std::map<std::string, ID3D11ShaderResourceView*> views;

    ID3D11ShaderResourceView* get(const std::string& path);
    void releaseAll();
    ~TextureCache();
};
