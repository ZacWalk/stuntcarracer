// main.cpp — binds the game to libdesktop's ui application and frame tree.

#include "game.h"
#include "render.software.h"

#include "libdesktop/ui/elements.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <stdexcept>
#include <thread>

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
	bool g_minimized = false;

	// The software game renderer owns the pixels; this element only uploads the current
	// surface to the UI renderer and stretches it over the content below the menu bar.
	class game_surface_element final : public ui::element
	{
		SoftwareRenderer& _renderer;
		mutable render::texture_ptr _texture;
		mutable core::sizei _texture_extent;

	public:
		explicit game_surface_element(SoftwareRenderer& renderer) :
			ui::element(ui::element_options(ui::flex_item::grow | ui::flex_item::stretch)), _renderer(renderer)
		{
			padding = {0, 0};
			margin = {0, 0};
		}

		core::sizei measure(render::measure_context&, const int) const override
		{
			// The column flex layout gives this item all space left by the menu bar.
			return {};
		}

		void render(render::draw_context& dc, const core::pointi element_offset) const override
		{
			const auto surface = _renderer.GetSurface();
			if (!surface || surface->empty() || bounds.is_empty()) return;

			if (!_texture || _texture_extent != surface->dimensions())
			{
				_texture = dc.create_texture();
				_texture_extent = {};
			}

			if (_texture)
			{
				if (_texture->update(surface) == render::texture_update_result::failed)
				{
					_texture.reset();
				}
				else
				{
					_texture_extent = surface->dimensions();
				}
			}

			if (_texture)
				dc.draw_texture(_texture, bounds.offset(element_offset), 1.0f, render::texture_sampler::point);
		}

		void dispatch_event(const ui::element_event& event) override
		{
			if (event.type == ui::element_event_type::free_graphics_resources)
			{
				_texture.reset();
				_texture_extent = {};
			}
		}
	};

	class game_app final : public ui::app, public ui::element_host,
	                       public std::enable_shared_from_this<game_app>
	{
		platform::setting_file_ptr _settings;
		ui::frame_ptr _frame;
		ui::elements_ptr _root;
		std::shared_ptr<game_surface_element> _surface;
		std::vector<ui::command_ptr> _menus;
		bool _window_active = true;
		bool _window_focused = true;

		void update_input_active()
		{
			GameSetInputActive(_window_active && _window_focused);
		}

	public:
		bool load_settings(const platform::setting_file_ptr& store) override
		{
			_settings = store;
			return true;
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

		int run_console_mode() override
		{
			return AppRunTests();
		}

		bool wants_gpu() const override
		{
			return false;
		}

		bool key_down(const char32_t key, const platform::key_state keys) override
		{
			const auto is_driving_key = key == platform::keys::LEFT || key == platform::keys::RIGHT ||
				key == platform::keys::UP || key == platform::keys::DOWN ||
				key == platform::keys::CONTROL;
			const auto is_direct_game_key = (key >= '1' && key <= '8') ||
				key == platform::keys::SPACE || key == 'S' || key == 's' || is_driving_key;
			const auto is_menu_game_key = key == platform::keys::ESCAPE || key == platform::keys::F5 ||
				key == platform::keys::F6 || key == platform::keys::F7 || key == platform::keys::F9 ||
				key == platform::keys::F10 || key == 'P' || key == 'O' || key == 'R' || key == 'Z';

			// Alt+F4, Alt+Space and other system combinations belong to the native window.
			if (keys.alt) return false;

			// Repeats keep held driving state alive, but must not toggle screens or replay menus.
			if (keys.repeat)
			{
				if (is_driving_key) AppHandleKeyDown(key);
				return is_direct_game_key || is_menu_game_key;
			}

			if (ui::dispatch_accelerator(_menus, key, keys)) return true;
			if (is_direct_game_key)
			{
				AppHandleKeyDown(key);
				return true;
			}
			return is_menu_game_key;
		}

		bool text_input(std::string_view) override
		{
			return false;
		}

		ui::focus_mode focus_mode() const override
		{
			return ui::focus_mode::none;
		}

		void track_menu(const ui::frame_ptr& parent, const core::recti bounds,
		                const std::vector<ui::command_ptr>& commands) override
		{
			if (parent) parent->track_menu(bounds, commands);
		}

		void activate(const bool active) override
		{
			_window_active = active;
			update_input_active();
		}

		void focus_changed(const bool has_focus, const ui::control_ptr&) override
		{
			_window_focused = has_focus;
			update_input_active();
		}

		bool init(std::string_view) override
		{
			if (!_settings) _settings = platform::settings();
			const bool placement_restored = platform::read_placement(
				_settings, platform::placement_setting_name()).has_value();
			if (!AppInit()) return false;

			_surface = std::make_shared<game_surface_element>(g_renderer);
			_menus = BuildGameMenu();
			auto menu_bar = ui::make_menu_bar(_menus);
			_root = std::make_shared<ui::elements>(std::vector<ui::element_ptr>{menu_bar, _surface});
			_root->padding = {0, 0};
			_root->margin = {0, 0};
			_root->flex_container.direction = ui::flex_direction::column;
			_root->flex_container.wrap = ui::flex_wrap::no_wrap;
			_root->flex_container.align_items = ui::flex_align::stretch;

			ui::frame_style style;
			style.drawn_caption = false;
			style.hardware_accelerated = false;
			style.client_size = {WINDOW_WIDTH, WINDOW_HEIGHT};
			style.minimum_client = {320, 240};
			style.resizable = true;
			style.colors = {
				ui::style::colors.view_background,
				ui::style::colors.view_text,
				ui::style::colors.view_selected_background
			};

			const auto self = shared_from_this();
			const auto app_self = std::static_pointer_cast<ui::app>(self);
			const auto host_self = std::static_pointer_cast<ui::element_host>(self);
			const auto frame_host_self = std::static_pointer_cast<ui::frame_host>(host_self);
			_frame = ui::create_app_frame(app_self, _settings, frame_host_self, style);
			if (!_frame) return false;
			g_frame = _frame;
			_frame->window_text(GameWindowTitle(g_soundAvailable));
			if (!placement_restored)
			{
				// The process is DPI aware: scale the 640x480 window so it comes
				// up the same physical size on every monitor.
				const auto screen = platform::primary_screen_size();
				_frame->window_bounds(GameWindowBounds(screen, _frame->scale_factor()), true);
			}
			_frame->layout();
			return true;
		}

		bool owns_loop() const override
		{
			return true;
		}

		int main_loop() override
		{
			AppRun();
			return 0;
		}

		void exit() override
		{
			GameSetInputActive(false);
			_frame.reset();
			g_frame.reset();
		}

		const ui::frame_ptr frame() const override
		{
			return _frame ? _frame : ui::no_frame();
		}

		const ui::frame_ptr owner() override
		{
			return frame();
		}

		void invoke(commands) override
		{
		}

		bool is_command_checked(commands) override
		{
			return false;
		}

		void track_menu(const core::recti bounds, const std::vector<ui::command_ptr>& commands) override
		{
			frame()->track_menu(bounds, commands);
		}

		void controller_changed() override
		{
			frame()->invalidate();
		}

		ui::controller_ptr controller_from_location(const core::pointi loc,
		                                             ui::hit_test_context& ctx) override
		{
			const auto host = std::static_pointer_cast<ui::element_host>(shared_from_this());
			return _root ? _root->controller_from_location(host, loc, {}, ctx) : nullptr;
		}

		void invalidate_element(const ui::element_ptr& element) override
		{
			frame()->invalidate(element ? element->invalidate_bounds() : core::recti{});
		}

		void invalidate(const ui::host_invalid what) override
		{
			if (what && ui::host_invalid::layout) frame()->layout();
			if (what && ui::host_invalid::redraw) frame()->invalidate();
		}

		void on_window_layout(render::measure_context& mc, const core::sizei extent,
		                      const bool is_minimized) override
		{
			g_minimized = is_minimized || extent.is_empty();
			if (g_minimized || !_root) return;

			_root->layout(mc, core::recti(0, 0, extent.cx, extent.cy));
			const auto content = _surface->bounds;
			if (!content.is_empty()) AppHandleFrameSize(content.width(), content.height());
		}

		void on_window_paint(render::draw_context& dc) override
		{
			dc.clear(core::color(dc.colors.background, dc.colors.alpha));
			if (_root) _root->render(dc, {});
		}

		void visit_elements(const std::function<void(const ui::element_ptr&, core::pointi)>& visit) override
		{
			if (_root) visit(_root, {});
		}
	};
}

void GameClose()
{
	if (g_frame) g_frame->close();
}

void GamePresent()
{
	if (g_frame)
	{
		g_frame->invalidate();
		g_frame->redraw_now();
	}
}

bool GameIsMinimized()
{
	return g_minimized;
}

int main(int argc, char** argv)
{
	core::set_app_name("stuntcarracer");
	auto app = std::make_shared<game_app>();
	return platform::run(app, argc, argv);
}
