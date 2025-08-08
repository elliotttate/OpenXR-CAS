// MIT License
//
// << insert your own copyright here >>
//
// Based on https://github.com/mbucchia/OpenXR-Layer-Template.
// Copyright(c) 2022-2023 Matthieu Bucchianeri
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.h"

#include "layer.h"
#include <log.h>
#include <util.h>
#include "utils/graphics.h"
#include <d3dcompiler.h>

// CAS CPU setup headers
#define A_CPU 1
#include "ffx_a.h"
#include "ffx_cas.h"

namespace openxr_api_layer {

    using namespace log;

    // Our API layer implement these extensions, and their specified version.
    const std::vector<std::pair<std::string, uint32_t>> advertisedExtensions = {};

    // Initialize these vectors with arrays of extensions to block and implicitly request for the instance.
    const std::vector<std::string> blockedExtensions = {};
    const std::vector<std::string> implicitExtensions = {};

    struct SessionState : utils::graphics::ICompositionSessionData {
        std::shared_ptr<utils::graphics::ICompositionFramework> composition;

        // App D3D11 device/context (direct, no framework dependency)
        Microsoft::WRL::ComPtr<ID3D11Device> appD3DDevice;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> appD3DContext;

        // D3D11 CAS objects on app device
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> cs;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constCB;
        float sharpness{0.6f};
        Microsoft::WRL::ComPtr<ID3D11Buffer> debugCB;

        // Timing queries
        Microsoft::WRL::ComPtr<ID3D11Query> qDisjoint;
        Microsoft::WRL::ComPtr<ID3D11Query> qBegin;
        Microsoft::WRL::ComPtr<ID3D11Query> qEnd;
        uint32_t timingFrameCounter{0};
        double timingAccumMs{0.0};

        // Config reload
        std::filesystem::file_time_type cfgLastWriteTime{};

        // Shader init state
        bool shaderInitAttempted{false};
        bool shaderInitFailed{false};

        // Debug controls
        uint32_t debugFramesMax{60};
        bool debugOverlay{false};

        // Levels controls
        bool levelsEnabled{false};
        float levelsInBlack{0.0f};
        float levelsInWhite{1.0f};
        float levelsOutBlack{0.0f};
        float levelsOutWhite{1.0f};
        float levelsGamma{1.0f};

        Microsoft::WRL::ComPtr<ID3D11ComputeShader> levelsCS;
        Microsoft::WRL::ComPtr<ID3D11Buffer> levelsCB;

        // FakeHDR controls
        bool fakeHdrEnabled{false};
        float fakeHdrPower{1.30f};
        float fakeHdrRadius1{0.793f};
        float fakeHdrRadius2{0.87f};
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> fakeHdrCS;
        Microsoft::WRL::ComPtr<ID3D11Buffer> fakeHdrCB;
    };

    static float readSharpnessFromEnv() {
        char buf[64]{};
        DWORD n = GetEnvironmentVariableA("XR_CAS_SHARPNESS", buf, sizeof(buf));
        if (n > 0 && n < sizeof(buf)) {
            try {
                float v = std::stof(buf);
                if (v < 0.f) v = 0.f;
                return v;
            } catch (...) {
            }
        }
        return 0.6f;
    }

    static std::optional<float> tryReadSharpnessFromConfigFile(const std::filesystem::path& path) {
        try {
            if (!std::filesystem::exists(path)) return std::nullopt;
            std::ifstream in(path);
            std::string line;
            while (std::getline(in, line)) {
                // Trim
                auto trim = [](std::string& s) {
                    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
                    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
                };
                trim(line);
                if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                auto pos = line.find('=');
                if (pos == std::string::npos) continue;
                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 1);
                trim(key);
                trim(val);
                // lowercase key
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (key == "sharpness") {
                    try {
                        float v = std::stof(val);
                        if (v < 0.f) v = 0.f;
                        return v;
                    } catch (...) {
                        return std::nullopt;
                    }
                }
            }
        } catch (...) {
        }
        return std::nullopt;
    }

    static std::optional<std::string> tryReadConfigValueFromFile(const std::filesystem::path& path,
                                                                 const std::string& keyLower) {
        try {
            if (!std::filesystem::exists(path)) return std::nullopt;
            std::ifstream in(path);
            std::string line;
            auto trim = [](std::string& s) {
                s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) { return !std::isspace(ch); }));
                s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) { return !std::isspace(ch); }).base(), s.end());
            };
            while (std::getline(in, line)) {
                trim(line);
                if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                auto pos = line.find('=');
                if (pos == std::string::npos) continue;
                std::string key = line.substr(0, pos);
                std::string val = line.substr(pos + 1);
                trim(key);
                trim(val);
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (key == keyLower) {
                    return val;
                }
            }
        } catch (...) {
        }
        return std::nullopt;
    }

    static std::optional<std::string> tryReadConfigValue(const std::string& keyLower) {
        if (auto v = tryReadConfigValueFromFile(openxr_api_layer::localAppData / "config.cfg", keyLower)) return v;
        if (auto v = tryReadConfigValueFromFile(openxr_api_layer::dllHome / "config.cfg", keyLower)) return v;
        return std::nullopt;
    }

    static float resolveSharpnessFromConfigOrEnv() {
        // Priority: env -> %LOCALAPPDATA% config -> DLL folder config -> default
        // Env already clamps and defaults if not set.
        char buf[64]{};
        if (GetEnvironmentVariableA("XR_CAS_SHARPNESS", buf, sizeof(buf)) > 0) {
            const float v = readSharpnessFromEnv();
            return v;
        }

        // Try local app data
        try {
            auto cfgPath = openxr_api_layer::localAppData / "config.cfg";
            if (auto val = tryReadSharpnessFromConfigFile(cfgPath)) {
                Log(fmt::format("Loaded sharpness from config: {}\\n", cfgPath.string()));
                return *val;
            }
        } catch (...) {
        }

        // Try DLL home
        try {
            auto cfgPath = openxr_api_layer::dllHome / "config.cfg";
            if (auto val = tryReadSharpnessFromConfigFile(cfgPath)) {
                Log(fmt::format("Loaded sharpness from config: {}\\n", cfgPath.string()));
                return *val;
            }
        } catch (...) {
        }

        return 0.6f;
    }

    static bool ensureCasObjects(SessionState* s) {
        if (!s || !s->appD3DDevice) return false;
        if (s->cs && (!s->levelsEnabled || s->levelsCS) && (!s->fakeHdrEnabled || s->fakeHdrCS)) return true;
        if (s->shaderInitAttempted && s->shaderInitFailed) return false;
        s->shaderInitAttempted = true;
        ID3D11Device* d3d = s->appD3DDevice.Get();

        // Try loading precompiled CAS.cso first
        bool shaderCreated = false;
        do {
            auto csoPath = (dllHome / "shaders" / "CAS.cso");
            if (std::filesystem::exists(csoPath)) {
                try {
                    std::ifstream fin(csoPath, std::ios::binary);
                    std::vector<char> bytes((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
                    if (!bytes.empty() && SUCCEEDED(d3d->CreateComputeShader(bytes.data(), bytes.size(), nullptr, s->cs.ReleaseAndGetAddressOf()))) {
                        Log(fmt::format("CAS shader loaded: {}\n", csoPath.string()));
                        shaderCreated = true;
                        break;
                    }
                } catch (...) {
                }
            }

            // Fallback: compile CAS compute shader from HLSL
            HMODULE d3dCompiler = LoadLibraryW(L"d3dcompiler_47.dll");
            if (!d3dCompiler) {
                // Try next to our DLL as a fallback
                std::wstring localDll = (dllHome / L"d3dcompiler_47.dll").wstring();
                d3dCompiler = LoadLibraryW(localDll.c_str());
            }
            if (!d3dCompiler) {
                ErrorLog("d3dcompiler_47.dll not found; CAS disabled\n");
                break;
            }
            using PFN_D3DCompileFromFile = HRESULT(WINAPI*)(LPCWSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
            auto pD3DCompileFromFile = reinterpret_cast<PFN_D3DCompileFromFile>(GetProcAddress(d3dCompiler, "D3DCompileFromFile"));
            if (!pD3DCompileFromFile) {
                FreeLibrary(d3dCompiler);
                break;
            }
            std::wstring shaderPathW = (dllHome / "shaders" / "CAS.hlsl").wstring();
            std::string shaderPath(shaderPathW.begin(), shaderPathW.end());
            Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
            UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
            if (FAILED(pD3DCompileFromFile(shaderPathW.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "mainCS", "cs_5_0", flags, 0, blob.ReleaseAndGetAddressOf(), err.ReleaseAndGetAddressOf()))) {
                std::string errMsg;
                if (err) errMsg.assign((const char*)err->GetBufferPointer(), err->GetBufferSize());
                ErrorLog(fmt::format("Failed to compile CAS.hlsl: {}\n{}\n", shaderPath, errMsg));
                FreeLibrary(d3dCompiler);
                break;
            }
            if (FAILED(d3d->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, s->cs.ReleaseAndGetAddressOf()))) {
                FreeLibrary(d3dCompiler);
                break;
            }
            FreeLibrary(d3dCompiler);
            Log(fmt::format("CAS shader compiled: {}\n", shaderPath));
            shaderCreated = true;
        } while (false);
        if (!shaderCreated) { s->shaderInitFailed = true; return false; }

        // Create Levels shader if enabled
        if (s->levelsEnabled && !s->levelsCS) {
            HMODULE d3dCompiler = LoadLibraryW(L"d3dcompiler_47.dll");
            std::wstring shaderPathW = (dllHome / L"shaders" / L"Levels.hlsl").wstring();
            Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
            if (d3dCompiler) {
                auto pD3DCompileFromFile = reinterpret_cast<HRESULT(WINAPI*)(LPCWSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**)>(GetProcAddress(d3dCompiler, "D3DCompileFromFile"));
                if (pD3DCompileFromFile && SUCCEEDED(pD3DCompileFromFile(shaderPathW.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "mainCS", "cs_5_0", 0, 0, blob.ReleaseAndGetAddressOf(), err.ReleaseAndGetAddressOf()))) {
                    d3d->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, s->levelsCS.ReleaseAndGetAddressOf());
                    Log("Levels shader compiled\n");
                }
                FreeLibrary(d3dCompiler);
            }
            if (!s->levelsCS) {
                ErrorLog("Levels shader missing or failed; levels disabled\n");
                s->levelsEnabled = false;
            } else {
                D3D11_BUFFER_DESC bd{}; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DYNAMIC; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                d3d->CreateBuffer(&bd, nullptr, s->levelsCB.ReleaseAndGetAddressOf());
            }
        }

        // Create FakeHDR shader if enabled
        if (s->fakeHdrEnabled && !s->fakeHdrCS) {
            HMODULE d3dCompiler = LoadLibraryW(L"d3dcompiler_47.dll");
            std::wstring shaderPathW = (dllHome / L"shaders" / L"FakeHDR.hlsl").wstring();
            Microsoft::WRL::ComPtr<ID3DBlob> blob, err;
            if (d3dCompiler) {
                auto pD3DCompileFromFile = reinterpret_cast<HRESULT(WINAPI*)(LPCWSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**)>(GetProcAddress(d3dCompiler, "D3DCompileFromFile"));
                if (pD3DCompileFromFile && SUCCEEDED(pD3DCompileFromFile(shaderPathW.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "mainCS", "cs_5_0", 0, 0, blob.ReleaseAndGetAddressOf(), err.ReleaseAndGetAddressOf()))) {
                    d3d->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, s->fakeHdrCS.ReleaseAndGetAddressOf());
                    Log("FakeHDR shader compiled\n");
                }
                FreeLibrary(d3dCompiler);
            }
            if (!s->fakeHdrCS) {
                ErrorLog("FakeHDR shader missing or failed; fakehdr disabled\n");
                s->fakeHdrEnabled = false;
            } else {
                D3D11_BUFFER_DESC bd{}; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DYNAMIC; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                d3d->CreateBuffer(&bd, nullptr, s->fakeHdrCB.ReleaseAndGetAddressOf());
            }
        }

        // Constant buffer (2x uint4)
        D3D11_BUFFER_DESC bd{};
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.ByteWidth = 2 * sizeof(uint32_t[4]);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(d3d->CreateBuffer(&bd, nullptr, s->constCB.ReleaseAndGetAddressOf()))) {
            ErrorLog("CAS: failed to create const buffer\n");
            return false;
        }
        // Debug CB: one uint (must be 16-byte aligned for D3D11 constant buffers)
        D3D11_BUFFER_DESC bd2{};
        bd2.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd2.ByteWidth = 32;
        bd2.Usage = D3D11_USAGE_DYNAMIC;
        bd2.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(d3d->CreateBuffer(&bd2, nullptr, s->debugCB.ReleaseAndGetAddressOf()))) {
            ErrorLog("CAS: failed to create debug buffer\n");
            return false;
        }

        // Create timestamp queries
        if (!s->qDisjoint) {
            D3D11_QUERY_DESC qd{}; qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
            d3d->CreateQuery(&qd, s->qDisjoint.ReleaseAndGetAddressOf());
            qd.Query = D3D11_QUERY_TIMESTAMP;
            d3d->CreateQuery(&qd, s->qBegin.ReleaseAndGetAddressOf());
            d3d->CreateQuery(&qd, s->qEnd.ReleaseAndGetAddressOf());
        }
        return true;
    }

    static uint64_t makeTempKey(XrSwapchain swapchain, uint32_t arraySlice) {
        return (uint64_t)swapchain ^ (uint64_t(arraySlice) << 32);
    }

    struct TempTextures { Microsoft::WRL::ComPtr<ID3D11Texture2D> input; Microsoft::WRL::ComPtr<ID3D11Texture2D> output; UINT width{}, height{}; DXGI_FORMAT format{}; };

    static void dispatchCas(SessionState* s, XrSwapchain swapchain, ID3D11Texture2D* source, const XrSwapchainSubImage& sub,
                            std::unordered_map<uint64_t, TempTextures>& tempPool) {
        if (!ensureCasObjects(s)) return;

        ID3D11Device* d3d = s->appD3DDevice.Get();
        ID3D11DeviceContext* ctx = s->appD3DContext.Get();

        D3D11_TEXTURE2D_DESC td{};
        source->GetDesc(&td);
        // Only support UAV+copy-safe formats to avoid driver/device crashes
        const bool supported =
            td.Format == DXGI_FORMAT_R8G8B8A8_UNORM || td.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            td.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ||
            td.Format == DXGI_FORMAT_B8G8R8A8_UNORM || td.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            td.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
            td.Format == DXGI_FORMAT_B8G8R8X8_UNORM || td.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ||
            td.Format == DXGI_FORMAT_B8G8R8X8_TYPELESS ||
            td.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (!supported) {
            DebugLog(fmt::format("CAS: unsupported swapchain format {}. Skipping.\n", (int)td.Format));
            return;
        }
        if (td.SampleDesc.Count != 1) {
            Log("CAS: skip MSAA swapchain image\n");
            return; // skip MSAA
        }

        // Use pooled temporary textures per (swapchain,slice)
        const uint64_t poolKey = makeTempKey(swapchain, sub.imageArrayIndex);
        auto& slot = tempPool[poolKey];
        if (!slot.input || slot.width != td.Width || slot.height != td.Height || slot.format != td.Format) {
            D3D11_TEXTURE2D_DESC texDesc = td;
            texDesc.MiscFlags = 0;
            texDesc.CPUAccessFlags = 0;
            texDesc.Usage = D3D11_USAGE_DEFAULT;
            texDesc.ArraySize = 1; // create single-slice 2D textures to match shader resource type
            texDesc.MipLevels = 1;
            // Choose a resource format that allows both SRV and UAV views. Use typeless when needed.
            auto chooseTypeless = [](DXGI_FORMAT f) {
                switch (f) {
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
                case DXGI_FORMAT_B8G8R8A8_UNORM:
                case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
                case DXGI_FORMAT_B8G8R8X8_UNORM:
                case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_TYPELESS;
                default: return f; // keep as-is (eg R16G16B16A16_FLOAT)
                }
            };
            DXGI_FORMAT resourceFormat = chooseTypeless(td.Format);
            texDesc.Format = resourceFormat;
            // Input: SRV+UAV (for ping-pong passes and Levels)
            texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(d3d->CreateTexture2D(&texDesc, nullptr, slot.input.ReleaseAndGetAddressOf()))) {
                ErrorLog("CAS: CreateTexture2D input failed\n");
                return;
            }
            // Output: UAV+SRV
            texDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(d3d->CreateTexture2D(&texDesc, nullptr, slot.output.ReleaseAndGetAddressOf()))) {
                ErrorLog("CAS: CreateTexture2D output failed\n");
                return;
            }
            slot.width = td.Width; slot.height = td.Height; slot.format = td.Format;
        }
        // Copy source slice/rect into input. Use mip 0 always.
        const UINT srcSubresource = D3D11CalcSubresource(0, sub.imageArrayIndex, 1);
        const UINT dstSubresourceInput = D3D11CalcSubresource(0, 0, 1);
        D3D11_BOX inBox{};
        inBox.left = sub.imageRect.offset.x;
        inBox.top = sub.imageRect.offset.y;
        inBox.front = 0;
        const UINT copyWidth = sub.imageRect.extent.width ? (UINT)sub.imageRect.extent.width : td.Width;
        const UINT copyHeight = sub.imageRect.extent.height ? (UINT)sub.imageRect.extent.height : td.Height;
        inBox.right = inBox.left + copyWidth;
        inBox.bottom = inBox.top + copyHeight;
        inBox.back = 1;
        ctx->CopySubresourceRegion(slot.input.Get(), dstSubresourceInput, inBox.left, inBox.top, 0, source, srcSubresource, &inBox);

        // Map formats for SRV/UAV if needed
        auto mapSrvFormat = [](DXGI_FORMAT fmt) {
            switch (fmt) {
            case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM; // force UNORM for SRV
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
            default: return fmt;
            }
        };
        auto mapUavFormat = [](DXGI_FORMAT fmt) {
            switch (fmt) {
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
            case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
            default: return fmt;
            }
        };
        const DXGI_FORMAT srvFormat = mapSrvFormat(td.Format);
        const DXGI_FORMAT uavFormat = mapUavFormat(td.Format);

        // SRV/UAV descs reused for ping-pong
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.Format = srvFormat;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels = 1;
        srvd.Texture2D.MostDetailedMip = 0;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavd{};
        uavd.Format = uavFormat;
        uavd.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavd.Texture2D.MipSlice = 0;

        // Constants
        uint32_t const0[4]{}, const1[4]{};
        float userSharp = s->sharpness;
        // Allow >1.0 by scaling the CAS internal strength non-linearly.
        // For values >1.0, apply an extra multiplier to emulate "super sharp" beyond standard CAS.
        float casStrength = userSharp;
        if (userSharp > 1.0f) {
            casStrength = 1.0f; // saturate CAS's own tuning to 1
        }
        CasSetup(const0, const1, casStrength, (float)td.Width, (float)td.Height, (float)td.Width, (float)td.Height);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(ctx->Map(s->constCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            memcpy(map.pData, const0, sizeof(const0));
            memcpy(reinterpret_cast<uint8_t*>(map.pData) + sizeof(const0), const1, sizeof(const1));
            ctx->Unmap(s->constCB.Get(), 0);
        }

        // Timing begin
        if (s->qDisjoint && s->qBegin && s->qEnd) {
            ctx->Begin(s->qDisjoint.Get());
            ctx->End(s->qBegin.Get());
        }

        // Dispatch passes (ping-pong for >1.0). Ensure UAV/SRV hazards are cleared per pass.
        ctx->CSSetShader(s->cs.Get(), nullptr, 0);
        ID3D11Buffer* cb = s->constCB.Get();
        ctx->CSSetConstantBuffers(0, 1, &cb);
        // Debug overlay disabled in production
        uint32_t dbgFlags = 0u;
        // Populate debug/rect CB: flags + sub-rect offset/extent
        struct DebugCB { UINT flags, offx, offy, extx; UINT exty, pad1, pad2, pad3; } cbData{};
        cbData.flags = dbgFlags;
        cbData.offx = sub.imageRect.offset.x;
        cbData.offy = sub.imageRect.offset.y;
        cbData.extx = sub.imageRect.extent.width ? (UINT)sub.imageRect.extent.width : td.Width;
        cbData.exty = sub.imageRect.extent.height ? (UINT)sub.imageRect.extent.height : td.Height;
        D3D11_MAPPED_SUBRESOURCE mapDbg{};
        if (SUCCEEDED(ctx->Map(s->debugCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapDbg))) {
            memcpy(mapDbg.pData, &cbData, sizeof(cbData));
            ctx->Unmap(s->debugCB.Get(), 0);
        }
        ID3D11Buffer* cbDebug = s->debugCB.Get();
        ctx->CSSetConstantBuffers(1, 1, &cbDebug);
        const UINT width = copyWidth;
        const UINT height = copyHeight;
        const UINT tgx = (width + 15) / 16;
        const UINT tgy = (height + 15) / 16;
        Log(fmt::format("CAS: dispatch {}x{} (groups {}x{}) format={} slice={}\n", width, height, tgx, tgy, (int)td.Format, (int)sub.imageArrayIndex));
        int totalPasses = 1;
        if (userSharp > 1.0f) {
            int extra = (int)floorf(userSharp - 1.0f);
            if (extra < 0) extra = 0; if (extra > 3) extra = 3;
            totalPasses += extra;
        }
        Microsoft::WRL::ComPtr<ID3D11Texture2D> readTex = slot.input;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> writeTex = slot.output;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> curSRV;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> curUAV;
        UINT initCounts[1] = {0};
        for (int pass = 0; pass < totalPasses; ++pass) {
            // Bind SRV from readTex
            curSRV.Reset(); curUAV.Reset();
            if (FAILED(d3d->CreateShaderResourceView(readTex.Get(), &srvd, curSRV.ReleaseAndGetAddressOf()))) {
                ErrorLog("CAS: Create SRV (pass) failed\n");
                return;
            }
            ID3D11ShaderResourceView* srvsX[1] = {curSRV.Get()};
            ctx->CSSetShaderResources(0, 1, srvsX);
            // Bind UAV to writeTex
            if (FAILED(d3d->CreateUnorderedAccessView(writeTex.Get(), &uavd, curUAV.ReleaseAndGetAddressOf()))) {
                ErrorLog("CAS: Create UAV (pass) failed\n");
                return;
            }
            ID3D11UnorderedAccessView* uavsX[1] = {curUAV.Get()};
            ctx->CSSetUnorderedAccessViews(0, 1, uavsX, initCounts);
            // Dispatch
            ctx->Dispatch(tgx, tgy, 1);
            // Unbind to avoid hazards next pass
            ID3D11UnorderedAccessView* nullU[1] = {nullptr};
            ctx->CSSetUnorderedAccessViews(0, 1, nullU, initCounts);
            ID3D11ShaderResourceView* nullS[1] = {nullptr};
            ctx->CSSetShaderResources(0, 1, nullS);
            // Ping-pong
            std::swap(readTex, writeTex);
        }

        // Timing end & readback (best effort)
        if (s->qDisjoint && s->qBegin && s->qEnd) {
            ctx->End(s->qEnd.Get());
            ctx->End(s->qDisjoint.Get());
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            if (S_OK == ctx->GetData(s->qDisjoint.Get(), &disjoint, sizeof(disjoint), 0) && !disjoint.Disjoint) {
                UINT64 t0=0, t1=0;
                if (S_OK == ctx->GetData(s->qBegin.Get(), &t0, sizeof(t0), 0) && S_OK == ctx->GetData(s->qEnd.Get(), &t1, sizeof(t1), 0)) {
                    double ms = double(t1 - t0) / double(disjoint.Frequency) * 1000.0;
                    s->timingAccumMs += ms;
                    if (++s->timingFrameCounter >= 120) {
                        Log(fmt::format("CAS average GPU cost: {:.3f} ms\n", s->timingAccumMs / s->timingFrameCounter));
                        s->timingAccumMs = 0.0; s->timingFrameCounter = 0;
                    }
                }
            }
        }

        // Ensure unbound
        UINT dummyCounts[1] = {0};
        ID3D11UnorderedAccessView* nullUAV[1] = {nullptr};
        ctx->CSSetUnorderedAccessViews(0, 1, nullUAV, dummyCounts);
        ID3D11ShaderResourceView* nullSRV[1] = {nullptr};
        ctx->CSSetShaderResources(0, 1, nullSRV);

        // Optional post-CAS passes
        // Determine final output of CAS passes: last swap put latest result into 'readTex'
        Microsoft::WRL::ComPtr<ID3D11Texture2D> casFinalTex = readTex;

        // Optional FakeHDR pass (before Levels)
        if (s->fakeHdrEnabled && s->fakeHdrCS && s->fakeHdrCB) {
            // Update constants
            D3D11_MAPPED_SUBRESOURCE mapH{};
            if (SUCCEEDED(ctx->Map(s->fakeHdrCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapH))) {
                struct { float pwr, r1, r2, pad0; UINT offx, offy, extx, exty; } cb{};
                cb.pwr = s->fakeHdrPower; cb.r1 = s->fakeHdrRadius1; cb.r2 = s->fakeHdrRadius2; cb.pad0 = 0.0f;
                cb.offx = sub.imageRect.offset.x;
                cb.offy = sub.imageRect.offset.y;
                cb.extx = copyWidth;
                cb.exty = copyHeight;
                memcpy(mapH.pData, &cb, sizeof(cb));
                ctx->Unmap(s->fakeHdrCB.Get(), 0);
            }
            // Read from casFinalTex, write to the other temp
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> hdrSRV;
            D3D11_SHADER_RESOURCE_VIEW_DESC srvdH = srvd;
            d3d->CreateShaderResourceView(casFinalTex.Get(), &srvdH, hdrSRV.ReleaseAndGetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> hdrUAV;
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavdH = uavd;
            ID3D11Texture2D* hdrDst = (casFinalTex.Get() == slot.input.Get()) ? slot.output.Get() : slot.input.Get();
            d3d->CreateUnorderedAccessView(hdrDst, &uavdH, hdrUAV.ReleaseAndGetAddressOf());

            ctx->CSSetShader(s->fakeHdrCS.Get(), nullptr, 0);
            ID3D11Buffer* hdrCB = s->fakeHdrCB.Get();
            ctx->CSSetConstantBuffers(0, 1, &hdrCB);
            ID3D11ShaderResourceView* srvsH[1] = {hdrSRV.Get()};
            ctx->CSSetShaderResources(0, 1, srvsH);
            ID3D11UnorderedAccessView* uavsH[1] = {hdrUAV.Get()};
            ctx->CSSetUnorderedAccessViews(0, 1, uavsH, dummyCounts);
            ctx->Dispatch(tgx, tgy, 1);
            // Unbind
            ID3D11UnorderedAccessView* nullUAVH[1] = {nullptr};
            ctx->CSSetUnorderedAccessViews(0, 1, nullUAVH, dummyCounts);
            ID3D11ShaderResourceView* nullSRVH[1] = {nullptr};
            ctx->CSSetShaderResources(0, 1, nullSRVH);
            // Update final tex
            casFinalTex = hdrDst;
        }

        if (s->levelsEnabled && s->levelsCS && s->levelsCB) {
            D3D11_MAPPED_SUBRESOURCE mapL{};
            if (SUCCEEDED(ctx->Map(s->levelsCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapL))) {
                struct { float inB, inW, outB, outW; float gamma, pad1, pad2, pad3; } lv{};
                lv.inB = s->levelsInBlack; lv.inW = s->levelsInWhite; lv.outB = s->levelsOutBlack; lv.outW = s->levelsOutWhite; lv.gamma = s->levelsGamma;
                memcpy(mapL.pData, &lv, sizeof(lv));
                ctx->Unmap(s->levelsCB.Get(), 0);
            }
            // Read from casFinalTex, write to the other temp
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> levelsSRV;
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd2 = srvd; // same format/dimensions as earlier
            d3d->CreateShaderResourceView(casFinalTex.Get(), &srvd2, levelsSRV.ReleaseAndGetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> levelsUAV;
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavd2 = uavd;
            // choose destination: if casFinalTex==slot.input then write into slot.output else into slot.input
            ID3D11Texture2D* levelsDst = (casFinalTex.Get() == slot.input.Get()) ? slot.output.Get() : slot.input.Get();
            d3d->CreateUnorderedAccessView(levelsDst, &uavd2, levelsUAV.ReleaseAndGetAddressOf());

            ctx->CSSetShader(s->levelsCS.Get(), nullptr, 0);
            ID3D11Buffer* lvCB = s->levelsCB.Get();
            ctx->CSSetConstantBuffers(0, 1, &lvCB);
            ID3D11ShaderResourceView* srvsL[1] = {levelsSRV.Get()};
            ctx->CSSetShaderResources(0, 1, srvsL);
            ID3D11UnorderedAccessView* uavsL[1] = {levelsUAV.Get()};
            ctx->CSSetUnorderedAccessViews(0, 1, uavsL, dummyCounts);
            ctx->Dispatch(tgx, tgy, 1);
            ID3D11UnorderedAccessView* nullUAVL[1] = {nullptr};
            ctx->CSSetUnorderedAccessViews(0, 1, nullUAVL, dummyCounts);
            ID3D11ShaderResourceView* nullSRVL[1] = {nullptr};
            ctx->CSSetShaderResources(0, 1, nullSRVL);
        // After levels, latest output is now in 'levelsDst'
        // Set casFinalTex to the ComPtr that owns that resource
        if (levelsDst == slot.input.Get()) {
            casFinalTex = slot.input;
        } else {
            casFinalTex = slot.output;
        }
        }

        // Copy back (only the processed slice/rect)
        const UINT dstSubresource = D3D11CalcSubresource(0, sub.imageArrayIndex, 1);
        const UINT srcSubresourceOutput = D3D11CalcSubresource(0, 0, 1);
        D3D11_BOX box{};
        box.left = sub.imageRect.offset.x;
        box.top = sub.imageRect.offset.y;
        box.front = 0;
        box.right = sub.imageRect.offset.x + width;
        box.bottom = sub.imageRect.offset.y + height;
        box.back = 1;
        // Copy back from final CAS/Levels output to the original array slice
        ctx->CopySubresourceRegion(source, dstSubresource, box.left, box.top, 0, casFinalTex.Get(), srcSubresourceOutput, &box);
        Log("CAS: completed\n");
    }

    // This class implements our API layer.
    class OpenXrLayer : public openxr_api_layer::OpenXrApi {
      public:
        OpenXrLayer() = default;
        ~OpenXrLayer() = default;

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetInstanceProcAddr
        XrResult xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) override {
            TraceLoggingWrite(g_traceProvider,
                              "xrGetInstanceProcAddr",
                              TLXArg(instance, "Instance"),
                              TLArg(name, "Name"),
                              TLArg(m_bypassApiLayer, "Bypass"));

            XrResult result = m_bypassApiLayer ? m_xrGetInstanceProcAddr(instance, name, function)
                                               : OpenXrApi::xrGetInstanceProcAddr(instance, name, function);

            TraceLoggingWrite(g_traceProvider, "xrGetInstanceProcAddr", TLPArg(*function, "Function"));

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrCreateInstance
        XrResult xrCreateInstance(const XrInstanceCreateInfo* createInfo) override {
            if (createInfo->type != XR_TYPE_INSTANCE_CREATE_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            // Needed to resolve the requested function pointers.
            OpenXrApi::xrCreateInstance(createInfo);

            // Dump the application name, OpenXR runtime information and other useful things for debugging.
            TraceLoggingWrite(g_traceProvider,
                              "xrCreateInstance",
                              TLArg(xr::ToString(createInfo->applicationInfo.apiVersion).c_str(), "ApiVersion"),
                              TLArg(createInfo->applicationInfo.applicationName, "ApplicationName"),
                              TLArg(createInfo->applicationInfo.applicationVersion, "ApplicationVersion"),
                              TLArg(createInfo->applicationInfo.engineName, "EngineName"),
                              TLArg(createInfo->applicationInfo.engineVersion, "EngineVersion"),
                              TLArg(createInfo->createFlags, "CreateFlags"));
            Log(fmt::format("Application: {}\n", createInfo->applicationInfo.applicationName));

            // Here there can be rules to disable the API layer entirely (based on applicationName for example).
            // m_bypassApiLayer = ...

            if (m_bypassApiLayer) {
                Log(fmt::format("{} layer will be bypassed\n", LayerName));
                return XR_SUCCESS;
            }

            for (uint32_t i = 0; i < createInfo->enabledApiLayerCount; i++) {
                TraceLoggingWrite(
                    g_traceProvider, "xrCreateInstance", TLArg(createInfo->enabledApiLayerNames[i], "ApiLayerName"));
            }
            for (uint32_t i = 0; i < createInfo->enabledExtensionCount; i++) {
                TraceLoggingWrite(
                    g_traceProvider, "xrCreateInstance", TLArg(createInfo->enabledExtensionNames[i], "ExtensionName"));
            }

            XrInstanceProperties instanceProperties = {XR_TYPE_INSTANCE_PROPERTIES};
            CHECK_XRCMD(OpenXrApi::xrGetInstanceProperties(GetXrInstance(), &instanceProperties));
            const auto runtimeName = fmt::format("{} {}.{}.{}",
                                                 instanceProperties.runtimeName,
                                                 XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
                                                 XR_VERSION_MINOR(instanceProperties.runtimeVersion),
                                                 XR_VERSION_PATCH(instanceProperties.runtimeVersion));
            TraceLoggingWrite(g_traceProvider, "xrCreateInstance", TLArg(runtimeName.c_str(), "RuntimeName"));
            Log(fmt::format("Using OpenXR runtime: {}\n", runtimeName));

            // Create composition factory (D3D11 only)
            m_compFactory = utils::graphics::createCompositionFrameworkFactory(
                *createInfo, GetXrInstance(), m_xrGetInstanceProcAddr, utils::graphics::CompositionApi::D3D11);

            // Ensure default config exists
            try {
                auto cfgPath = openxr_api_layer::localAppData / "config.cfg";
                if (!std::filesystem::exists(cfgPath)) {
                    std::ofstream out(cfgPath);
                    if (out) {
                        out << "# OpenXR CAS Layer configuration\n";
                        out << "# Sharpening strength (>=0). Values >1.0 apply multiple CAS passes.\n";
                        out << "sharpness=0.6\n";
                        out << "\n# Debug overlay (0/1) and number of frames for border/overlay\n";
                        out << "debug_overlay=0\n";
                        out << "debug_frames=60\n";
                        out << "\n# Optional Levels pass (applied after CAS)\n";
                        out << "levels_enable=0\n";
                        out << "levels_in_black=0.0\n";
                        out << "levels_in_white=1.0\n";
                        out << "levels_out_black=0.0\n";
                        out << "levels_out_white=1.0\n";
                        out << "levels_gamma=1.0\n";
                        out << "\n# Optional FakeHDR pass (applied after CAS, before Levels)\n";
                        out << "fakehdr_enable=0\n";
                        out << "fakehdr_power=1.30\n";
                        out << "fakehdr_radius1=0.793\n";
                        out << "fakehdr_radius2=0.87\n";
                        out.close();
                        Log(fmt::format("Created default config at {}\n", cfgPath.string()));
                    }
                } else {
                    // Backfill missing FakeHDR keys in existing config
                    std::ifstream in(cfgPath);
                    std::string existing((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    in.close();
                    if (existing.find("fakehdr_enable") == std::string::npos) {
                        std::ofstream out(cfgPath, std::ios::app);
                        if (out) {
                            out << "\n# Optional FakeHDR pass (applied after CAS, before Levels)\n";
                            out << "fakehdr_enable=0\n";
                            out << "fakehdr_power=1.30\n";
                            out << "fakehdr_radius1=0.793\n";
                            out << "fakehdr_radius2=0.87\n";
                            out.close();
                            Log("Appended FakeHDR defaults to existing config\n");
                        }
                    }
                }
            } catch (...) {
            }

            return XR_SUCCESS;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetSystem
        XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) override {
            if (getInfo->type != XR_TYPE_SYSTEM_GET_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrGetSystem",
                              TLXArg(instance, "Instance"),
                              TLArg(xr::ToCString(getInfo->formFactor), "FormFactor"));

            const XrResult result = OpenXrApi::xrGetSystem(instance, getInfo, systemId);
            if (XR_SUCCEEDED(result) && getInfo->formFactor == XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
                if (*systemId != m_systemId) {
                    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
                    CHECK_XRCMD(OpenXrApi::xrGetSystemProperties(instance, *systemId, &systemProperties));
                    TraceLoggingWrite(g_traceProvider, "xrGetSystem", TLArg(systemProperties.systemName, "SystemName"));
                    Log(fmt::format("Using OpenXR system: {}\n", systemProperties.systemName));
                }

                // Remember the XrSystemId to use.
                m_systemId = *systemId;
            }

            TraceLoggingWrite(g_traceProvider, "xrGetSystem", TLArg((int)*systemId, "SystemId"));

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrCreateSession
        XrResult xrCreateSession(XrInstance instance,
                                 const XrSessionCreateInfo* createInfo,
                                 XrSession* session) override {
            if (createInfo->type != XR_TYPE_SESSION_CREATE_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrCreateSession",
                              TLXArg(instance, "Instance"),
                              TLArg((int)createInfo->systemId, "SystemId"),
                              TLArg(createInfo->createFlags, "CreateFlags"));

            const XrResult result = OpenXrApi::xrCreateSession(instance, createInfo, session);
            if (XR_SUCCEEDED(result)) {
                auto state = std::make_unique<SessionState>();

                // Optional: get framework for serialization if available
                if (m_compFactory) {
                    if (auto* comp = m_compFactory->getCompositionFramework(*session)) {
                        state->composition = std::shared_ptr<utils::graphics::ICompositionFramework>(
                            comp, [](utils::graphics::ICompositionFramework*) {});
                    }
                }

                // Extract D3D11 device from session create chain (defensive: runtime may rewrap next)
                const XrBaseInStructure* cur = reinterpret_cast<const XrBaseInStructure*>(createInfo->next);
                while (cur) {
                    if (cur->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
                        const XrGraphicsBindingD3D11KHR* d3d11 = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(cur);
                        state->appD3DDevice = d3d11->device;
                        if (state->appD3DDevice) {
                            ID3D11DeviceContext* tmpCtx = nullptr;
                            state->appD3DDevice->GetImmediateContext(&tmpCtx);
                            state->appD3DContext.Attach(tmpCtx);
                        }
                        break;
                    }
                    cur = cur->next;
                }
                if (!state->appD3DDevice) {
                    Log("CAS layer: no D3D11 graphics binding found; layer will be inactive for this session\n");
                }

                state->sharpness = resolveSharpnessFromConfigOrEnv();
                // Read debug controls from env or config
                // Frames
                {
                    // Priority: env > config
                    char env[32]{};
                    if (GetEnvironmentVariableA("XR_CAS_DEBUG_FRAMES", env, sizeof(env)) > 0) {
                        try { int v = std::stoi(env); if (v >= 0) state->debugFramesMax = (uint32_t)v; } catch (...) {}
                    }
                    if (auto s = tryReadConfigValue("debug_frames")) {
                        try { int v = std::stoi(*s); if (v >= 0) state->debugFramesMax = (uint32_t)v; } catch (...) {}
                    }
                }
                // Overlay
                {
                    // Priority: env > config
                    char env[8]{};
                    if (GetEnvironmentVariableA("XR_CAS_DEBUG_OVERLAY", env, sizeof(env)) > 0) {
                        state->debugOverlay = (env[0] != '0');
                    }
                    if (auto s = tryReadConfigValue("debug_overlay")) {
                        std::string val = *s; std::transform(val.begin(), val.end(), val.begin(), ::tolower);
                        state->debugOverlay = (val == "1" || val == "true" || val == "yes");
                    }
                }
                // Levels from config
                {
                    if (auto s = tryReadConfigValue("levels_enable")) {
                        std::string v=*s; std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                        state->levelsEnabled = (v=="1"||v=="true"||v=="yes");
                    }
                    if (auto s = tryReadConfigValue("levels_in_black")) try { state->levelsInBlack = std::stof(*s); } catch (...) {}
                    if (auto s = tryReadConfigValue("levels_in_white")) try { state->levelsInWhite = std::stof(*s); } catch (...) {}
                    if (auto s = tryReadConfigValue("levels_out_black")) try { state->levelsOutBlack = std::stof(*s); } catch (...) {}
                    if (auto s = tryReadConfigValue("levels_out_white")) try { state->levelsOutWhite = std::stof(*s); } catch (...) {}
                    if (auto s = tryReadConfigValue("levels_gamma")) try { state->levelsGamma = std::stof(*s); } catch (...) {}
                }
                // FakeHDR from config
                {
                    if (auto s = tryReadConfigValue("fakehdr_enable")) {
                        std::string v=*s; std::transform(v.begin(), v.end(), v.begin(), ::tolower);
                        state->fakeHdrEnabled = (v=="1"||v=="true"||v=="yes");
                    }
                    if (auto s = tryReadConfigValue("fakehdr_power")) try { state->fakeHdrPower = std::stof(*s); } catch (...) {}
                    if (auto s = tryReadConfigValue("fakehdr_radius1")) try { state->fakeHdrRadius1 = std::stof(*s); } catch (...) {}
                    if (auto s = tryReadConfigValue("fakehdr_radius2")) try { state->fakeHdrRadius2 = std::stof(*s); } catch (...) {}
                }
                Log(fmt::format("CAS sharpness set to {:.3f}\n", state->sharpness));
                Log(fmt::format("CAS debug: overlay={} frames={}\n", state->debugOverlay ? 1 : 0, state->debugFramesMax));
                m_sessions[*session] = std::move(state);

                TraceLoggingWrite(g_traceProvider, "xrCreateSession", TLXArg(*session, "Session"));
            }

            return result;
        }

        // Pre-enumerate swapchain images and remember textures
        XrResult xrCreateSwapchain(XrSession session,
                                   const XrSwapchainCreateInfo* createInfo,
                                   XrSwapchain* swapchain) override {
            const XrResult r = OpenXrApi::xrCreateSwapchain(session, createInfo, swapchain);
            if (XR_SUCCEEDED(r)) {
                try {
                    auto sit = m_sessions.find(session);
                    if (sit != m_sessions.end() && sit->second->appD3DDevice) {
                        // Only attempt D3D11 image enumeration when we know we're D3D11
                        std::vector<XrSwapchainImageD3D11KHR> images;
                        uint32_t count = 0;
                        xrEnumerateSwapchainImages(*swapchain, 0, &count, nullptr);
                        if (count > 0) {
                            images.resize(count);
                            for (auto& img : images) img.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR, img.next = nullptr;
                            if (XR_SUCCEEDED(xrEnumerateSwapchainImages(*swapchain,
                                                                        count,
                                                                        &count,
                                                                        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))) {
                                std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> texList;
                                texList.reserve(count);
                                for (auto& img : images) {
                                    texList.emplace_back(img.texture);
                                }
                                m_swapchainImages.insert_or_assign(*swapchain, std::move(texList));
                                Log(fmt::format("Cached {} D3D11 swapchain images for {} (create)\n", count, (void*)*swapchain));
                            }
                        }
                    }
                } catch (...) {
                    ErrorLog("xrCreateSwapchain: exception during D3D11 image caching\n");
                }
            }
            return r;
        }

        XrResult xrDestroySwapchain(XrSwapchain swapchain) override {
            // Cleanup bookkeeping
            m_acquired.erase(swapchain);
            m_lastReleased.erase(swapchain);
            m_swapchainImages.erase(swapchain);
            return OpenXrApi::xrDestroySwapchain(swapchain);
        }

        // Track swapchain image acquire/release to know which image to process.
        XrResult xrAcquireSwapchainImage(XrSwapchain swapchain,
                                         const XrSwapchainImageAcquireInfo* acquireInfo,
                                         uint32_t* index) override {
            const XrResult r = OpenXrApi::xrAcquireSwapchainImage(swapchain, acquireInfo, index);
            if (XR_SUCCEEDED(r)) {
                TraceLoggingWrite(g_traceProvider, "xrAcquireSwapchainImage", TLArg((int)*index, "Index"));
                m_acquired[swapchain].push_back(*index);
            }
            return r;
        }

        XrResult xrReleaseSwapchainImage(XrSwapchain swapchain,
                                         const XrSwapchainImageReleaseInfo* releaseInfo) override {
            const XrResult r = OpenXrApi::xrReleaseSwapchainImage(swapchain, releaseInfo);
            if (XR_SUCCEEDED(r)) {
                auto& dq = m_acquired[swapchain];
                if (!dq.empty()) {
                    m_lastReleased[swapchain] = dq.front();
                    dq.pop_front();
                }
                Log(fmt::format("Swapchain {} released image index {}\n", (void*)swapchain, (int)m_lastReleased[swapchain].value_or(-1)));
            }
            return r;
        }

        // Minimal hook to serialize, then process the most recent color swapchain image via CAS.
        XrResult xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) override {
            try {
                auto it = m_sessions.find(session);
                if (it != m_sessions.end()) {
                    if (it->second->composition) {
                        it->second->composition->serializePreComposition();
                    }
                Log(fmt::format("xrEndFrame: intercept, layerCount={}\n", frameEndInfo ? (int)frameEndInfo->layerCount : 0));
                if (frameEndInfo && frameEndInfo->layerCount > 0) {
                    const XrCompositionLayerBaseHeader* base0 = frameEndInfo->layers[0];
                    DebugLog(fmt::format("FirstLayer type={} (no flags in this OpenXR header)\n", base0 ? (int)base0->type : -1));
                }

        // Process first projection layer found; apply to all its views (L/R)
                    if (frameEndInfo && frameEndInfo->layerCount > 0) {
                        const XrCompositionLayerProjection* projLayer = nullptr;
                        for (uint32_t li = 0; li < frameEndInfo->layerCount; ++li) {
                            const XrCompositionLayerBaseHeader* base = frameEndInfo->layers[li];
                            if (base && base->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                                projLayer = reinterpret_cast<const XrCompositionLayerProjection*>(base);
                                break;
                            }
                        }
            if (projLayer && projLayer->viewCount > 0) {
                            for (uint32_t vi = 0; vi < projLayer->viewCount; ++vi) {
                                const XrSwapchainSubImage& sub = projLayer->views[vi].subImage;
                                auto lastIt = m_lastReleased.find(sub.swapchain);
                                if (lastIt != m_lastReleased.end() && lastIt->second.has_value()) {
                                    const uint32_t idx = lastIt->second.value();
                                    auto imgIt = m_swapchainImages.find(sub.swapchain);
                        if (imgIt == m_swapchainImages.end()) {
                            // Fallback: enumerate images now (D3D11 only)
                            std::vector<XrSwapchainImageD3D11KHR> images;
                            uint32_t count = 0;
                            xrEnumerateSwapchainImages(sub.swapchain, 0, &count, nullptr);
                            if (count > 0) {
                                images.resize(count);
                                for (auto& img : images) img.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR, img.next = nullptr;
                                if (XR_SUCCEEDED(xrEnumerateSwapchainImages(sub.swapchain,
                                                                            count,
                                                                            &count,
                                                                            reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))) {
                                    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> texList;
                                    texList.reserve(count);
                                    for (auto& img : images) texList.emplace_back(img.texture);
                                    m_swapchainImages.insert_or_assign(sub.swapchain, std::move(texList));
                                    Log(fmt::format("Cached {} D3D11 swapchain images for {} (fallback)\n", count, (void*)sub.swapchain));
                                }
                            }
                            imgIt = m_swapchainImages.find(sub.swapchain);
                        }
                                    if (imgIt != m_swapchainImages.end() && idx < imgIt->second.size()) {
                                        Log(fmt::format("CAS: processing view {} image index {} ({}x{})\n", (int)vi, idx, sub.imageRect.extent.width, sub.imageRect.extent.height));
                                        // Guard against null textures
                                    if (imgIt->second[idx]) {
                                dispatchCas(m_sessions[session].get(), sub.swapchain, imgIt->second[idx].Get(), sub, m_tempPool);
                                        } else {
                        Log("CAS: null D3D11 texture pointer; skipping.\n");
                                        }
                        } else {
                            Log("CAS: no cached images or index out of range; skipping.\n");
                                    }
                    } else {
                        Log("CAS: no last-released image to process.\n");
                    }
                            }
                        } else {
                Log("No projection layer found; CAS skipped\n");
                if (frameEndInfo && frameEndInfo->layerCount > 0) {
                    for (uint32_t li = 0; li < frameEndInfo->layerCount; ++li) {
                        const XrCompositionLayerBaseHeader* base = frameEndInfo->layers[li];
                        DebugLog(fmt::format("Layer[{}] type={} (no flags in this OpenXR header)\n", (int)li, base ? (int)base->type : -1));
                    }
                }
                        }
                    }

                    if (it->second->composition) {
                        it->second->composition->serializePostComposition();
                    }
                }
            } catch (...) {
                ErrorLog("xrEndFrame: exception in layer processing\n");
            }
            return OpenXrApi::xrEndFrame(session, frameEndInfo);
        }

      private:
        bool isSystemHandled(XrSystemId systemId) const {
            return systemId == m_systemId;
        }

        bool m_bypassApiLayer{false};
        XrSystemId m_systemId{XR_NULL_SYSTEM_ID};
        std::shared_ptr<utils::graphics::ICompositionFrameworkFactory> m_compFactory;
        std::unordered_map<XrSession, std::unique_ptr<SessionState>> m_sessions;
        std::unordered_map<XrSwapchain, std::deque<uint32_t>> m_acquired;
        std::unordered_map<XrSwapchain, std::optional<uint32_t>> m_lastReleased;
        std::unordered_map<XrSwapchain, std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>>> m_swapchainImages;
        std::unordered_map<uint64_t, TempTextures> m_tempPool;
    };

    // This method is required by the framework to instantiate your OpenXrApi implementation.
    OpenXrApi* GetInstance() {
        if (!g_instance) {
            g_instance = std::make_unique<OpenXrLayer>();
        }
        return g_instance.get();
    }

} // namespace openxr_api_layer

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        TraceLoggingRegister(openxr_api_layer::log::g_traceProvider);
        break;

    case DLL_PROCESS_DETACH:
        TraceLoggingUnregister(openxr_api_layer::log::g_traceProvider);
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
