// Backdrop.cpp — Draws the sky/ground horizon and distant scenery objects.

#include <algorithm>

#include "platform.h"
#include "game.h"
#include "render.software.h"

static constexpr int32_t SKY_COLOUR = SCR_BASE_COLOUR + 7;
static constexpr int32_t GROUND_COLOUR = SCR_BASE_COLOUR + 13;

static constexpr int32_t MIN_SCENERY_TYPE = 0;
static constexpr int32_t MAX_SCENERY_TYPE = 4;

static int32_t current_scenery_type = MAX_SCENERY_TYPE;

// ─── ClipLine ───────────────────────────────────────────────────────────────

struct ClipLineResult
{
	bool on_screen;
	int32_t x1, y1, x2, y2;
};

static ClipLineResult ClipLine(int32_t x1, int32_t y1,
                               int32_t x2, int32_t y2,
                               const int32_t screen_width,
                               const int32_t screen_height)
{
	const int32_t max_x = screen_width - 1;
	const int32_t max_y = screen_height - 1;

	do
	{
		// clip x1
		if (x1 < 0)
		{
			if (x2 < 0) break;
			y1 -= static_cast<int32_t>(static_cast<double>(x1) * (y2 - static_cast<double>(y1)) /
				(x2 - static_cast<double>(x1)));
			x1 = 0;
		}
		else if (x1 > max_x)
		{
			if (x2 > max_x) break;
			y1 -= static_cast<int32_t>((x1 - static_cast<double>(max_x)) * (y2 - static_cast<double>(y1)) /
				(x2 - static_cast<double>(x1)));
			x1 = max_x;
		}

		// clip y1
		if (y1 < 0)
		{
			if (y2 < 0) break;
			x1 -= static_cast<int32_t>(static_cast<double>(y1) * (x2 - static_cast<double>(x1)) /
				(y2 - static_cast<double>(y1)));
			y1 = 0;
			if (x1 < 0 || x1 > max_x) break;
		}
		else if (y1 > max_y)
		{
			if (y2 > max_y) break;
			x1 -= static_cast<int32_t>((y1 - static_cast<double>(max_y)) * (x2 - static_cast<double>(x1)) /
				(y2 - static_cast<double>(y1)));
			y1 = max_y;
			if (x1 < 0 || x1 > max_x) break;
		}

		// clip x2
		if (x2 < 0)
		{
			y2 -= static_cast<int32_t>(static_cast<double>(x2) * (y1 - static_cast<double>(y2)) /
				(x1 - static_cast<double>(x2)));
			x2 = 0;
		}
		else if (x2 > max_x)
		{
			y2 -= static_cast<int32_t>((x2 - static_cast<double>(max_x)) * (y1 - static_cast<double>(y2)) /
				(x1 - static_cast<double>(x2)));
			x2 = max_x;
		}

		// clip y2
		if (y2 < 0)
		{
			x2 -= static_cast<int32_t>(static_cast<double>(y2) * (x1 - static_cast<double>(x2)) /
				(y1 - static_cast<double>(y2)));
			y2 = 0;
			if (x2 < 0 || x2 > max_x) break;
		}
		else if (y2 > max_y)
		{
			x2 -= static_cast<int32_t>((y2 - static_cast<double>(max_y)) * (x1 - static_cast<double>(x2)) /
				(y1 - static_cast<double>(y2)));
			y2 = max_y;
			if (x2 < 0 || x2 > max_x) break;
		}

		return {true, x1, y1, x2, y2};
	}
	while (false);

	return {false, x1, y1, x2, y2};
}

// ─── Scenery accessors ──────────────────────────────────────────────────────

void NextSceneryType(void)
{
	current_scenery_type++;
	if (current_scenery_type > MAX_SCENERY_TYPE)
		current_scenery_type = MIN_SCENERY_TYPE;
}

int32_t GetSceneryType(void)
{
	return current_scenery_type;
}

void SetSceneryType(const int32_t type)
{
	if (type >= MIN_SCENERY_TYPE && type <= MAX_SCENERY_TYPE)
		current_scenery_type = type;
}


static void DrawHorizon(SoftwareRenderer& r,
                        double viewpoint_y,
                        double viewpoint_x_angle,
                        double viewpoint_z_angle)
{
	bool upside_down = false;

	// Two co-ordinates defining the horizon line — large but finite world
	// extents so the perspective projection (with the focal length used below)
	// places the horizon near the visible centre of the screen.
	struct HorizonPoint
	{
		double x, y, z;
	};
	HorizonPoint plane[2] = {
		{-65536.0f, 0.0f, 65536.0f}, // left
		{65536.0f, 0.0f, 65536.0f}, // right
	};
	COORD_2D screen_coords[2];

	auto [screen_width, screen_height] = GetScreenDimensions(r);

	// Calculate y adjustment depending upon viewpoint_x_angle.
	// Needed because only two horizon points are used rather than four.
	// When the rotated z values are negative (viewpoint pitched past the zenith
	// or nadir) the resulting y values are negated so the y adjustment must
	// also change sign.
	const bool inverted_pitch =
		viewpoint_x_angle >= 0.5f * SCR_PI && viewpoint_x_angle < 1.5f * SCR_PI;
	// viewpoint_y is in render-space world units.
	double y_adjust = viewpoint_y;
	if (!inverted_pitch) y_adjust = -y_adjust;
	y_adjust *= 0.5f; // reduce using PC_FACTOR

	// Rotate two points about x/z axis and perform perspective projection
	const double sin_x = std::sin(viewpoint_x_angle);
	const double cos_x = std::cos(viewpoint_x_angle);
	const double sin_z = std::sin(viewpoint_z_angle);
	const double cos_z = std::cos(viewpoint_z_angle);
	const double focal = std::min(screen_height * 512.f / 480.f, screen_width * 512.f / 640.f);

	for (int32_t i = 0; i < 2; i++)
	{
		double x = plane[i].x;
		double y = plane[i].y + y_adjust;
		double z = plane[i].z;

		// rotate about x axis
		const double rot_y_x = y * cos_x - z * sin_x;
		const double rot_z = y * sin_x + z * cos_x;

		// rotate about z axis
		y = rot_y_x;
		const double rot_x = x * cos_z - y * sin_z;
		const double rot_y = x * sin_z + y * cos_z;

		// perspective projection — focal length tracks the smaller of the
		// height- and width-based reference scales so wider/taller windows
		// reveal more world (matching the 3D projection) instead of cropping.
		double zd = rot_z / focal;
		if (std::fabs(zd) < 1.0f) zd = (zd < 0.0f) ? -1.0f : 1.0f;

		screen_coords[i].x = static_cast<int32_t>(rot_x / zd) + screen_width / 2;
		screen_coords[i].y = static_cast<int32_t>(rot_y / zd) + screen_height / 2;
	}

	int32_t x1 = screen_coords[0].x;
	int32_t y1 = screen_coords[0].y;
	int32_t x2 = screen_coords[1].x;
	int32_t y2 = screen_coords[1].y;

	// Draw required rectangular sections
	constexpr int32_t min_x = 0;
	int32_t max_x = screen_width - 1;
	const int32_t max_y = screen_height - 1;
	int32_t ytop = 0, ybottom = 0, colour_index = 0;

	if (x1 > x2 || (x1 == x2 && y1 > y2))
		upside_down = !upside_down;

	auto clip = ClipLine(x1, y1, x2, y2, screen_width, screen_height);
	bool on_screen = clip.on_screen;
	x1 = clip.x1;
	y1 = clip.y1;
	x2 = clip.x2;
	y2 = clip.y2;

	// Get smallest and largest y
	int32_t xs, ys, xl, yl;
	if (y2 < y1)
	{
		xs = x2;
		ys = y2;
		xl = x1;
		yl = y1;
	}
	else
	{
		xs = x1;
		ys = y1;
		xl = x2;
		yl = y2;
	}

	// Special check for when line has been clipped to a single
	// pixel (i.e. one of the four corners of the screen)
	if (xs == xl && ys == yl)
		on_screen = false;


	if (on_screen)
	{
		// Draw top rectangle
		bool draw = false;

		if (!upside_down)
		{
			colour_index = SKY_COLOUR;

			if (ys != yl)
			{
				ytop = 0;
				ybottom = xl > min_x && xl < max_x ? yl : yl - 1;
				draw = true;
			}
			else if (ys > 0)
			{
				ytop = 0;
				ybottom = (xs == min_x && xl == max_x) ||
				          (xs == max_x && xl == min_x)
					          ? ys - 1
					          : ys;
				draw = true;
			}
		}
		else
		{
			colour_index = GROUND_COLOUR;

			if (ys != yl)
			{
				if (ys > 0)
				{
					ytop = 0;
					ybottom = ys - 1;
					draw = true;
				}
			}
			else if ((xs == min_x && xl == max_x) ||
				(xs == max_x && xl == min_x))
			{
				ytop = 0;
				ybottom = ys;
				draw = true;
			}
		}

		if (draw)
			r.FillRect(min_x, ytop, max_x, ybottom, SCRGB(colour_index));

		// Draw bottom rectangle — fill the area that the top rectangle missed
		colour_index = colour_index == SKY_COLOUR ? GROUND_COLOUR : SKY_COLOUR;

		if (draw)
		{
			if (ybottom < max_y)
				r.FillRect(min_x, ybottom + 1, max_x, max_y, SCRGB(colour_index));
		}
		else
		{
			r.FillRect(min_x, 0, max_x, max_y, SCRGB(colour_index));
		}
	}
	else // horizon line is off screen
	{
		const bool off_top = y1 <= 0 && y2 <= 0;
		const bool off_bottom = y1 >= max_y && y2 >= max_y;
		const bool off_left = x1 <= min_x && x2 <= min_x;
		const bool off_right = x1 >= max_x && x2 >= max_x;

		if (off_top)
			colour_index = upside_down ? SKY_COLOUR : GROUND_COLOUR;
		else if (off_bottom)
			colour_index = upside_down ? GROUND_COLOUR : SKY_COLOUR;
		else if (off_left)
			colour_index = y1 > y2 ? GROUND_COLOUR : SKY_COLOUR;
		else if (off_right)
			colour_index = y1 > y2 ? SKY_COLOUR : GROUND_COLOUR;

		r.FillRect(min_x, 0, max_x, max_y, SCRGB(colour_index));
	}


	// draw sloping ground section
	if (on_screen && y1 != y2)
	{
		int32_t sides = 0;
		Point2D points[MAX_POLY_SIDES];

		if (y1 < y2)
		{
			// ground on left area of screen

			// Adjust to include last pixel in polygon
			++y2;

			if (x1 > min_x)
			{
				points[sides].x = min_x;
				points[sides].y = y1;
				sides++;
			}

			points[sides].x = x1;
			points[sides].y = y1;
			sides++;
			points[sides].x = x2;
			points[sides].y = y2;
			sides++;

			if (x2 > min_x)
			{
				points[sides].x = min_x;
				points[sides].y = y2;
				sides++;
			}
		}
		else
		{
			// ground on right area of screen

			// Adjust to include last pixel in polygon
			++y1;
			++max_x;

			if (x1 < max_x)
			{
				points[sides].x = max_x;
				points[sides].y = y1;
				sides++;
			}

			points[sides].x = x1;
			points[sides].y = y1;
			sides++;
			points[sides].x = x2;
			points[sides].y = y2;
			sides++;

			if (x2 < max_x)
			{
				points[sides].x = max_x;
				points[sides].y = y2;
				sides++;
			}
		}

		if (sides >= 3)
			r.DrawScreenTriangleFan(points, sides, SCRGB(GROUND_COLOUR));
	}
}


// ─── Scenery data ───────────────────────────────────────────────────────────

static constexpr int32_t NUM_SCENERY_OBJECTS = 32;
static constexpr int32_t MAX_SCENERY_COORDS = 7;
static constexpr int32_t SCENERY_X_Y_SCALE_FACTOR = 64; // z of 0x00010000 / (4 * FOCUS)
static constexpr int32_t Z_FAR = 0x00010000;

struct Scenery
{
	const COORD_3D* coords;
	int32_t numCoords;
	int32_t numPolygons;
	const int32_t* polygons;
};

// Scenery angular positions around the horizon (range 0-255)
static const int32_t scenery_positions[NUM_SCENERY_OBJECTS] = {
	0x05, 0x0f, 0x15, 0x1f, 0x25, 0x2f, 0x35, 0x3f,
	0x45, 0x4f, 0x55, 0x5f, 0x65, 0x6f, 0x75, 0x7f,
	0x85, 0x8f, 0x95, 0x9f, 0xa5, 0xaf, 0xb5, 0xbf,
	0xc5, 0xcf, 0xd5, 0xdf, 0xe5, 0xef, 0xf5, 0xff
};

// Scenery object IDs per scenery type (range 0-24)
static const int32_t standard_numbers[NUM_SCENERY_OBJECTS] = {
	0, 13, 10, 11, 12, 5, 2, 3, 0, 1, 4, 5, 2, 1, 0, 5,
	2, 3, 4, 5, 0, 9, 6, 7, 8, 5, 0, 3, 4, 1, 2, 5
};
static const int32_t taller_numbers[NUM_SCENERY_OBJECTS] = {
	14, 15, 15, 14, 15, 14, 14, 15, 14, 15, 14, 15, 15, 14, 15, 14,
	14, 15, 15, 14, 15, 15, 14, 14, 15, 14, 15, 15, 14, 14, 14, 15
};
static const int32_t snowcapped_numbers[NUM_SCENERY_OBJECTS] = {
	16, 17, 18, 19, 17, 16, 17, 18, 18, 16, 19, 17, 19, 18, 18, 16,
	19, 17, 17, 18, 16, 16, 19, 18, 17, 16, 19, 19, 17, 18, 16, 19
};
static const int32_t building_numbers[NUM_SCENERY_OBJECTS] = {
	20, 21, 22, 23, 23, 21, 20, 20, 21, 22, 22, 20, 20, 21, 23, 22,
	21, 20, 21, 21, 23, 22, 20, 21, 23, 23, 22, 20, 21, 20, 21, 23
};
static const int32_t mixed_numbers[NUM_SCENERY_OBJECTS] = {
	16, 0, 18, 1, 17, 2, 17, 3, 18, 4, 19, 5, 20, 24, 21, 24,
	23, 22, 12, 19, 6, 16, 7, 18, 8, 16, 9, 19, 10, 18, 11, 19
};

static const int32_t* scenery_types[] = {
	standard_numbers, taller_numbers, snowcapped_numbers, building_numbers, mixed_numbers
};

// Scenery coordinates — all share z = Z_FAR
// Standard hills (14 variants, 4 coords each)
static constexpr COORD_3D standard1_c[] = {{0, 0, Z_FAR}, {0x180, 0, Z_FAR}, {0x4b, 0x1c, Z_FAR}, {0x104, 0x10, Z_FAR}};
static constexpr COORD_3D standard2_c[] = {{0, 0, Z_FAR}, {0x100, 0, Z_FAR}, {0x7d, 0x12, Z_FAR}, {0xc0, 0x1e, Z_FAR}};
static constexpr COORD_3D standard3_c[] = {{0, 0, Z_FAR}, {0x180, 0, Z_FAR}, {0x64, 0x14, Z_FAR}, {0x136, 0x25, Z_FAR}};
static constexpr COORD_3D standard4_c[] = {{0, 0, Z_FAR}, {0x100, 0, Z_FAR}, {0x46, 0x18, Z_FAR}, {0xd8, 0x24, Z_FAR}};
static constexpr COORD_3D standard5_c[] = {{0, 0, Z_FAR}, {0x180, 0, Z_FAR}, {0xc8, 0x27, Z_FAR}, {0xf0, 0x1f, Z_FAR}};
static constexpr COORD_3D standard6_c[] = {{0, 0, Z_FAR}, {0x100, 0, Z_FAR}, {0x32, 0x0c, Z_FAR}, {0xa8, 0x1a, Z_FAR}};
static constexpr COORD_3D standard7_c[] = {{0, 0, Z_FAR}, {0x172, 0, Z_FAR}, {0x70, 0x19, Z_FAR}, {0xe6, 0x14, Z_FAR}};
static constexpr COORD_3D standard8_c[] = {{0, 0, Z_FAR}, {0xfa, 0, Z_FAR}, {0x64, 0x0c, Z_FAR}, {0xbb, 0x12, Z_FAR}};
static constexpr COORD_3D standard9_c[] = {{0, 0, Z_FAR}, {0x180, 0, Z_FAR}, {0xc6, 0x1c, Z_FAR}, {0x13b, 0x18, Z_FAR}};
static constexpr COORD_3D standard10_c[] = {{0, 0, Z_FAR}, {0x100, 0, Z_FAR}, {0x23, 0x28, Z_FAR}, {0x6e, 0x37, Z_FAR}};
static constexpr COORD_3D standard11_c[] = {{0, 0, Z_FAR}, {0x159, 0, Z_FAR}, {0x5c, 0x2a, Z_FAR}, {0xf0, 0x1e, Z_FAR}};
static constexpr COORD_3D standard12_c[] = {{0, 0, Z_FAR}, {0xfa, 0, Z_FAR}, {0x2d, 0x0f, Z_FAR}, {0x80, 0x0b, Z_FAR}};
static constexpr COORD_3D standard13_c[] = {{0, 0, Z_FAR}, {0x17c, 0, Z_FAR}, {0x88, 0x2b, Z_FAR}, {0xd2, 0x23, Z_FAR}};
static constexpr COORD_3D standard14_c[] = {{0, 0, Z_FAR}, {0x100, 0, Z_FAR}, {0x4b, 0x29, Z_FAR}, {0x9b, 0x37, Z_FAR}};

// Taller peaks (2 variants, 4 coords each)
static constexpr COORD_3D taller1_c[] = {{0, 0, Z_FAR}, {0x17c, 0, Z_FAR}, {0x88, 0, Z_FAR}, {0x2b, 0xd2, Z_FAR}};
static constexpr COORD_3D taller2_c[] = {{0, 0, Z_FAR}, {0x100, 0, Z_FAR}, {0x4b, 0, Z_FAR}, {0x29, 0x9b, Z_FAR}};

// Snow-capped mountains (4 variants, 7 coords each)
static const COORD_3D snowcapped1_c[] = {
	{0, 0, Z_FAR}, {0xfa, 0, Z_FAR}, {0x1a4, 0, Z_FAR}, {0x253, 0, Z_FAR},
	{0x181, 0x2e, Z_FAR}, {0x118, 0x34, Z_FAR}, {0x19f, 0x73, Z_FAR}
};
static const COORD_3D snowcapped2_c[] = {
	{0, 0, Z_FAR}, {0x4b, 0, Z_FAR}, {0x127, 0, Z_FAR}, {0x1f4, 0, Z_FAR},
	{0xaf, 0x32, Z_FAR}, {0x87, 0x3c, Z_FAR}, {0xff, 0x48, Z_FAR}
};
static const COORD_3D snowcapped3_c[] = {
	{0, 0, Z_FAR}, {0x87, 0, Z_FAR}, {0xc5, 0, Z_FAR}, {0xfa, 0, Z_FAR},
	{0x96, 0x46, Z_FAR}, {0x69, 0x50, Z_FAR}, {0xaa, 0x5f, Z_FAR}
};
static const COORD_3D snowcapped4_c[] = {
	{0, 0, Z_FAR}, {0x87, 0, Z_FAR}, {0x113, 0, Z_FAR}, {0x1a9, 0, Z_FAR},
	{0x91, 0x2a, Z_FAR}, {0x3c, 0x32, Z_FAR}, {0x8c, 0x4d, Z_FAR}
};

// Buildings (4 variants, 6 coords each)
static const COORD_3D building1_c[] = {
	{0, 0, Z_FAR}, {0x10, 0, Z_FAR}, {0x18, 0, Z_FAR},
	{0, 0x50, Z_FAR}, {0x10, 0x50, Z_FAR}, {0x18, 0x50, Z_FAR}
};
static const COORD_3D building2_c[] = {
	{0, 0, Z_FAR}, {0x10, 0, Z_FAR}, {0x18, 0, Z_FAR},
	{0, 0x3c, Z_FAR}, {0x10, 0x3c, Z_FAR}, {0x18, 0x3c, Z_FAR}
};
static const COORD_3D building3_c[] = {
	{0, 0, Z_FAR}, {0x28, 0, Z_FAR}, {0x3c, 0, Z_FAR},
	{0, 0x39, Z_FAR}, {0x28, 0x39, Z_FAR}, {0x3c, 0x39, Z_FAR}
};
static const COORD_3D building4_c[] = {
	{0, 0, Z_FAR}, {0x69, 0, Z_FAR}, {0x7d, 0, Z_FAR},
	{0, 0x2a, Z_FAR}, {0x69, 0x2a, Z_FAR}, {0x7d, 0x2a, Z_FAR}
};

// Lake (1 variant, 4 coords)
static constexpr COORD_3D lake_c[] = {
	{0, 8, Z_FAR}, {0x32, 0, Z_FAR}, {0x28a, 0, Z_FAR}, {0x2bc, 8, Z_FAR}
};

// Polygon definitions: each is a sequence of {colour, numSides, vertexIndices...} repeated
static const int32_t standard_p[] = {5, 4, 1, 0, 2, 3};
static const int32_t taller_p[] = {4, 3, 2, 0, 3, 5, 3, 1, 2, 3};
static const int32_t snowcapped_p[] = {
	4, 4, 1, 0, 5, 4, 5, 3, 2, 1, 4, 5, 4, 3, 2, 4, 6, 15, 3, 4, 5, 6
};
static const int32_t building_p[] = {15, 4, 1, 0, 3, 4, 14, 4, 2, 1, 4, 5};
static const int32_t lake_p[] = {6, 4, 2, 1, 0, 3};

// Macro to define a Scenery entry from a coord array
#define SCENERY_DEF(coords, nPoly, poly) { coords, static_cast<int32_t>(sizeof(coords) / sizeof(coords[0])), nPoly, poly }

static const Scenery scenery_objects[] = {
	SCENERY_DEF(standard1_c, 1, standard_p),
	SCENERY_DEF(standard2_c, 1, standard_p),
	SCENERY_DEF(standard3_c, 1, standard_p),
	SCENERY_DEF(standard4_c, 1, standard_p),
	SCENERY_DEF(standard5_c, 1, standard_p),
	SCENERY_DEF(standard6_c, 1, standard_p),
	SCENERY_DEF(standard7_c, 1, standard_p),
	SCENERY_DEF(standard8_c, 1, standard_p),
	SCENERY_DEF(standard9_c, 1, standard_p),
	SCENERY_DEF(standard10_c, 1, standard_p),
	SCENERY_DEF(standard11_c, 1, standard_p),
	SCENERY_DEF(standard12_c, 1, standard_p),
	SCENERY_DEF(standard13_c, 1, standard_p),
	SCENERY_DEF(standard14_c, 1, standard_p),
	SCENERY_DEF(taller1_c, 2, taller_p),
	SCENERY_DEF(taller2_c, 2, taller_p),
	SCENERY_DEF(snowcapped1_c, 4, snowcapped_p),
	SCENERY_DEF(snowcapped2_c, 4, snowcapped_p),
	SCENERY_DEF(snowcapped3_c, 4, snowcapped_p),
	SCENERY_DEF(snowcapped4_c, 4, snowcapped_p),
	SCENERY_DEF(building1_c, 2, building_p),
	SCENERY_DEF(building2_c, 2, building_p),
	SCENERY_DEF(building3_c, 2, building_p),
	SCENERY_DEF(building4_c, 2, building_p),
	SCENERY_DEF(lake_c, 1, lake_p),
};

#undef SCENERY_DEF

// ─── DrawScenery ────────────────────────────────────────────────────────────

static void DrawScenery(SoftwareRenderer& r,
                        const double viewpoint_y,
                        const double viewpoint_x_angle,
                        const double viewpoint_y_angle,
                        const double viewpoint_z_angle)
{
	const int32_t* scenery_numbers = scenery_types[current_scenery_type];

	COORD_2D screen_coords[MAX_SCENERY_COORDS];
	Point2D points[MAX_POLY_SIDES];

	auto [screen_width, screen_height] = GetScreenDimensions(r);

	// x/z angles are fixed for all scenery objects
	const double sin_x = std::sin(viewpoint_x_angle);
	const double cos_x = std::cos(viewpoint_x_angle);
	const double sin_z = std::sin(viewpoint_z_angle);
	const double cos_z = std::cos(viewpoint_z_angle);
	const double focal = std::min(screen_height * 512.f / 480.f, screen_width * 512.f / 640.f);

	// Each scenery_positions[] entry is in the original Amiga 0..255 range,
	// representing a fraction of the full 360-degree circle around the camera.
	constexpr double kSceneryAngleStep = 2.0f * SCR_PI / 256.0f;

	// Shift scenery vertically with the viewpoint altitude. viewpoint_y is in
	// render-space world units (positive == below the horizon plane). The /2
	// matches the original Amiga "reduce using PC_FACTOR" rescale.
	const double vp_y_offset = viewpoint_y / 2;

	for (int32_t m = 0; m < NUM_SCENERY_OBJECTS; m++)
	{
		const int32_t number = scenery_numbers[m];
		const Scenery& scenery = scenery_objects[number];

		// y angle for this object: viewpoint yaw + per-object azimuth, then
		// negated to match the original (clockwise) scenery orientation.
		const double y_angle = -(viewpoint_y_angle + scenery_positions[m] * kSceneryAngleStep);

		const double sin_y = std::sin(y_angle);
		const double cos_y = std::cos(y_angle);

		// Rotate scenery about y/x/z axes and perform perspective projection
		bool visible = true;
		for (int32_t i = 0; i < scenery.numCoords; i++)
		{
			double x = scenery.coords[i].x * SCENERY_X_Y_SCALE_FACTOR;
			double y = -(scenery.coords[i].y * SCENERY_X_Y_SCALE_FACTOR);
			double z = scenery.coords[i].z;

			y -= vp_y_offset; // reduce using PC_FACTOR
			y += 2 * SCENERY_X_Y_SCALE_FACTOR; // prevent sky showing through

			// rotate about y axis
			const double rot_x_y = x * cos_y + z * sin_y;
			const double rot_z_y = z * cos_y - x * sin_y;

			// rotate about x axis
			z = rot_z_y;
			const double rot_y_x = y * cos_x - z * sin_x;
			const double rot_z = y * sin_x + z * cos_x;

			// rotate about z axis
			x = rot_x_y;
			y = rot_y_x;
			const double rot_x = x * cos_z - y * sin_z;
			const double rot_y = x * sin_z + y * cos_z;

			// Skip this object if any z is negative (behind the camera)
			if (rot_z <= 0.0f)
			{
				visible = false;
				break;
			}

			// perspective projection — see DrawHorizon for rationale.
			const double zd = rot_z / focal;
			const double inv_z = 1.0f / (zd > 0.0f ? zd : 1.0f);

			screen_coords[i].x = static_cast<int32_t>(rot_x * inv_z) + screen_width / 2;
			screen_coords[i].y = static_cast<int32_t>(rot_y * inv_z) + screen_height / 2;
		}

		if (!visible)
			continue;

		// Draw scenery object polygons
		const int32_t* polygons = scenery.polygons;
		for (int32_t i = 0; i < scenery.numPolygons; i++)
		{
			const uint8_t colour = static_cast<uint8_t>(*polygons++);

			const int32_t sides = *polygons++;

			// store all polygon's points
			for (int32_t j = 0; j < sides; j++)
			{
				const int32_t offset = *polygons++;
				points[j].x = screen_coords[offset].x;
				points[j].y = screen_coords[offset].y;
			}

			// draw current polygon
			if (sides >= 3)
				r.DrawScreenTriangleFan(points, sides, SCRGB(SCR_BASE_COLOUR + colour));
		}
	}
}

// ─── DrawBackdrop ───────────────────────────────────────────────────────────

void DrawBackdrop(SoftwareRenderer& r,
                  const double viewpoint_y,
                  const double viewpoint_x_angle,
                  const double viewpoint_y_angle,
                  const double viewpoint_z_angle)
{
	DrawHorizon(r, viewpoint_y, viewpoint_x_angle, viewpoint_z_angle);
	DrawScenery(r, viewpoint_y, viewpoint_x_angle, viewpoint_y_angle, viewpoint_z_angle);
}
