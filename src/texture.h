#pragma once
// DX11 texture helper. Textures must be created/destroyed on the main thread.
#include <memory>
#include <cstdint>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

struct Texture {
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;
    ~Texture();
    uint64_t ImId() const { return (uint64_t)(uintptr_t)srv; }
};
using TexPtr = std::shared_ptr<Texture>;

void TextureSetDevice(ID3D11Device* dev);
TexPtr CreateTextureRGBA(const uint8_t* rgba, int w, int h);
