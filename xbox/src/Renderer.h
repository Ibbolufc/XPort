// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#pragma once

#include <string>

namespace xport
{

enum class TextStyle
{
	Title,
	Heading,
	Body,
	Small,
};

// D3D11 swap chain on the app's CoreWindow, with Direct2D/DirectWrite drawing on top.
// The D3D11 device is the one the stream decoder (D3D11VA) and video renderer will share
// in later phases. All drawing coordinates are in DIPs (device-independent pixels).
class Renderer
{
public:
	void SetWindow(winrt::Windows::UI::Core::CoreWindow const &window);
	void OnSizeChanged();
	void Trim(); // required on suspend

	bool BeginFrame(D2D1_COLOR_F clear);
	void EndFrame();

	void Text(const std::wstring &text, float x, float y, float w, float h, TextStyle style, D2D1_COLOR_F color);
	void FillRect(float x, float y, float w, float h, D2D1_COLOR_F color);
	void StrokeRect(float x, float y, float w, float h, D2D1_COLOR_F color, float thickness = 2.0f);

	float Width() const { return width_dips; }
	float Height() const { return height_dips; }
	std::wstring AdapterDescription() const { return adapter_desc; }
	D3D_FEATURE_LEVEL FeatureLevel() const { return feature_level; }

private:
	void CreateDeviceIndependentResources();
	void CreateDeviceResources();
	void CreateWindowSizeDependentResources();
	void ReleaseTarget();
	void HandleDeviceLost();

	winrt::agile_ref<winrt::Windows::UI::Core::CoreWindow> window;
	float dpi = 96.0f;
	float width_dips = 0.0f;
	float height_dips = 0.0f;
	std::wstring adapter_desc;
	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;

	winrt::com_ptr<ID3D11Device> d3d_device;
	winrt::com_ptr<ID3D11DeviceContext> d3d_context;
	winrt::com_ptr<IDXGISwapChain1> swap_chain;

	winrt::com_ptr<ID2D1Factory1> d2d_factory;
	winrt::com_ptr<ID2D1Device> d2d_device;
	winrt::com_ptr<ID2D1DeviceContext> d2d_context;
	winrt::com_ptr<ID2D1Bitmap1> d2d_target;
	winrt::com_ptr<ID2D1SolidColorBrush> brush;

	winrt::com_ptr<IDWriteFactory> dwrite_factory;
	winrt::com_ptr<IDWriteTextFormat> fmt_title, fmt_heading, fmt_body, fmt_small;
};

} // namespace xport
