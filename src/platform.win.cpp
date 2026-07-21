//
// platform.win.cpp — Windows platform layer
//
// Implements platform API (platform.h) for Win32:
//   - WinMain entry point, window creation, message loop
//   - Timer (QueryPerformanceCounter)
//   - Sound (DirectSound 8)
//   - Resource loading (FindResource / LoadResource)
//   - Menu / accelerator management
//

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <functional>
#include <vector>
#include <string>

#include "game.h"
#include "platform.h"
#include "resource.h"

using namespace std::string_view_literals;

// ── Globals ────────────────────────────────────────────────────────────────────

static HWND g_hWnd = nullptr;
static LARGE_INTEGER g_perfFreq;
static LARGE_INTEGER g_perfStart;
static IDirectSound8* s_ds = nullptr;
static HMENU g_hMenu = nullptr;
static HACCEL g_hAccel = nullptr;
static std::vector<MenuCommand> g_menuDef;
static bool g_windowMinimized = false;

// ── Timer ──────────────────────────────────────────────────────────────────────

void PlatformInitTimer()
{
	QueryPerformanceFrequency(&g_perfFreq);
	QueryPerformanceCounter(&g_perfStart);
}

double PlatformGetTime()
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return static_cast<double>(now.QuadPart - g_perfStart.QuadPart)
		/ static_cast<double>(g_perfFreq.QuadPart);
}

void PlatformSleep(const int milliseconds)
{
	Sleep(static_cast<DWORD>(milliseconds));
}

// ── Resource Loading ───────────────────────────────────────────────────────────

void* PlatformLoadResource(const std::wstring_view name, const std::wstring_view type)
{
	LPCWSTR resType = type.data();
	if (type == L"BITMAP"sv)
		resType = RT_BITMAP;

	const HRSRC hResInfo = FindResource(nullptr, name.data(), resType);
	if (!hResInfo) return nullptr;

	const HGLOBAL hResData = LoadResource(nullptr, hResInfo);
	if (!hResData) return nullptr;

	return LockResource(hResData);
}

std::optional<BitmapData> PlatformLoadBitmapResource(const std::wstring_view resName)
{
	const auto pData = static_cast<const uint8_t*>(PlatformLoadResource(resName, L"BITMAP"));
	if (!pData) return std::nullopt;

	const auto bih = reinterpret_cast<const BITMAPINFOHEADER*>(pData);
	const int w = bih->biWidth;
	const int h = abs(bih->biHeight);
	const bool topDown = bih->biHeight < 0;
	const int bpp = bih->biBitCount;

	int paletteSize = 0;
	if (bpp <= 8)
		paletteSize = (bih->biClrUsed ? bih->biClrUsed : 1 << bpp) * static_cast<int>(sizeof(RGBQUAD));

	const uint8_t* pixelData = pData + bih->biSize + paletteSize;
	const RGBQUAD* palette = bpp <= 8 ? reinterpret_cast<const RGBQUAD*>(pData + bih->biSize) : nullptr;

	std::vector<uint32_t> pixels(w * h, 0);
	const int srcStride = (w * bpp + 31) / 32 * 4;

	for (int y = 0; y < h; y++)
	{
		const int srcY = topDown ? y : h - 1 - y;
		const uint8_t* srcRow = pixelData + srcY * srcStride;

		for (int x = 0; x < w; x++)
		{
			uint32_t c = 0;
			if (bpp == 24)
			{
				c = XRGB(srcRow[x * 3 + 2], srcRow[x * 3 + 1], srcRow[x * 3 + 0]);
			}
			else if (bpp == 32)
			{
				c = *reinterpret_cast<const uint32_t*>(srcRow + x * 4) | 0xFF000000;
			}
			else if (bpp == 8 && palette)
			{
				const auto& p = palette[srcRow[x]];
				c = XRGB(p.rgbRed, p.rgbGreen, p.rgbBlue);
			}
			else if (bpp == 4 && palette)
			{
				const uint8_t idx = x & 1 ? srcRow[x / 2] & 0x0F : srcRow[x / 2] >> 4;
				c = XRGB(palette[idx].rgbRed, palette[idx].rgbGreen, palette[idx].rgbBlue);
			}
			pixels[y * w + x] = c;
		}
	}

	return BitmapData{w, h, pixels};
}

// ── Utility ────────────────────────────────────────────────────────────────────

std::wstring format(std::wstring_view fmt, ...)
{
	wchar_t buf[512];
	va_list args;
	va_start(args, fmt);
	vswprintf_s(buf, _countof(buf), fmt.data(), args);
	va_end(args);
	buf[_countof(buf) - 1] = L'\0';
	return buf;
}

void PlatformShowError(const std::wstring_view message, const std::wstring_view title)
{
	MessageBox(nullptr, message.data(), title.data(), MB_OK);
}

// ── Sound — WAV resource helpers ───────────────────────────────────────────────

struct WAVResource
{
	const BYTE* data;
	size_t size;
};

static std::optional<WAVResource> GetWAVResource(const HMODULE hModule, const std::wstring_view name)
{
	const HRSRC hRes = FindResource(hModule, name.data(), L"WAVE");
	if (!hRes) return std::nullopt;

	const HGLOBAL hData = LoadResource(hModule, hRes);
	if (!hData) return std::nullopt;

	const auto data = static_cast<const BYTE*>(LockResource(hData));
	const DWORD size = SizeofResource(hModule, hRes);
	if (!data || size == 0) return std::nullopt;
	return WAVResource{data, size};
}

static DWORD ReadResourceDword(const BYTE* data)
{
	DWORD value;
	CopyMemory(&value, data, sizeof(value));
	return value;
}

static BOOL UnpackWAVChunk(const WAVResource resource, LPWAVEFORMATEX* ppFormat, LPBYTE* ppData,
	DWORD* pDataSize)
{
	if (ppFormat) *ppFormat = nullptr;
	if (ppData) *ppData = nullptr;
	if (pDataSize) *pDataSize = 0;

	if (resource.size < 12) return FALSE;
	const DWORD chunkID = ReadResourceDword(resource.data);
	const DWORD riffLength = ReadResourceDword(resource.data + 4);
	DWORD type = ReadResourceDword(resource.data + 8);

	if (chunkID != mmioFOURCC('R', 'I', 'F', 'F') || type != mmioFOURCC('W', 'A', 'V', 'E'))
		return FALSE;
	if (riffLength < 4 || riffLength > resource.size - 8) return FALSE;

	const size_t end = static_cast<size_t>(riffLength) + 8;
	size_t offset = 12;
	while (offset + 8 <= end)
	{
		type = ReadResourceDword(resource.data + offset);
		const DWORD length = ReadResourceDword(resource.data + offset + 4);
		offset += 8;
		if (length > end - offset) return FALSE;
		const BYTE* chunkData = resource.data + offset;

		if (type == mmioFOURCC('f', 'm', 't', ' '))
		{
			if (ppFormat && !*ppFormat)
			{
				if (length < sizeof(WAVEFORMAT))
					return FALSE;
				*ppFormat = reinterpret_cast<LPWAVEFORMATEX>(const_cast<BYTE*>(chunkData));
				if ((!ppData || *ppData) && (!pDataSize || *pDataSize))
					return TRUE;
			}
		}
		else if (type == mmioFOURCC('d', 'a', 't', 'a'))
		{
			if ((ppData && !*ppData) || (pDataSize && !*pDataSize))
			{
				if (ppData) *ppData = const_cast<LPBYTE>(chunkData);
				if (pDataSize) *pDataSize = length;
				if (!ppFormat || *ppFormat)
					return TRUE;
			}
		}

		const size_t paddedLength = static_cast<size_t>(length) + (length & 1u);
		if (paddedLength > end - offset) return FALSE;
		offset += paddedLength;
	}

	return FALSE;
}

static BOOL WriteWAVToBuffer(IDirectSoundBuffer* pBuf, const LPBYTE pWaveData, const DWORD dwBytes)
{
	if (!pBuf || !pWaveData || !dwBytes) return FALSE;

	LPVOID pAudio1, pAudio2;
	DWORD size1, size2;

	if (pBuf->Lock(0, dwBytes, &pAudio1, &size1, &pAudio2, &size2, 0) != DS_OK)
		return FALSE;

	CopyMemory(pAudio1, pWaveData, size1);
	if (size2)
		CopyMemory(pAudio2, pWaveData + size1, size2);

	pBuf->Unlock(pAudio1, size1, pAudio2, size2);
	return TRUE;
}

static IDirectSoundBuffer* CreateSoundBuffer(IDirectSound8* ds, const std::wstring_view name)
{
	const auto resource = GetWAVResource(nullptr, name);
	if (!resource) return nullptr;

	DSBUFFERDESC desc = {};
	desc.dwSize = sizeof(desc);
	desc.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME;

	LPBYTE pWaveData = nullptr;
	if (!UnpackWAVChunk(*resource, &desc.lpwfxFormat, &pWaveData, &desc.dwBufferBytes))
		return nullptr;

	IDirectSoundBuffer* pBuf = nullptr;
	if (ds->CreateSoundBuffer(&desc, &pBuf, nullptr) != DS_OK)
		return nullptr;

	if (!WriteWAVToBuffer(pBuf, pWaveData, desc.dwBufferBytes))
	{
		pBuf->Release();
		return nullptr;
	}
	return pBuf;
}

// ── Sound — Platform API ───────────────────────────────────────────────────────

static constexpr int32_t MAX_AMIGA_VOLUME = 64;

static int32_t AmigaVolumeToDirectX(int32_t amigaVolume)
{
	static bool initialized = false;
	static int32_t table[MAX_AMIGA_VOLUME + 1];

	if (!initialized)
	{
		table[0] = DSBVOLUME_MIN;
		for (int32_t i = 1; i <= MAX_AMIGA_VOLUME; i++)
			table[i] = static_cast<int32_t>(2000.0 * log10(static_cast<double>(i) / MAX_AMIGA_VOLUME));
		initialized = true;
	}

	if (amigaVolume < 0 || amigaVolume > MAX_AMIGA_VOLUME)
		amigaVolume = MAX_AMIGA_VOLUME;

	return table[amigaVolume];
}

bool PlatformSoundInit()
{
	if (DirectSoundCreate8(nullptr, &s_ds, nullptr) != DS_OK)
		return false;
	if (s_ds->SetCooperativeLevel(g_hWnd, DSSCL_NORMAL) != DS_OK)
	{
		s_ds->Release();
		s_ds = nullptr;
		return false;
	}
	return true;
}

void PlatformSoundShutdown()
{
	if (s_ds)
	{
		s_ds->Release();
		s_ds = nullptr;
	}
}

PlatformSoundBuffer* PlatformCreateSoundBuffer(const std::wstring_view resourceName)
{
	if (!s_ds) return nullptr;
	return reinterpret_cast<PlatformSoundBuffer*>(CreateSoundBuffer(s_ds, resourceName));
}

void PlatformDeleteSoundBuffer(PlatformSoundBuffer*& buf)
{
	if (buf)
	{
		reinterpret_cast<IDirectSoundBuffer*>(buf)->Release();
		buf = nullptr;
	}
}

void PlatformSoundPlay(PlatformSoundBuffer* buf, const bool loop)
{
	if (!buf) return;
	reinterpret_cast<IDirectSoundBuffer*>(buf)->Play(0, 0, loop ? DSBPLAY_LOOPING : 0);
}

void PlatformSoundStop(PlatformSoundBuffer* buf)
{
	if (!buf) return;
	reinterpret_cast<IDirectSoundBuffer*>(buf)->Stop();
}

void PlatformSoundSetFrequency(PlatformSoundBuffer* buf, const uint32_t freq)
{
	if (!buf) return;
	reinterpret_cast<IDirectSoundBuffer*>(buf)->SetFrequency(freq);
}

void PlatformSoundSetVolume(PlatformSoundBuffer* buf, const int32_t amigaVolume)
{
	if (!buf) return;
	reinterpret_cast<IDirectSoundBuffer*>(buf)->SetVolume(AmigaVolumeToDirectX(amigaVolume));
}

void PlatformSoundSetPan(PlatformSoundBuffer* buf, const SoundPan pan)
{
	if (!buf) return;
	reinterpret_cast<IDirectSoundBuffer*>(buf)->SetPan(pan);
}

SoundPosition PlatformSoundGetPosition(PlatformSoundBuffer* buf)
{
	if (!buf) return {false, 0};
	DWORD cursor = 0;
	const HRESULT hr = reinterpret_cast<IDirectSoundBuffer*>(buf)->GetCurrentPosition(&cursor, nullptr);
	return {hr == DS_OK, cursor};
}

void PlatformSoundSetPosition(PlatformSoundBuffer* buf, const uint32_t pos)
{
	if (!buf) return;
	reinterpret_cast<IDirectSoundBuffer*>(buf)->SetCurrentPosition(pos);
}

// ── Menu & Accelerators ────────────────────────────────────────────────────────

static HMENU BuildPopupMenu(const std::vector<MenuCommand>& items)
{
	const HMENU hMenu = CreatePopupMenu();
	for (auto& item : items)
	{
		if (item.text.empty() && item.children.empty())
			AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
		else if (!item.children.empty())
			AppendMenu(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildPopupMenu(item.children)), item.text.c_str());
		else
			AppendMenu(hMenu, MF_STRING, item.id, item.text.c_str());
	}
	return hMenu;
}

void PlatformSetMenu(std::vector<MenuCommand> menuDef)
{
	g_menuDef = std::move(menuDef);
	g_hMenu = CreateMenu();
	for (auto& top : g_menuDef)
	{
		if (!top.children.empty())
			AppendMenu(g_hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(BuildPopupMenu(top.children)), top.text.c_str());
		else
			AppendMenu(g_hMenu, MF_STRING, top.id, top.text.c_str());
	}
	SetMenu(g_hWnd, g_hMenu);
}

static HACCEL CreateGameAccelerators()
{
	ACCEL accel[] = {
		{FVIRTKEY, VK_F5, IDM_SHOW_STATS},
		{FVIRTKEY, VK_F6, IDM_PAUSE_PLAYER},
		{FVIRTKEY, VK_F7, IDM_PAUSE_OPPONENT},
		{FVIRTKEY, VK_F9, IDM_SPEED_INCREASE},
		{FVIRTKEY, VK_F10, IDM_SPEED_DECREASE},
		{FVIRTKEY, VK_ESCAPE, IDM_BACK_TO_MENU},
	};
	return CreateAcceleratorTable(accel, _countof(accel));
}

static void UpdateMenuCommandStates(const HMENU hMenu, const std::vector<MenuCommand>& items)
{
	for (auto& item : items)
	{
		if (!item.children.empty())
		{
			UpdateMenuCommandStates(hMenu, item.children);
			continue;
		}
		if (item.id == 0) continue;
		if (item.isEnabled)
			EnableMenuItem(hMenu, item.id, MF_BYCOMMAND | (item.isEnabled() ? MF_ENABLED : MF_GRAYED));
		if (item.isChecked)
			CheckMenuItem(hMenu, item.id, MF_BYCOMMAND | (item.isChecked() ? MF_CHECKED : MF_UNCHECKED));
	}
}

static bool DispatchMenuCommand(const std::vector<MenuCommand>& items, const WORD cmd)
{
	for (auto& item : items)
	{
		if (!item.children.empty())
		{
			if (DispatchMenuCommand(item.children, cmd))
				return true;
			continue;
		}
		if (item.id == cmd && item.action)
		{
			item.action();
			return true;
		}
	}
	return false;
}

// ── Window Procedure ───────────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(const HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_COMMAND:
		DispatchMenuCommand(g_menuDef, LOWORD(wParam));
		return 0;

	case WM_KEYDOWN:
		if ((lParam & (1LL << 30)) == 0 || wParam == VK_LEFT || wParam == VK_RIGHT ||
			wParam == VK_UP || wParam == VK_DOWN || wParam == VK_CONTROL)
			AppHandleKeyDown(static_cast<uint32_t>(wParam));
		return 0;

	case WM_KEYUP:
		AppHandleKeyUp(static_cast<uint32_t>(wParam));
		return 0;

	case WM_PAINT:
		{
			PAINTSTRUCT ps;
			BeginPaint(hWnd, &ps);
			EndPaint(hWnd, &ps);
		}
		return 0;

	case WM_SIZE:
		g_windowMinimized = wParam == SIZE_MINIMIZED;
		if (wParam != SIZE_MINIMIZED)
		{
			const int clientW = LOWORD(lParam);
			const int clientH = HIWORD(lParam);
			if (clientW > 0 && clientH > 0)
				AppHandleFrameSize(clientW, clientH);
		}
		return 0;

	case WM_ACTIVATEAPP:
		if (!wParam)
			AppResetInput();
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_CLOSE:
		DestroyWindow(hWnd);
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_INITMENUPOPUP:
		UpdateMenuCommandStates(reinterpret_cast<HMENU>(wParam), g_menuDef);
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// ── Entry Point & Main Loop ────────────────────────────────────────────────────

INT WINAPI WinMain(const HINSTANCE hInstance, HINSTANCE, LPSTR, const int nCmdShow)
{
	PlatformInitTimer();

	// Headless self-test mode: -test or /test on the command line.
	{
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
		bool testMode = false;
		if (argv)
		{
			for (int i = 1; i < argc; ++i)
			{
				if (lstrcmpiW(argv[i], L"-test") == 0 ||
					lstrcmpiW(argv[i], L"/test") == 0)
				{
					testMode = true;
					break;
				}
			}
			LocalFree(argv);
		}
		if (testMode)
		{
			// Attach to parent console (if launched from one) so log output is visible.
			if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())
			{
				FILE* f = nullptr;
				freopen_s(&f, "CONOUT$", "w", stderr);
				freopen_s(&f, "CONOUT$", "w", stdout);
			}
			return AppRunTests();
		}
	}

	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
	wc.lpszClassName = L"StuntCarRacerClass";
	wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));
	RegisterClassEx(&wc);

	RECT rc = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, true);

	g_hWnd = CreateWindow(L"StuntCarRacerClass", L"StuntCarRacer",
	                      WS_OVERLAPPEDWINDOW,
	                      CW_USEDEFAULT, CW_USEDEFAULT,
	                      rc.right - rc.left, rc.bottom - rc.top,
	                      nullptr, nullptr, hInstance, nullptr);
	if (!g_hWnd)
		return 1;

	if (!AppInit())
	{
		DestroyWindow(g_hWnd);
		return 1;
	}
	g_hAccel = CreateGameAccelerators();

	ShowWindow(g_hWnd, nCmdShow);
	UpdateWindow(g_hWnd);

	AppRun();

	PlatformSoundShutdown();
	if (g_hAccel) DestroyAcceleratorTable(g_hAccel);
	return 0;
}

void PlatformClose()
{
	PostMessage(g_hWnd, WM_CLOSE, 0, 0);
}

bool PlatformEvents()
{
	MSG msg = {};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
			return false;
		if (!TranslateAccelerator(g_hWnd, g_hAccel, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	if (g_windowMinimized)
	{
		AppResetInput();
		WaitMessage();
	}
	return true;
}

void PlatformPresentFrame(const uint32_t* pixels, const int cx, const int cy)
{
	const HDC hdc = GetDC(g_hWnd);

	BITMAPINFO bmi = {};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = cx;
	bmi.bmiHeader.biHeight = -cy; // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	SetDIBitsToDevice(hdc, 0, 0, cx, cy, 0, 0, 0, cy, pixels, &bmi, DIB_RGB_COLORS);

	ReleaseDC(g_hWnd, hdc);
}
