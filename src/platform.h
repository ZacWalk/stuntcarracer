#pragma once

//
// platform.h — Platform-independent types, constants, and API declarations.
//
// This header must NOT include any OS-specific headers (windows.h, etc.).
// A platform implementation (e.g. platform.win.cpp) supplies the bodies.
//

// Standard C++ headers
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

constexpr int32_t AMIGA_PAL_HZ = 3546895;
constexpr int WINDOW_WIDTH = 640;
constexpr int WINDOW_HEIGHT = 480;

// 
// Color macros (ARGB format, platform-independent)
// 
constexpr uint32_t D3DCOLOR_ARGB(const uint32_t a, const uint32_t r, const uint32_t g, const uint32_t b)
{
	return (a & 0xff) << 24 | (r & 0xff) << 16 | (g & 0xff) << 8 | b & 0xff;
}

constexpr uint32_t XRGB(const uint32_t r, const uint32_t g, const uint32_t b)
{
	return D3DCOLOR_ARGB(0xff, r, g, b);
}

// 
// Platform-independent types (replacing Windows-specific types)
// 

// Palette color (replaces PALETTEENTRY)
struct PaletteColor
{
	uint8_t r, g, b;
};

// 2D integer point (replaces POINT)
struct Point2D
{
	int32_t x, y;
};

// 
// Platform key codes
// Values match Windows VK_ codes; other platform layers map native
// keycodes to these values.
// 
namespace PlatformKey
{
	constexpr unsigned int Left = 0x25;
	constexpr unsigned int Up = 0x26;
	constexpr unsigned int Right = 0x27;
	constexpr unsigned int Down = 0x28;
	constexpr unsigned int Control = 0x11;
	constexpr unsigned int Space = 0x20;
	constexpr unsigned int Escape = 0x1B;
	constexpr unsigned int F5 = 0x74;
	constexpr unsigned int F6 = 0x75;
	constexpr unsigned int F7 = 0x76;
	constexpr unsigned int F9 = 0x78;
	constexpr unsigned int F10 = 0x79;
}

// 
// Platform Sound Abstraction
// 
struct PlatformSoundBuffer;

enum SoundPan : int
{
	PAN_LEFT = -10000,
	PAN_CENTER = 0,
	PAN_RIGHT = 10000
};

void PlatformDeleteSoundBuffer(PlatformSoundBuffer*& buf);

// 
// Menu definition types
// 
struct MenuCommand
{
	std::wstring text; // display text (empty = separator)
	int id = 0; // command ID (0 for submenus/separators)
	std::function<void()> action; // invoked on click
	std::function<bool()> isEnabled; // returns true if enabled
	std::function<bool()> isChecked; // returns true if checked
	std::vector<MenuCommand> children; // non-empty = submenu

	MenuCommand() = default;

	// Leaf item with action + optional enabled/checked callbacks
	MenuCommand(std::wstring t, const int cmdId,
	            std::function<void()> act,
	            std::function<bool()> en = nullptr,
	            std::function<bool()> chk = nullptr)
		: text(std::move(t)), id(cmdId), action(std::move(act)),
		  isEnabled(std::move(en)), isChecked(std::move(chk))
	{
	}
};

bool PlatformSoundInit();
void PlatformSoundShutdown();
bool PlatformEvents();
void PlatformPresentFrame(const uint32_t* pixels, int cx, int cy);
void PlatformClose();
void PlatformSetMenu(std::vector<MenuCommand> menuDef);

// App methods called by platform layer
bool AppInit();
void AppRun();
void AppHandleFrameSize(int cx, int cy);
void AppHandleKeyDown(uint32_t nChar);
void AppHandleKeyUp(uint32_t nChar);
void AppResetInput();

// Headless self-test entry. Returns process exit code (0 = success).
int AppRunTests();

// Buffer management
PlatformSoundBuffer* PlatformCreateSoundBuffer(std::wstring_view resourceName);

// Playback
void PlatformSoundPlay(PlatformSoundBuffer* buf, bool loop = false);
void PlatformSoundStop(PlatformSoundBuffer* buf);

// Properties  (volume is in Amiga units 0-64; platform converts internally)
void PlatformSoundSetFrequency(PlatformSoundBuffer* buf, uint32_t freq);
void PlatformSoundSetVolume(PlatformSoundBuffer* buf, int32_t amigaVolume);
void PlatformSoundSetPan(PlatformSoundBuffer* buf, SoundPan pan);

// Position
struct SoundPosition
{
	bool valid;
	uint32_t pos;
};

SoundPosition PlatformSoundGetPosition(PlatformSoundBuffer* buf);
void PlatformSoundSetPosition(PlatformSoundBuffer* buf, uint32_t pos);

// 
// Platform Timer
// 
void PlatformInitTimer();
double PlatformGetTime();
void PlatformSleep(int milliseconds);

// 
// Platform Resource Loading
// 
void* PlatformLoadResource(std::wstring_view name, std::wstring_view type);

// 
// Platform UI
// 
void PlatformShowError(std::wstring_view message, std::wstring_view title);


// 
// Platform Bitmap Resource Loading
// 
// Load a BMP from resources, returning ARGB pixels. Caller must delete[] pixels.
struct BitmapData
{
	int width;
	int height;
	std::vector<uint32_t> pixels;
};

std::optional<BitmapData> PlatformLoadBitmapResource(std::wstring_view resName);


std::wstring format(std::wstring_view fmt, ...);
