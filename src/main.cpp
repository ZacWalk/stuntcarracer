// main.cpp — runs the game in a libdesktop ui::shell: its window and menu bar, its fixed-step loop and the
// keys the steps read, and a canvas the scene is drawn on.

#include "game.h"

#include "libdesktop/ui/shell.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <stdexcept>

std::wstring format(const std::wstring_view fmt, ...)
{
	wchar_t buf[512];
	const std::wstring terminated(fmt);
	va_list args;
	va_start(args, fmt);
	const int written = std::vswprintf(buf, std::size(buf), terminated.c_str(), args);
	va_end(args);
	if (written < 0 || static_cast<size_t>(written) >= std::size(buf))
		throw std::runtime_error("Game text exceeds its formatting buffer.");
	return buf;
}

namespace
{
	ui::frame_ptr g_frame;

	class game_app final : public ui::shell
	{
	public:
		game_app() : shell(make_options()) {}

		static ui::shell_options make_options()
		{
			ui::shell_options options;
			options.title = GameWindowTitle(true);
			options.client_size = {WINDOW_WIDTH, WINDOW_HEIGHT};
			options.minimum_client = {320, 240};
			options.gpu = true;
			options.steps_per_second = AppStepsPerSecond();
			return options;
		}

		bool parse_command_line(const std::string_view command_line) override
		{
			for (const auto part : core::split(command_line, true))
			{
				if (core::icmp(part, "-test") == 0 || core::icmp(part, "/test") == 0)
				{
					return true;
				}
			}
			return false;
		}

		int run_console_mode() override { return AppRunTests(); }

		std::vector<ui::command_ptr> make_menus() override { return BuildGameMenu(); }

		ui::element_ptr make_content() override
		{
			return std::make_shared<ui::canvas_element>([](render::draw_context& dc, const core::recti bounds)
			{ AppDraw(dc, bounds); });
		}

		bool start(std::string_view) override
		{
			if (!AppInit()) return false;
			g_frame = frame();
			frame()->window_text(GameWindowTitle(g_soundAvailable));
			return true;
		}

		void step(double) override
		{
			// A tap between two steps still drives for one.
			const auto held = [this](const char32_t key)
			{
				return keys().held(key) || keys().pressed(key);
			};
			AppStep({held(platform::keys::LEFT), held(platform::keys::RIGHT), held(platform::keys::UP),
			         held(platform::keys::DOWN), held(platform::keys::CONTROL)});

			// The Speed menu changes the rate between steps.
			step_rate(AppStepsPerSecond());
		}

		bool key(const char32_t key, const platform::key_state keys) override
		{
			const auto is_driving_key = key == platform::keys::LEFT || key == platform::keys::RIGHT ||
			                            key == platform::keys::UP || key == platform::keys::DOWN ||
			                            key == platform::keys::CONTROL;
			const auto is_direct_game_key = (key >= '1' && key <= '8') || key == platform::keys::SPACE || key == 'S' ||
			                                key == 's' || is_driving_key;
			// The menus' keys reach here while their commands are disabled, and are still the game's.
			const auto is_menu_game_key = key == platform::keys::ESCAPE || key == platform::keys::F5 ||
			                              key == platform::keys::F6 || key == platform::keys::F7 ||
			                              key == platform::keys::F9 || key == platform::keys::F10 || key == 'P' ||
			                              key == 'O' || key == 'R' || key == 'Z';

			// Alt+F4, Alt+Space and other system combinations belong to the native window.
			if (keys.alt) return false;

			// A held arrow keeps cycling tracks or scenery in the track menu, but a repeat must not toggle screens.
			if (keys.repeat)
			{
				if (is_driving_key) AppHandleKeyDown(key);
				return is_direct_game_key || is_menu_game_key;
			}

			if (is_direct_game_key)
			{
				AppHandleKeyDown(key);
				return true;
			}
			return is_menu_game_key;
		}

		void minimized() override { AppIdleMinimized(); }

		void stop() override
		{
			g_frame.reset();
			AppShutdown();
		}
	};
}

void GameClose()
{
	if (g_frame) g_frame->close();
}

int main(int argc, char** argv)
{
	core::set_app_name("stuntcarracer");
	return platform::run(std::make_shared<game_app>(), argc, argv);
}
