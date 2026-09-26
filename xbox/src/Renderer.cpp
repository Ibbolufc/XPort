// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "pch.h"
#include "Renderer.h"
#include "Log.h"

#include <algorithm>
#include <cmath>

using namespace winrt;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::Graphics::Display;

namespace xport
{

void Renderer::SetWindow(CoreWindow const &w)
{
	window = winrt::make_agile(w);
	dpi = DisplayInformation::GetForCurrentView().LogicalDpi();
	CreateDeviceIndependentResources();
	CreateDeviceResources();
	CreateWindowSizeDependentResources();
}

void Renderer::OnSizeChanged()
{
	dpi = DisplayInformation::GetForCurrentView().LogicalDpi();
	CreateWindowSizeDependentResources();
}

void Renderer::Trim()
{
	if(!d3d_device)
		return;
	d3d_context->ClearState();
	if(auto dxgi3 = d3d_device.try_as<IDXGIDevice3>())
		dxgi3->Trim();
}

void Renderer::CreateDeviceIndependentResources()
{
	D2D1_FACTORY_OPTIONS options = {};
	check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, d2d_factory.put_void()));
	check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
		reinterpret_cast<::IUnknown **>(dwrite_factory.put())));

	auto make_format = [&](float size, DWRITE_FONT_WEIGHT weight, com_ptr<IDWriteTextFormat> &out) {
		check_hresult(dwrite_factory->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out.put()));
		out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
	};
	make_format(56.0f, DWRITE_FONT_WEIGHT_BOLD, fmt_title);
	make_format(26.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, fmt_heading);
	make_format(20.0f, DWRITE_FONT_WEIGHT_NORMAL, fmt_body);
	make_format(15.0f, DWRITE_FONT_WEIGHT_NORMAL, fmt_small);
}

void Renderer::CreateDeviceResources()
{
	// BGRA support is required for Direct2D interop.
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
	};
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, ARRAYSIZE(levels),
		D3D11_SDK_VERSION, d3d_device.put(), &feature_level, d3d_context.put());
	if(FAILED(hr))
	{
		XLOG("Renderer: hardware D3D11 device failed (0x%08lx), falling back to WARP", (unsigned long)hr);
		check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, ARRAYSIZE(levels),
			D3D11_SDK_VERSION, d3d_device.put(), &feature_level, d3d_context.put()));
	}

	auto dxgi_device = d3d_device.as<IDXGIDevice>();
	com_ptr<IDXGIAdapter> adapter;
	check_hresult(dxgi_device->GetAdapter(adapter.put()));
	DXGI_ADAPTER_DESC desc = {};
	adapter->GetDesc(&desc);
	adapter_desc = desc.Description;
	XLOG("Renderer: D3D11 device on \"%ls\", feature level 0x%x", adapter_desc.c_str(), (unsigned)feature_level);

	check_hresult(d2d_factory->CreateDevice(dxgi_device.get(), d2d_device.put()));
	check_hresult(d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2d_context.put()));
	check_hresult(d2d_context->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), brush.put()));
}

void Renderer::ReleaseTarget()
{
	if(d2d_context)
		d2d_context->SetTarget(nullptr);
	d2d_target = nullptr;
}

void Renderer::CreateWindowSizeDependentResources()
{
	ReleaseTarget();

	CoreWindow w = window.get();
	auto bounds = w.Bounds();
	width_dips = bounds.Width;
	height_dips = bounds.Height;
	UINT px_w = (UINT)std::max(1L, std::lround(bounds.Width * dpi / 96.0f));
	UINT px_h = (UINT)std::max(1L, std::lround(bounds.Height * dpi / 96.0f));

	if(swap_chain)
	{
		HRESULT hr = swap_chain->ResizeBuffers(2, px_w, px_h, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
		if(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		{
			HandleDeviceLost();
			return;
		}
		check_hresult(hr);
	}
	else
	{
		DXGI_SWAP_CHAIN_DESC1 desc = {};
		desc.Width = px_w;
		desc.Height = px_h;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 2;
		desc.Scaling = DXGI_SCALING_STRETCH;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

		auto dxgi_device = d3d_device.as<IDXGIDevice1>();
		com_ptr<IDXGIAdapter> adapter;
		check_hresult(dxgi_device->GetAdapter(adapter.put()));
		com_ptr<IDXGIFactory2> factory;
		check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()));
		check_hresult(factory->CreateSwapChainForCoreWindow(d3d_device.get(), winrt::get_unknown(w), &desc, nullptr, swap_chain.put()));
		// Lowest latency: never queue more than one frame (matters once video is on screen).
		dxgi_device->SetMaximumFrameLatency(1);
	}
	XLOG("Renderer: swap chain %ux%u px (%.0fx%.0f DIPs @ %.0f dpi)", px_w, px_h, width_dips, height_dips, dpi);

	com_ptr<IDXGISurface> surface;
	check_hresult(swap_chain->GetBuffer(0, __uuidof(IDXGISurface), surface.put_void()));
	D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
		D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
	check_hresult(d2d_context->CreateBitmapFromDxgiSurface(surface.get(), &props, d2d_target.put()));
	d2d_context->SetTarget(d2d_target.get());
	d2d_context->SetDpi(dpi, dpi);
	d2d_context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}

void Renderer::HandleDeviceLost()
{
	XLOG("Renderer: device lost, recreating");
	ReleaseTarget();
	swap_chain = nullptr;
	brush = nullptr;
	d2d_context = nullptr;
	d2d_device = nullptr;
	d3d_context = nullptr;
	d3d_device = nullptr;
	CreateDeviceResources();
	CreateWindowSizeDependentResources();
}

bool Renderer::BeginFrame(D2D1_COLOR_F clear)
{
	if(!d2d_target)
		return false;
	d2d_context->BeginDraw();
	d2d_context->Clear(clear);
	return true;
}

void Renderer::EndFrame()
{
	HRESULT hr = d2d_context->EndDraw();
	if(hr == D2DERR_RECREATE_TARGET)
	{
		HandleDeviceLost();
		return;
	}
	hr = swap_chain->Present(1, 0); // vsync: also paces the input/update loop
	if(hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		HandleDeviceLost();
}

void Renderer::Text(const std::wstring &text, float x, float y, float w, float h, TextStyle style, D2D1_COLOR_F color)
{
	IDWriteTextFormat *fmt = fmt_body.get();
	switch(style)
	{
		case TextStyle::Title: fmt = fmt_title.get(); break;
		case TextStyle::Heading: fmt = fmt_heading.get(); break;
		case TextStyle::Body: fmt = fmt_body.get(); break;
		case TextStyle::Small: fmt = fmt_small.get(); break;
	}
	brush->SetColor(color);
	d2d_context->DrawText(text.c_str(), (UINT32)text.size(), fmt, D2D1::RectF(x, y, x + w, y + h), brush.get(),
		D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Renderer::FillRect(float x, float y, float w, float h, D2D1_COLOR_F color)
{
	brush->SetColor(color);
	d2d_context->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush.get());
}

void Renderer::StrokeRect(float x, float y, float w, float h, D2D1_COLOR_F color, float thickness)
{
	brush->SetColor(color);
	d2d_context->DrawRectangle(D2D1::RectF(x, y, x + w, y + h), brush.get(), thickness);
}

} // namespace xport
