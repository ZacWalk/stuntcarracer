// main.cpp — binds the game to the shared platform layer: window callbacks,
// input translation, and the game's own frame loop.

#include "game.h"

#include <cstdarg>
#include <cstdio>
#include <memory>

std::wstring format(const std::wstring_view fmt, ...)
{
	wchar_t buf[512];
	va_list args;
	va_start(args, fmt);
	vswprintf_s(buf, std::size(buf), fmt.data(), args);
	va_end(args);
	buf[std::size(buf) - 1] = L'\0';
	return buf;
}

namespace
{
	pf::window_frame_ptr g_frame;
	bool g_minimized = false;

	struct game_reactor final : pf::frame_reactor
	{
		uint32_t handle_message(pf::window_frame_ptr, const pf::message_type message,
		                        const pf::message_params&) override
		{
			if (message == pf::message_type::erase_background)
				return 1; // the frame loop paints every pixel

			if (message == pf::message_type::kill_focus)
				AppResetInput();

			return 0;
		}

		uint32_t handle_keyboard(pf::window_frame_ptr, const pf::keyboard_message_type message,
		                         const pf::keyboard_params& params) override
		{
			if (message == pf::keyboard_message_type::key_down)
			{
				// Auto-repeat is ignored except for the driving keys, whose
				// repeats the original relies on to keep the input latched.
				const bool driving = params.vk == pf::platform_key::Left ||
					params.vk == pf::platform_key::Right ||
					params.vk == pf::platform_key::Up ||
					params.vk == pf::platform_key::Down ||
					params.vk == pf::platform_key::Control;

				if (!params.repeat || driving)
					AppHandleKeyDown(params.vk);
			}
			else if (message == pf::keyboard_message_type::key_up)
			{
				AppHandleKeyUp(params.vk);
			}

			return 0;
		}

		void handle_paint(pf::window_frame_ptr&, pf::draw_context&) override
		{
		}

		void handle_size(pf::window_frame_ptr&, const pf::isize extent, pf::measure_context&) override
		{
			g_minimized = extent.cx <= 0 || extent.cy <= 0;
			if (!g_minimized)
				AppHandleFrameSize(extent.cx, extent.cy);
		}
	};
}

void GameClose()
{
	if (g_frame) g_frame->close();
}

void GamePresent(const uint32_t* pixels, const int cx, const int cy)
{
	if (g_frame) g_frame->present_pixels(pixels, cx, cy);
}

void GameSetMenu(std::vector<pf::menu_command> menu_def)
{
	if (g_frame) g_frame->set_menu(std::move(menu_def));
}

bool GameIsMinimized()
{
	return g_minimized;
}

app_init_result app_init(const pf::window_frame_ptr& main_frame,
                         const std::span<const std::string_view> params)
{
	for (const auto& param : params)
	{
		if (pf::icmp(param, "-test") == 0 || pf::icmp(param, "/test") == 0)
		{
			pf::attach_console();
			return {false, AppRunTests()};
		}
	}

	pf::config_set_app_name("stuntcarracer");

	g_frame = main_frame;
	main_frame->set_reactor(std::make_shared<game_reactor>());

	if (!AppInit())
		return {false, 1};

	main_frame->set_text("Stunt Car Racer");

	const auto screen = pf::platform_screen_size();
	const int left = std::max(0, (screen.cx - WINDOW_WIDTH) / 2);
	const int top = std::max(0, (screen.cy - WINDOW_HEIGHT) / 2);
	main_frame->move_window({left, top, left + WINDOW_WIDTH, top + WINDOW_HEIGHT});

	app_init_result result;
	result.main_loop = []
	{
		AppRun();
		pf::sound_shutdown();
		return 0;
	};
	return result;
}

void app_idle()
{
}

void app_destroy()
{
	g_frame.reset();
}
