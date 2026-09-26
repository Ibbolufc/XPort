// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// XPort — Milestone 1 app shell.
//
// A plain CoreApplication (no XAML) UWP app: one CoreWindow, a D3D11 swap chain with
// Direct2D text on top, Windows.Gaming.Input for controllers and a Winsock LAN probe.
// It proves the packaging, lifecycle, rendering, input and networking plumbing on an
// Xbox Series X|S in Dev Mode before any Remote Play code (lib/) is linked in.

#include "pch.h"
#include "Log.h"
#include "NetProbe.h"
#include "Renderer.h"

#include <cstdarg>
#include <cstdio>

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Gaming::Input;
using namespace winrt::Windows::UI::Core;

#ifndef XPORT_VERSION
#define XPORT_VERSION "dev"
#endif

namespace
{

D2D1_COLOR_F Rgb(uint32_t rgb, float a = 1.0f)
{
	return D2D1::ColorF(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, a);
}

const D2D1_COLOR_F kBackground = Rgb(0x101418);
const D2D1_COLOR_F kPanel = Rgb(0x1a2028);
const D2D1_COLOR_F kText = Rgb(0xe8edf2);
const D2D1_COLOR_F kMuted = Rgb(0x8a96a3);
const D2D1_COLOR_F kAccent = Rgb(0x2f9e6e);
const D2D1_COLOR_F kActive = Rgb(0x3fcf8e);

// How each Xbox control will map onto the DualShock/DualSense layout the console expects
// (see the migration plan). Shown live so the mapping can be checked on real hardware.
struct ButtonMapping
{
	GamepadButtons button;
	const wchar_t *xbox;
	const wchar_t *ps;
};

const ButtonMapping kMappings[] = {
	{GamepadButtons::A, L"A", L"Cross"},
	{GamepadButtons::B, L"B", L"Circle"},
	{GamepadButtons::X, L"X", L"Square"},
	{GamepadButtons::Y, L"Y", L"Triangle"},
	{GamepadButtons::LeftShoulder, L"LB", L"L1"},
	{GamepadButtons::RightShoulder, L"RB", L"R1"},
	{GamepadButtons::LeftThumbstick, L"LS click", L"L3"},
	{GamepadButtons::RightThumbstick, L"RS click", L"R3"},
	{GamepadButtons::View, L"View", L"Share / Create"},
	{GamepadButtons::Menu, L"Menu", L"Options"},
	{GamepadButtons::DPadUp, L"D-pad Up", L"D-pad Up"},
	{GamepadButtons::DPadDown, L"D-pad Down", L"D-pad Down"},
	{GamepadButtons::DPadLeft, L"D-pad Left", L"D-pad Left"},
	{GamepadButtons::DPadRight, L"D-pad Right", L"D-pad Right"},
};

bool Has(GamepadButtons buttons, GamepadButtons b)
{
	return (buttons & b) == b;
}

std::wstring Widen(const std::string &s)
{
	if(s.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring out((size_t)n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
	return out;
}

std::wstring Format(const wchar_t *fmt, ...)
{
	wchar_t buf[512];
	va_list args;
	va_start(args, fmt);
	_vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
	va_end(args);
	return buf;
}

constexpr ULONGLONG kExitHoldMs = 3000;

} // namespace

struct App : implements<App, IFrameworkViewSource, IFrameworkView>
{
	IFrameworkView CreateView()
	{
		return *this;
	}

	void Initialize(CoreApplicationView const &view)
	{
		xport::log::Init();
		XLOG("XPort %s starting (Milestone 1 shell)", XPORT_VERSION);
		XLOG("Log file: %ls", xport::log::FilePath().c_str());

		view.Activated({this, &App::OnActivated});
		CoreApplication::Suspending({this, &App::OnSuspending});
		CoreApplication::Resuming({this, &App::OnResuming});
		Gamepad::GamepadAdded([](auto &&, Gamepad const &) { XLOG("Controller connected (%u total)", Gamepad::Gamepads().Size()); });
		Gamepad::GamepadRemoved([](auto &&, Gamepad const &) { XLOG("Controller disconnected (%u total)", Gamepad::Gamepads().Size()); });
	}

	void Load(hstring const &)
	{
	}

	void Uninitialize()
	{
		StopVibration();
		XLOG("Uninitialize");
		xport::log::Shutdown();
	}

	void SetWindow(CoreWindow const &window)
	{
		window.Closed([this](auto &&, auto &&) { closed = true; });
		window.VisibilityChanged([this](auto &&, VisibilityChangedEventArgs const &args) { visible = args.Visible(); });
		window.SizeChanged([this](auto &&, auto &&) { renderer.OnSizeChanged(); });

		// On Xbox, B raises BackRequested; unhandled it would navigate "back" out of the app.
		SystemNavigationManager::GetForCurrentView().BackRequested([](auto &&, BackRequestedEventArgs const &args) {
			args.Handled(true);
		});

		renderer.SetWindow(window);
	}

	void Run()
	{
		CoreWindow window = CoreWindow::GetForCurrentThread();
		window.Activate();
		XLOG("Running");

		while(!closed)
		{
			if(visible)
			{
				window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
				Update();
				Render(); // Present() blocks on vsync and paces this loop
			}
			else
			{
				window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
			}
		}
	}

private:
	void OnActivated(CoreApplicationView const &, IActivatedEventArgs const &args)
	{
		XLOG("Activated (kind %d, previous state %d)", (int)args.Kind(), (int)args.PreviousExecutionState());
		CoreWindow::GetForCurrentThread().Activate();
	}

	void OnSuspending(IInspectable const &, SuspendingEventArgs const &args)
	{
		auto deferral = args.SuspendingOperation().GetDeferral();
		XLOG("Suspending");
		StopVibration();
		renderer.Trim(); // required for UWP apps on suspend
		deferral.Complete();
	}

	void OnResuming(IInspectable const &, IInspectable const &)
	{
		XLOG("Resuming");
	}

	void StopVibration()
	{
		if(pad)
			pad.Vibration(GamepadVibration{});
	}

	void Update()
	{
		auto pads = Gamepad::Gamepads();
		pad_count = pads.Size();
		if(pad_count == 0)
		{
			pad = nullptr;
			reading = {};
			exit_hold_start = 0;
			return;
		}
		pad = pads.GetAt(0);
		GamepadButtons previous = reading.Buttons;
		reading = pad.GetCurrentReading();

		// Y: run the LAN discovery probe (edge-triggered)
		if(Has(reading.Buttons, GamepadButtons::Y) && !Has(previous, GamepadButtons::Y) && !probe.Running())
		{
			XLOG("Starting LAN discovery probe");
			probe.Start();
		}

		// Rumble / impulse-trigger test: X drives the body motors, LT/RT their trigger motors.
		GamepadVibration vib{};
		if(Has(reading.Buttons, GamepadButtons::X))
		{
			vib.LeftMotor = 0.6;
			vib.RightMotor = 0.3;
		}
		vib.LeftTrigger = reading.LeftTrigger * 0.5;
		vib.RightTrigger = reading.RightTrigger * 0.5;
		pad.Vibration(vib);

		// Hold View + Menu to exit (the Guide button is reserved by the system).
		bool exit_combo = Has(reading.Buttons, GamepadButtons::View) && Has(reading.Buttons, GamepadButtons::Menu);
		ULONGLONG now = GetTickCount64();
		if(!exit_combo)
			exit_hold_start = 0;
		else if(exit_hold_start == 0)
			exit_hold_start = now;
		else if(now - exit_hold_start >= kExitHoldMs)
		{
			XLOG("Exit requested via View+Menu");
			StopVibration();
			CoreApplication::Exit();
		}
	}

	void Render()
	{
		if(!renderer.BeginFrame(kBackground))
			return;

		const float W = renderer.Width();
		const float H = renderer.Height();
		// Stay inside the TV title-safe area.
		const float mx = W * 0.05f;
		const float my = H * 0.05f;
		const float cw = W - 2 * mx;

		float y = my;
		renderer.Text(L"XPort", mx, y, cw, 70, xport::TextStyle::Title, kText);
		renderer.Text(Format(L"v%hs", XPORT_VERSION), mx + 175, y + 28, 300, 30, xport::TextStyle::Body, kMuted);
		y += 72;
		renderer.Text(L"PlayStation Remote Play for Xbox Series X|S  —  Milestone 1: app shell (Remote Play not wired in yet)",
			mx, y, cw, 30, xport::TextStyle::Body, kMuted);
		y += 34;
		renderer.FillRect(mx, y, cw, 3, kAccent);
		y += 20;

		const float col_gap = 24;
		const float col_w = (cw - col_gap) / 2;
		const float panel_h = H - my - y - 60 - 190;
		DrawControllerPanel(mx, y, col_w, panel_h);
		DrawNetworkPanel(mx + col_w + col_gap, y, col_w, panel_h);
		y += panel_h + 16;
		DrawLogPanel(mx, y, cw, 174);

		std::wstring footer = L"Y: search for consoles    X: rumble test    LT/RT: trigger rumble    Hold View + Menu: exit";
		if(exit_hold_start)
			footer = Format(L"Exiting in %.1f s — keep holding View + Menu",
				(kExitHoldMs - (GetTickCount64() - exit_hold_start)) / 1000.0);
		renderer.Text(footer, mx, H - my - 30, cw, 30, xport::TextStyle::Body, exit_hold_start ? kActive : kMuted);

		renderer.EndFrame();
	}

	void DrawControllerPanel(float x, float y, float w, float h)
	{
		renderer.FillRect(x, y, w, h, kPanel);
		const float pad_x = x + 20;
		float cy = y + 16;
		renderer.Text(L"Controller", pad_x, cy, w - 40, 34, xport::TextStyle::Heading, kText);
		cy += 40;

		if(!pad)
		{
			renderer.Text(L"No controller detected", pad_x, cy, w - 40, 28, xport::TextStyle::Body, kMuted);
			return;
		}
		renderer.Text(Format(L"%u connected — showing controller 1.  Xbox → PlayStation mapping:", pad_count),
			pad_x, cy, w - 40, 28, xport::TextStyle::Small, kMuted);
		cy += 30;

		const float row_h = 27;
		const float half = (w - 40) / 2;
		size_t count = sizeof(kMappings) / sizeof(kMappings[0]);
		size_t rows = (count + 1) / 2;
		for(size_t i = 0; i < count; i++)
		{
			const auto &m = kMappings[i];
			bool on = Has(reading.Buttons, m.button);
			float bx = pad_x + (i < rows ? 0 : half);
			float by = cy + (float)(i % rows) * row_h;
			if(on)
				renderer.FillRect(bx - 6, by, half - 12, row_h - 3, kAccent);
			renderer.Text(Format(L"%ls → %ls", m.xbox, m.ps), bx, by, half - 16, row_h, xport::TextStyle::Body,
				on ? kText : kMuted);
		}
		cy += (float)rows * row_h + 8;

		renderer.Text(Format(L"LT → L2  %3.0f%%      RT → R2  %3.0f%%", reading.LeftTrigger * 100.0, reading.RightTrigger * 100.0),
			pad_x, cy, w - 40, row_h, xport::TextStyle::Body, kText);
		cy += row_h;
		renderer.Text(Format(L"Left stick  %+.2f, %+.2f      Right stick  %+.2f, %+.2f",
			reading.LeftThumbstickX, reading.LeftThumbstickY, reading.RightThumbstickX, reading.RightThumbstickY),
			pad_x, cy, w - 40, row_h, xport::TextStyle::Body, kText);
		cy += row_h;
		renderer.Text(L"PS button → hold Menu + View (Guide is reserved by Xbox)",
			pad_x, cy, w - 40, row_h, xport::TextStyle::Small, kMuted);
	}

	void DrawNetworkPanel(float x, float y, float w, float h)
	{
		renderer.FillRect(x, y, w, h, kPanel);
		const float pad_x = x + 20;
		float cy = y + 16;
		renderer.Text(L"LAN discovery probe", pad_x, cy, w - 40, 34, xport::TextStyle::Heading, kText);
		cy += 40;
		renderer.Text(L"Broadcasts the PS4/PS5 discovery request (UDP 987 / 9302).",
			pad_x, cy, w - 40, 26, xport::TextStyle::Small, kMuted);
		cy += 28;
		renderer.Text(Widen(probe.Status()), pad_x, cy, w - 40, 28, xport::TextStyle::Body, probe.Running() ? kActive : kText);
		cy += 38;

		for(const auto &host : probe.Hosts())
		{
			if(cy + 56 > y + h)
				break;
			renderer.StrokeRect(pad_x - 6, cy - 2, w - 28, 52, kAccent, 1.5f);
			renderer.Text(Widen(host.name.empty() ? std::string("(unnamed)") : host.name), pad_x + 4, cy, w - 60, 26,
				xport::TextStyle::Body, kText);
			renderer.Text(Widen(host.type + "  ·  " + host.address + "  ·  " + host.state), pad_x + 4, cy + 24, w - 60, 24,
				xport::TextStyle::Small, kMuted);
			cy += 60;
		}

		renderer.Text(Format(L"GPU: %ls", renderer.AdapterDescription().c_str()), pad_x, y + h - 30, w - 40, 24,
			xport::TextStyle::Small, kMuted);
	}

	void DrawLogPanel(float x, float y, float w, float h)
	{
		renderer.FillRect(x, y, w, h, kPanel);
		renderer.Text(L"Log", x + 20, y + 8, 200, 28, xport::TextStyle::Body, kText);
		float cy = y + 38;
		for(const auto &line : xport::log::Recent(6))
		{
			renderer.Text(Widen(line), x + 20, cy, w - 40, 22, xport::TextStyle::Small, kMuted);
			cy += 22;
		}
	}

	xport::Renderer renderer;
	xport::NetProbe probe;
	bool closed = false;
	bool visible = true;
	Gamepad pad{nullptr};
	GamepadReading reading{};
	uint32_t pad_count = 0;
	ULONGLONG exit_hold_start = 0;
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	winrt::init_apartment();
	CoreApplication::Run(make<App>());
	return 0;
}
