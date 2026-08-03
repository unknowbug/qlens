// HDR 检测实现（DXGI）
#include "hdr.h"
#include <dxgi.h>
#include <dxgi1_6.h>

bool HdrIsDisplayHDR()
{
    static bool checked = false;
    static bool isHdr = false;
    if (checked) return isHdr;
    checked = true;

    IDXGIFactory1 *factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory);
    if (FAILED(hr) || !factory) return false;

    IDXGIAdapter1 *adapter = nullptr;
    IDXGIOutput *output = nullptr;
    if (SUCCEEDED(factory->EnumAdapters1(0, &adapter)) && adapter) {
        adapter->EnumOutputs(0, &output);
        adapter->Release();
    }
    factory->Release();
    if (!output) return false;

    IDXGIOutput6 *out6 = nullptr;
    if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), (void **)&out6)) && out6) {
        DXGI_OUTPUT_DESC1 desc1 = {};
        if (SUCCEEDED(out6->GetDesc1(&desc1))) {
            // HDR10：色彩空间为 RGB_FULL_G2084_NONE_P2020
            if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
                isHdr = true;
            if (desc1.MaxLuminance > 100.0f) isHdr = true;
        }
        out6->Release();
    }
    output->Release();
    return isHdr;
}

float HdrDisplayPeakBrightness()
{
    static float cached = -1.0f;
    if (cached >= 0.0f) return cached;
    if (!HdrIsDisplayHDR()) { cached = 0.0f; return 0.0f; }
    IDXGIFactory1 *factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory);
    if (FAILED(hr) || !factory) return 0.0f;
    IDXGIAdapter1 *adapter = nullptr;
    IDXGIOutput *output = nullptr;
    float peak = 0.0f;
    if (SUCCEEDED(factory->EnumAdapters1(0, &adapter)) && adapter) {
        if (SUCCEEDED(adapter->EnumOutputs(0, &output)) && output) {
            IDXGIOutput6 *out6 = nullptr;
            if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), (void **)&out6)) && out6) {
                DXGI_OUTPUT_DESC1 d1 = {};
                if (SUCCEEDED(out6->GetDesc1(&d1))) peak = d1.MaxLuminance;
                out6->Release();
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    cached = peak;
    return peak;
}

bool HdrEnabled()
{
    return HdrIsDisplayHDR();
}
