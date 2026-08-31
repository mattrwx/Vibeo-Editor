#include "texture.h"
#include <d3d11.h>

static ID3D11Device* g_dev = nullptr;

void TextureSetDevice(ID3D11Device* dev) { g_dev = dev; }

Texture::~Texture() {
    if (srv) srv->Release();
}

TexPtr CreateTextureRGBA(const uint8_t* rgba, int w, int h) {
    if (!g_dev || !rgba || w <= 0 || h <= 0) return nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba;
    init.SysMemPitch = (UINT)(w * 4);

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&desc, &init, &tex))) return nullptr;

    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = g_dev->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    if (FAILED(hr)) return nullptr;

    auto t = std::make_shared<Texture>();
    t->srv = srv;
    t->w = w;
    t->h = h;
    return t;
}
