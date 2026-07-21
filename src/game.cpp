//
// game.cpp — Top-level game logic (platform-independent).
//
// Responsibilities:
//   - App entry points: AppInit / AppRun / AppHandleFrameSize / AppHandleKey*
//   - Frame update (OnFrameMove) and frame render (OnFrameRender, RenderText)
//   - Palette setup, resource creation/teardown, sound buffer setup
//   - Viewpoint calculation for menu, preview and in-game camera
//   - Fixed-point sin/cos table generation (CreateSinCosTable, LockAngle)
//   - Car 3D model construction and rendering (CreateCarVertexBuffer, DrawCar)
//   - World transform matrices for the player car, opponent car and track
//

#include <algorithm>
#include <cmath>
#include <ctime>

#include "platform.h"
#include "game.h"
#include "render.software.h"

using namespace std::string_view_literals;


constexpr int32_t DEFAULT_FRAME_GAP = 4;
constexpr double SIMULATION_STEP_SECONDS = 1.0 / 25.0;

// Inside-view camera height above the car body, in render-space world units.
// The original 100 sat the eye very close to the road; combined with
// the road being a single-sided surface, suspension dives and bump tilts could
// pop the eye through the tarmac and the road would vanish (back-face culled).
// 180 keeps the view feeling first-person while leaving headroom for normal
// suspension travel.
constexpr int32_t HEIGHT_ABOVE_ROAD = 180;

// Hard floor: the eye is never allowed closer than this many world units to the
// road surface beneath the car. Belt-and-braces against the see-through-track
// artefact even if HEIGHT_ABOVE_ROAD is reduced or a future change lets the
// car body sink further than LimitViewpointY currently permits.
constexpr int32_t MIN_EYE_ABOVE_ROAD = 60;

GameState g_gameState;
TrackState g_trackState;
SoundState g_soundState;

GameModeType g_gameMode = TRACK_MENU;

std::vector<SWTexture> g_roadTexture;

SoftwareRenderer g_renderer;

static uint32_t lastInput = 0;
static bool ctrlHeld = false;

static int32_t frameGap = DEFAULT_FRAME_GAP;
static bool bFrameMoved = false;

static bool bShowStats = false;
static bool bPaused = false;
static bool bPlayerPaused = false;
static bool bOpponentPaused = false;
static bool bOutsideView = false;
static double gameStartTime, gameEndTime;

static double g_fpsTime = 0.0;
static int g_fpsFrameCount = 0;
static double g_fps = 0.0f;

// game.cpp's local mirror of the player car's pose. Position is in render-space
// world units; angles are in MAX_ANGLE units (Amiga convention).
static double player1_x = 0.0, player1_y = 0.0, player1_z = 0.0;
static double player1_x_angle = 0.0;
static double player1_y_angle = 0.0;
static double player1_z_angle = 0.0;

// game.cpp's local mirror of the opponent's pose. Position is in render-space
// world units; angles are in MAX_ANGLE units (Amiga convention) inherited
// directly from OpponentPose.
static double opponent_x = 0, opponent_y = 0, opponent_z = 0;
static double opponent_x_angle = 0.0f, opponent_y_angle = 0.0f, opponent_z_angle = 0.0f;

// Viewpoint position, in render-space world units. The viewpoint calculators
// below produce render-space coordinates directly so the renderer (matView,
// vEyePt, DrawBackdrop) can consume them without any further scale conversion.
static double viewpoint1_x, viewpoint1_y, viewpoint1_z;
// Viewpoint orientation, in double radians. Set by the per-frame viewpoint
// calculators and consumed directly by the renderer (Mat4RotationX/Y/Z and
// DrawBackdrop), so no further unit conversion is required.
static double viewpoint1_x_angle = 0.0f,
              viewpoint1_y_angle = 0.0f,
              viewpoint1_z_angle = 0.0f;
// Camera lookat target, also in render-space world units (matches viewpoint1_*).
static double target_x, target_y, target_z;

// Render-only smoothed pitch/roll for the car body and trailing camera.
// Driven toward the road-surface plane (derived from the three wheel
// road-height samples) when the car is grounded, or toward the car's physics
// angles when airborne. Keeps the visible orientation flush with the track
// even though the underlying spring-damper physics angles can lag.
static double render_x_angle = 0;
static double render_z_angle = 0;

// Render-only car height (world-space, +Y up) used by SetCarWorldTransform.
static double render_wheel_y[4] = {};
// When grounded it is anchored to the road plane sampled under the wheels so
// the visible car sits flush with the track instead of following the lagged
// physics player_y (which is allowed to sink up to MAX_BELOW_ROAD into the
// surface). Driven by UpdateRenderAngles each frame.
static double render_car_y = 0;

// Compute the angle (in MAX_ANGLE units) whose sine equals s. Used to convert
// a road-plane slope into an Amiga-style angle.
static double AngleFromSin(double s)
{
	if (s > 1.0) s = 1.0;
	else if (s < -1.0) s = -1.0;
	const double rads = std::asin(s);
	return rads * MAX_ANGLE / (2.0 * PI);
}

// Wrap an angle delta into the range [-MAX_ANGLE/2, +MAX_ANGLE/2] so that
// shortest-path lerping works across the 0/MAX_ANGLE seam.
static double WrapAngleSigned(double a)
{
	a = WrapAngle(a);
	if (a >= MAX_ANGLE / 2) a -= MAX_ANGLE;
	return a;
}

// Update render_x_angle / render_z_angle. When grounded, target = road plane
// derived from the three wheel road-height samples (inverting the math in
// CalculateActualWheelHeights). When airborne, target = physics angles.
static void UpdateRenderAngles()
{
	double target_x_angle = player1_x_angle;
	double target_z_angle = player1_z_angle;

	const bool grounded = g_gameState.touching_road &&
		g_gameState.front_left_road_height != GameState::OFF_ROAD_HEIGHT &&
		g_gameState.front_right_road_height != GameState::OFF_ROAD_HEIGHT &&
		g_gameState.rear_road_height != GameState::OFF_ROAD_HEIGHT;

	if (grounded)
	{
		// Inverting CalculateActualWheelHeights():
		//   front_avg_road - rear_road = sin_x * 4096   (road_height units)
		//   front_left_road - front_right_road = sin_z * 2048
		const double front_avg_road =
			(g_gameState.front_left_road_height + g_gameState.front_right_road_height) / 2;
		const double pitch_sin = (front_avg_road - g_gameState.rear_road_height) / 4096.0;
		const double roll_sin =
			(g_gameState.front_left_road_height - g_gameState.front_right_road_height) / 2048.0;
		// DrawWorld uses the opposite X/Z rotation convention from the physics
		// coefficients, just as CarBehaviour's returned airborne angles do.
		target_x_angle = WrapAngle(-AngleFromSin(pitch_sin));
		target_z_angle = WrapAngle(-AngleFromSin(roll_sin));
	}

	if (g_gameState.on_chains)
	{
		// A respawn is a new placement, not a continuation of the crash pose.
		render_x_angle = target_x_angle;
		render_z_angle = target_z_angle;
	}
	else
	{
		// Shortest-path lerp toward target with a moderate time constant.
		constexpr double kSmoothingFactor = 0.25; // 1/4 step per frame; ~4 frames to settle
		const double dx = WrapAngleSigned(target_x_angle - render_x_angle);
		const double dz = WrapAngleSigned(target_z_angle - render_z_angle);
		render_x_angle = WrapAngle(render_x_angle + dx * kSmoothingFactor);
		render_z_angle = WrapAngle(render_z_angle + dz * kSmoothingFactor);
	}

	// Drive the render-only car height. When grounded, place the origin high
	// enough for every transformed wheel-bottom point to clear its road sample.
	// When airborne, fall back to the physics height with the original offset.
	if (grounded)
	{
		const double x_angle = render_x_angle * ANGLE_TO_RADIANS;
		const double z_angle = render_z_angle * ANGLE_TO_RADIANS;
		const double sin_x = std::sin(x_angle);
		const double cos_x = std::cos(x_angle);
		const double sin_z = std::sin(z_angle);
		const double cos_z = std::cos(z_angle);

		// The visual pitch/roll deliberately lags the road plane. At an abrupt
		// ramp transition, centring the model on the average road height lets
		// its still-level front wheels pass below the rising road. Choose the
		// origin height required by the highest of the three wheel-bottom points,
		// transformed in the same Z-then-X order as SetCarWorldTransform.
		auto requiredOrigin = [&](const double road_height, const double x, const double z)
		{
			constexpr double wheel_bottom_y = -VCAR_HEIGHT / 4.0;
			const double after_z_y = x * sin_z + wheel_bottom_y * cos_z;
			const double rotated_y = after_z_y * cos_x - z * sin_x;
			return road_height / 16.0 - rotated_y;
		};

		const double rear_left_origin = requiredOrigin(
			g_gameState.rear_road_height, -VCAR_WIDTH * 3.0 / 8.0, -VCAR_LENGTH / 2.0);
		const double rear_right_origin = requiredOrigin(
			g_gameState.rear_road_height, VCAR_WIDTH * 3.0 / 8.0, -VCAR_LENGTH / 2.0);
		const double front_right_origin = requiredOrigin(
			g_gameState.front_right_road_height, VCAR_WIDTH * 3.0 / 8.0, VCAR_LENGTH / 2.0);
		const double front_left_origin = requiredOrigin(
			g_gameState.front_left_road_height, -VCAR_WIDTH * 3.0 / 8.0, VCAR_LENGTH / 2.0);
		constexpr double visualRideClearance = 2.0;
		render_car_y = std::max({rear_left_origin, rear_right_origin, front_right_origin, front_left_origin}) +
			visualRideClearance;

		constexpr double maxSuspensionDrop = 14.0;
		const double wheelOrigins[] = {
			rear_left_origin, rear_right_origin, front_left_origin, front_right_origin
		};
		for (int wheel = 0; wheel < 4; ++wheel)
			render_wheel_y[wheel] = std::clamp(wheelOrigins[wheel] - render_car_y,
				-maxSuspensionDrop, 0.0);
	}
	else
	{
		render_car_y = -player1_y + VCAR_HEIGHT * 3 / 8;
		std::fill(std::begin(render_wheel_y), std::end(render_wheel_y), 0.0);
	}
}

void InitialiseData(TrackState& t)
{
	ConvertAmigaTrack(t, LITTLE_RAMP);
	srand(static_cast<unsigned>(std::time(nullptr)));
}

void FreeData(SoundState& s, TrackState& t)
{
	FreeTrackData(t);
	DestroySoundBuffers(s);
}

ScreenSize GetScreenDimensions(const SoftwareRenderer& r)
{
	return {r.GetWidth(), r.GetHeight()};
}

// Amiga-style palette for game colors
constexpr int NUM_PALETTE_ENTRIES = 42;

static PaletteColor SCPalette[NUM_PALETTE_ENTRIES] =
{
	{0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00},

	// car colours 1
	{0x00, 0x00, 0x00},
	{0x88, 0x00, 0x22},
	{0xaa, 0x00, 0x33},
	{0xcc, 0x00, 0x44},
	{0xee, 0x00, 0x55},
	{0x22, 0x22, 0x33},
	{0x44, 0x44, 0x44},
	{0x33, 0x33, 0x33},

	// car colours 2
	{0x00, 0x00, 0x00},
	{0x22, 0x00, 0x88},
	{0x33, 0x00, 0xaa},
	{0x44, 0x00, 0xcc},
	{0x55, 0x00, 0xee},
	{0x22, 0x22, 0x33},
	{0x44, 0x44, 0x44},
	{0x33, 0x33, 0x33},

	// track colours (i.e. Stunt Car Racer car colours)
	{0x00, 0x00, 0x00},
	{0x99, 0x99, 0x77},
	{0xbb, 0xbb, 0x99},
	{0xff, 0xff, 0x00},
	{0x99, 0xbb, 0x33},
	{0x55, 0x77, 0x77},
	{0x55, 0xbb, 0xff},
	{0x55, 0x99, 0xff},
	{0x33, 0x55, 0x77},
	{0x55, 0x00, 0x00},
	{0x77, 0x33, 0x33},
	{0x99, 0x55, 0x55},
	{0xdd, 0x99, 0x99},
	{0x77, 0x77, 0x55},
	{0xbb, 0xbb, 0xbb},
	{0xff, 0xff, 0xff}
};

uint32_t SCRGB(const int32_t colour_index) // return full RGB value
{
	return XRGB(SCPalette[colour_index].r,
	            SCPalette[colour_index].g,
	            SCPalette[colour_index].b);
}

void CreateResources()
{
	CreateTrackVertexBuffer(g_trackState);
	CreateShadowVertexBuffer();
	CreateCarVertexBuffer();

	// Load road textures from resources
	static std::wstring_view roadTexNames[] = {
		L"RoadYellowDark", L"RoadYellowLight",
		L"RoadRedDark", L"RoadRedLight",
		L"RoadBlack", L"RoadWhite"
	};
	g_roadTexture.clear();
	g_roadTexture.resize(std::size(roadTexNames));
	for (size_t i = 0; i < std::size(roadTexNames); ++i)
	{
		const auto bmp = PlatformLoadBitmapResource(roadTexNames[i]);
		if (bmp)
			g_roadTexture[i] = SWTexture{bmp->pixels, bmp->width, bmp->height};
		else
			PlatformShowError(format(L"Failed to load road texture: %ls", roadTexNames[i].data()), L"Warning");
	}

	// Set projection transform
	const double fAspect = static_cast<double>(g_renderer.GetWidth()) / static_cast<double>(g_renderer.GetHeight());
	const Mat4 matProj = Mat4PerspectiveFovLH(SCR_PI / 4.0f, fAspect, 0.5f, FURTHEST_Z);
	g_renderer.SetProjectionMatrix(matProj);
}

void FreeResources()
{
	FreeTrackVertexBuffer();
	FreeShadowVertexBuffer();
	FreeCarVertexBuffer();
	g_roadTexture.clear();
}


static void CalcTrackMenuViewpoint()
{
	static double circle_y_angle = 0.0;

	// Centre and orbit radius in render-space world units.
	constexpr double centre_r = NUM_TRACK_CUBES * WORLD_CUBE_SIZE / 2.0;
	constexpr double radius_r = (NUM_TRACK_CUBES - 2) * WORLD_CUBE_SIZE;

	target_x = centre_r;
	target_y = 0;
	target_z = centre_r;

	// Orbit camera around the track
	if (!bPaused) circle_y_angle += 128;
	circle_y_angle = WrapAngle(circle_y_angle);

	const double circle_rad = circle_y_angle * ANGLE_TO_RADIANS;
	const double sin = std::sin(circle_rad);
	const double cos = std::cos(circle_rad);

	viewpoint1_x = centre_r + sin * radius_r;
	viewpoint1_y = -3.0 * WORLD_CUBE_SIZE;
	viewpoint1_z = centre_r + cos * radius_r;

	const auto trackMenuView = LockViewpointToTarget(viewpoint1_x,
	                                                 viewpoint1_y,
	                                                 viewpoint1_z,
	                                                 target_x,
	                                                 target_y,
	                                                 target_z);
	viewpoint1_x_angle = trackMenuView.x_angle;
	viewpoint1_y_angle = trackMenuView.y_angle;
	viewpoint1_z_angle = 0.0f;
}

static void CalcTrackPreviewViewpoint(const TrackState& t)
{
	// opponent_x/y/z are render-space already.
	target_x = opponent_x;
	target_y = opponent_y;
	target_z = opponent_z;

	constexpr double centre_r = NUM_TRACK_CUBES * WORLD_CUBE_SIZE / 2.0;

	viewpoint1_x = centre_r;

	if (t.TrackID == DRAW_BRIDGE)
		viewpoint1_y = target_y - WORLD_CUBE_SIZE * 5.0 / 2.0;
	else
		viewpoint1_y = target_y - WORLD_CUBE_SIZE / 2.0;

	viewpoint1_z = centre_r;

	viewpoint1_x += (target_x - viewpoint1_x) / 2;
	viewpoint1_z += (target_z - viewpoint1_z) / 2;

	const auto previewView = LockViewpointToTarget(viewpoint1_x,
	                                               viewpoint1_y,
	                                               viewpoint1_z,
	                                               target_x,
	                                               target_y,
	                                               target_z);
	viewpoint1_x_angle = previewView.x_angle;
	viewpoint1_y_angle = previewView.y_angle;
	viewpoint1_z_angle = 0.0f;
}

static void CalcGameViewpoint()
{
	if (bOutsideView)
	{
		// Trailing camera: keep the camera level so the horizon remains stable.
		const auto rot = CalcYXZTrigCoefficients(0,
		                                         player1_y_angle,
		                                         0);

		const auto offset = WorldOffset(rot, 0.0, 0xc0, 0x300);
		// player1_x/y/z and offset are both render-space; subtract directly.
		viewpoint1_x = player1_x - offset.x;
		viewpoint1_y = player1_y - offset.y;
		viewpoint1_z = player1_z - offset.z;

		viewpoint1_x_angle = 0.0f;
		viewpoint1_y_angle = AngleToRadians(player1_y_angle);
		viewpoint1_z_angle = 0.0f;
	}
	else
	{
		viewpoint1_x = player1_x;
		viewpoint1_y = player1_y - static_cast<double>(HEIGHT_ABOVE_ROAD);
		viewpoint1_z = player1_z;

		// Clamp the eye so it can't sink below the road surface near the car.
		// Road heights live in "internal" Amiga units; the conversion to external
		// player_y coords (where MORE NEGATIVE == HIGHER in world) is
		//   external_y = -road_height * 256 * LOCAL_Y_FACTOR  ==  -road_height << 10
		// (matching the reverse calc in LimitViewpointY). Bigger road_height ==
		// higher road surface, so to keep the eye above ALL nearby road we clamp
		// against the MAXIMUM of the three wheel road-height samples (front-left,
		// front-right, rear). Using min() instead would only protect against the
		// lower side of a banked turn / the lower end on a hill crest, leaving
		// the eye free to dive through the higher side.
		// Skip the clamp when every sample is the off-road sentinel (car is in
		// the void), otherwise it would shove the eye absurdly high.
		const double road_heights[] = {
			g_gameState.front_left_road_height,
			g_gameState.front_right_road_height,
			g_gameState.rear_road_height
		};
		double road_height = 0;
		bool road_found = false;
		for (const double sample : road_heights)
		{
			if (sample != GameState::OFF_ROAD_HEIGHT && (!road_found || sample > road_height))
			{
				road_height = sample;
				road_found = true;
			}
		}
		if (road_found)
		{
			// road_height is in road_height units (256-per-render-Y). The matching
			// render-space external y is -road_height / 16.
			const double road_external_y_render = -road_height / 16.0;
			const double max_viewpoint_y = road_external_y_render -
				static_cast<double>(MIN_EYE_ABOVE_ROAD);
			if (viewpoint1_y > max_viewpoint_y)
				viewpoint1_y = max_viewpoint_y;
		}

		viewpoint1_x_angle = AngleToRadians(player1_x_angle);
		viewpoint1_y_angle = AngleToRadians(player1_y_angle);
		viewpoint1_z_angle = AngleToRadians(player1_z_angle);
	}
}

static Mat4 matWorldTrack, matWorldCar, matWorldOpponentsCar;
static Mat4 matWorldCarWheels[4];
static void DrawPlayerCar(SoftwareRenderer& r);

static void SetCarWorldTransform()
{
	// Use render angles (road-aligned when grounded) so the car visibly sits
	// flush with the track in outside view. Yaw still comes from physics.
	Mat4 matRot = Mat4::Identity();
	const double xa = AngleToRadians(render_x_angle);
	const double ya = AngleToRadians(player1_y_angle);
	const double za = AngleToRadians(render_z_angle);
	Mat4 matTemp = Mat4RotationZ(za);
	matRot = Mat4Multiply(matRot, matTemp);
	matTemp = Mat4RotationX(xa);
	matRot = Mat4Multiply(matRot, matTemp);
	matTemp = Mat4RotationY(ya);
	matRot = Mat4Multiply(matRot, matTemp);
	// player1_x/z are render-space units already. The vertical position comes
	// from render_car_y, which anchors the wheel-contact plane to the road when
	// grounded (see UpdateRenderAngles) so the car stays flush with the track.
	const Mat4 matTrans = Mat4Translation(player1_x,
	                                      render_car_y,
	                                      player1_z);
	matWorldCar = Mat4Multiply(matRot, matTrans);
	for (int wheel = 0; wheel < 4; ++wheel)
	{
		const Mat4 suspension = Mat4Translation(0.0, render_wheel_y[wheel], 0.0);
		matWorldCarWheels[wheel] = Mat4Multiply(suspension, matWorldCar);
	}
}

static void SetOpponentsCarWorldTransform()
{
	Mat4 matRot = Mat4::Identity();
	Mat4 matTemp = Mat4RotationZ(opponent_z_angle);
	matRot = Mat4Multiply(matRot, matTemp);
	matTemp = Mat4RotationX(opponent_x_angle);
	matRot = Mat4Multiply(matRot, matTemp);
	matTemp = Mat4RotationY(opponent_y_angle);
	matRot = Mat4Multiply(matRot, matTemp);
	// opponent_x/y/z are render-space units already.
	const Mat4 matTrans = Mat4Translation(opponent_x,
	                                      -opponent_y + VCAR_HEIGHT / 4,
	                                      opponent_z);
	matWorldOpponentsCar = Mat4Multiply(matRot, matTrans);
}

static void StopEngineSound()
{
	if (g_soundState.engineSoundPlaying)
	{
		for (int i = 0; i < 8; i++)
			PlatformSoundStop(g_soundState.EngineSoundBuffers[i]);

		g_soundState.engineSoundPlaying = false;
	}
}

void OnFrameMove(const double /*fTime*/, const TrackState& t, const GameState& /*p*/)
{
	const uint32_t input = lastInput; // take copy of user input

	bFrameMoved = false;

	if (g_gameMode == GAME_OVER)
	{
		StopEngineSound();
		return;
	}

	if (bPaused)
	{
		StopEngineSound();
	}

	if (t.TrackID == NO_TRACK)
		return;

	// Track preview and game mode run on the fixed simulation clock.
	if (g_gameMode == TRACK_PREVIEW || g_gameMode == GAME_IN_PROGRESS)
	{
		if (g_gameMode == GAME_IN_PROGRESS)
		{
			// Advance engine and wheel state once per fixed simulation tick.
			if (!bPaused) FramesWheelsEngine(g_soundState, g_soundState.EngineSoundBuffers);
		}
	}
	else if (g_gameMode == TRACK_MENU)
	{
		StopEngineSound();
	}

	if (!bPaused)
		MoveDrawBridge(g_trackState, g_gameState);

	// CarBehaviour
	if (g_gameMode == TRACK_PREVIEW || g_gameMode == GAME_IN_PROGRESS)
	{
		if (!bPaused)
		{
			if (g_gameMode == GAME_IN_PROGRESS && !bPlayerPaused)
			{
				const auto carPose = CarBehaviour(g_trackState, g_gameState, g_soundState,
				                                  input,
				                                  player1_x,
				                                  player1_y,
				                                  player1_z,
				                                  player1_x_angle,
				                                  player1_y_angle,
				                                  player1_z_angle);
				player1_x = carPose.x;
				player1_y = carPose.y;
				player1_z = carPose.z;
				player1_x_angle = carPose.x_angle;
				player1_y_angle = carPose.y_angle;
				player1_z_angle = carPose.z_angle;
			}

			const auto oppPose = OpponentBehaviour(
				g_trackState,
				g_gameState,
				bOpponentPaused);
			opponent_x = oppPose.x;
			opponent_y = oppPose.y;
			opponent_z = oppPose.z;
			opponent_x_angle = oppPose.x_angle;
			opponent_y_angle = oppPose.y_angle;
			opponent_z_angle = oppPose.z_angle;
		}

		player1_y = LimitViewpointY(g_trackState, g_gameState, player1_y);

		// Smooth render-only orientation toward the road plane so the trailing
		// camera and visible car body sit flush with the track surface.
		UpdateRenderAngles();
	}

	if (g_gameMode == TRACK_MENU || g_gameMode == TRACK_PREVIEW)
	{
		if (g_gameMode == TRACK_MENU)
			CalcTrackMenuViewpoint();
		else
		{
			CalcTrackPreviewViewpoint(t);
			SetOpponentsCarWorldTransform();
		}

		// viewpoint1_* and target_* are already in render-space world units;
		// the only post-processing the renderer needs is the y-axis flip
		// matching the LookAt convention (positive y == up on screen, while
		// our world stores higher altitudes as more-negative y).
		target_y = -target_y;

		// Set the track's world transform matrix
		matWorldTrack = Mat4::Identity();

		// Set the view transform matrix using LookAt
		const Vec3 vUpVec(0.0f, 1.0f, 0.0f);
		const Vec3 vEyePt(viewpoint1_x, -viewpoint1_y, viewpoint1_z);
		const Vec3 vLookatPt(target_x, target_y, target_z);
		const Mat4 matView = Mat4LookAtLH(vEyePt, vLookatPt, vUpVec);
		g_renderer.SetViewMatrix(matView);
	}
	else if (g_gameMode == GAME_IN_PROGRESS)
	{
		CalcGameViewpoint();

		matWorldTrack = Mat4::Identity();

		SetOpponentsCarWorldTransform();

		if (bOutsideView)
		{
			SetCarWorldTransform();
		}

		// Build view matrix manually: translate then rotate.
		const Mat4 matTrans = Mat4Translation(-viewpoint1_x,
		                                      viewpoint1_y,
		                                      -viewpoint1_z);
		Mat4 matRot = Mat4::Identity();
		const double xa = -viewpoint1_x_angle;
		const double ya = -viewpoint1_y_angle;
		const double za = -viewpoint1_z_angle;
		Mat4 matTemp = Mat4RotationY(ya);
		matRot = Mat4Multiply(matRot, matTemp);
		matTemp = Mat4RotationX(xa);
		matRot = Mat4Multiply(matRot, matTemp);
		matTemp = Mat4RotationZ(za);
		matRot = Mat4Multiply(matRot, matTemp);
		const Mat4 matView = Mat4Multiply(matTrans, matRot);
		g_renderer.SetViewMatrix(matView);
	}

	if (!bPaused)
		bFrameMoved = true;
}

static void StartGame()
{
	g_gameState.bNewGame = true;
	g_gameMode = GAME_IN_PROGRESS;
	ResetLapData(g_gameState, OPPONENT);
	ResetLapData(g_gameState, PLAYER);
	gameStartTime = PlatformGetTime();
	gameEndTime = 0;
	g_gameState.boostReserve = g_trackState.StandardBoost;
	g_gameState.boostUnit = 0;
	bPlayerPaused = bOpponentPaused = false;
}

static void SelectTrack()
{
	g_gameState.bNewGame = true;
	ResetPlayer(g_gameState);
	g_gameMode = TRACK_PREVIEW;
	bPlayerPaused = bOpponentPaused = false;
}


void RenderText(const double /*fTime*/, const TrackState& track, const GameState& game,
                const GameModeType GameMode)
{
	const int screenW = g_renderer.GetWidth();
	const int screenH = g_renderer.GetHeight();
	constexpr uint32_t textColor = XRGB(255, 255, 0);
	constexpr int textWidth = 8;
	constexpr int textHeight = 8;
	constexpr int hudPadding = 8;
	constexpr int hudRowHeight = 14;

	auto drawTextRightAligned = [&](const int right, const int y, const std::wstring_view text, const uint32_t color)
	{
		g_renderer.DrawGameText(right - static_cast<int>(text.size()) * textWidth, y, text, color);
	};

	auto drawTextCentered = [&](const int y, const std::wstring_view text, const uint32_t color)
	{
		const int width = static_cast<int>(text.size()) * textWidth;
		g_renderer.DrawGameText((screenW - width) / 2, y, text, color);
	};

	if (bShowStats)
	{
		g_renderer.DrawGameText(hudPadding, 4, L"Version 1.0", textColor);
	}

	switch (GameMode)
	{
	case TRACK_MENU:
		if (track.TrackID != NO_TRACK)
			g_renderer.DrawTextLargeCentered(10, GetTrackName(track.TrackID), textColor);
		else
			g_renderer.DrawTextLargeCentered(10, L"Select a Track", textColor);
		break;

	case TRACK_PREVIEW:
		g_renderer.DrawTextLargeCentered(10, GetTrackName(track.TrackID), textColor);
		break;

	case GAME_IN_PROGRESS:
	case GAME_OVER:
		{
			wchar_t lapText[3] = L"  ";
			std::wstring_view title;
			uint32_t titleColor = textColor;

			// Output opponent's name for four seconds at race start
			if (PlatformGetTime() - gameStartTime < 4.0 && game.opponentsID != NO_OPPONENT)
			{
				title = GetOpponentName(game.opponentsID);
			}
			if (game.lapNumber[PLAYER] > 0)
				swprintf_s(lapText, 3, L"%d", game.lapNumber[PLAYER]);

			const bool compactHud = screenW < 480;
			const auto lapBoost = compactHud
				? format(L"Lap %s  Boost %d", lapText, game.boostReserve)
				: format(L"Lap: %s   Boost: %d", lapText, game.boostReserve);
			const auto opponentDistance = compactHud
				? format(L"Opp. distance %d", CalculateOpponentsDistance(game))
				: format(L"Opponent Distance: %d", CalculateOpponentsDistance(game));
			const auto speed = format(compactHud ? L"Speed %d" : L"Speed: %d", CalculateDisplaySpeed(g_gameState));
			const auto damage = format(compactHud ? L"Damage %d" : L"Damage: %d", game.new_damage);

			const int hudRows = compactHud ? 4 : 2;
			const int hudHeight = hudPadding * 2 + hudRows * hudRowHeight - (hudRowHeight - textHeight);
			const int hudTop = screenH - hudHeight;
			g_renderer.BlendRect(0, hudTop, screenW - 1, screenH - 1, XRGB(0, 0, 0), 128);

			if (compactHud)
			{
				g_renderer.DrawGameText(hudPadding, hudTop + hudPadding, lapBoost, textColor);
				g_renderer.DrawGameText(hudPadding, hudTop + hudPadding + hudRowHeight, opponentDistance, textColor);
				g_renderer.DrawGameText(hudPadding, hudTop + hudPadding + hudRowHeight * 2, speed, textColor);
				g_renderer.DrawGameText(hudPadding, hudTop + hudPadding + hudRowHeight * 3, damage, textColor);
			}
			else
			{
				g_renderer.DrawGameText(hudPadding, hudTop + hudPadding, lapBoost, textColor);
				drawTextRightAligned(screenW - hudPadding, hudTop + hudPadding, speed, textColor);
				g_renderer.DrawGameText(hudPadding, hudTop + hudPadding + hudRowHeight, opponentDistance, textColor);
				drawTextRightAligned(screenW - hudPadding, hudTop + hudPadding + hudRowHeight, damage, textColor);
			}

			if (game.raceFinished)
			{
				const double currentTime = PlatformGetTime();
				if (gameEndTime == 0.0)
					gameEndTime = currentTime;

				const double diffTime = currentTime - gameEndTime;

				if (GameMode == GAME_OVER)
				{
					title = L"GAME OVER";
					drawTextCentered(30, screenW >= 360
						? L"Use Game menu to return to track menu"
						: L"Use Game menu to return", textColor);
				}
				else
				{
					const int32_t intTime = static_cast<int32_t>(diffTime);
					if (diffTime - static_cast<double>(intTime) < 0.5)
						titleColor = XRGB(255, 255, 255);
					else
						titleColor = XRGB(0, 0, 0);

					title = game.raceWon ? L"RACE WON" : L"RACE LOST";
				}
			}

			if (!title.empty())
				g_renderer.DrawTextLargeCentered(10, title, titleColor);
		}
		break;
	}
}


void OnFrameRender(const TrackState& t, const GameModeType GameMode, const double fTime)
{
	g_renderer.ClearDepth();

	// Draw Backdrop
	DrawBackdrop(g_renderer, viewpoint1_y, viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle);

	// Draw Track
	g_renderer.SetWorldMatrix(matWorldTrack);
	DrawTrack(t, GameMode, g_renderer, g_gameState.player_current_piece,
	          g_gameState.player_current_segment, g_roadTexture);

	switch (GameMode)
	{
	case TRACK_MENU:
		break;

	case TRACK_PREVIEW:
		// Draw Opponent's Car
		g_renderer.SetWorldMatrix(matWorldOpponentsCar);
		DrawCar(g_renderer);
		break;

	case GAME_IN_PROGRESS:
	case GAME_OVER:
		// Draw Opponent's Car
		g_renderer.SetWorldMatrix(matWorldOpponentsCar);
		DrawCar(g_renderer);

		if (bOutsideView)
		{
			// Draw Player1's Car
			DrawPlayerCar(g_renderer);
		}
		break;
	}

	if (GameMode == GAME_IN_PROGRESS)
	{
		DrawOtherGraphics(g_gameState, g_soundState);

		if (bFrameMoved) UpdateDamage(g_gameState, g_soundState);

		UpdateLapData(g_gameState, g_trackState);
	}

	RenderText(fTime, g_trackState, g_gameState, GameMode);
}

void AppHandleKeyDown(const uint32_t nChar)
{
	switch (nChar)
	{
	// Track selection keys in TRACK_MENU mode
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
		if (g_gameMode == TRACK_MENU)
		{
			const int32_t track_number = nChar - '1';
			if (!ConvertAmigaTrack(g_trackState, track_number))
			{
				PlatformShowError(L"Failed to convert track", L"Error");
				return;
			}
			CreateTrackVertexBuffer(g_trackState);
		}
		break;

	// controls for CarBehaviour, Player 1
	case PlatformKey::Left:
		if (g_gameMode == TRACK_MENU)
		{
			int32_t newTrack = g_trackState.TrackID == NO_TRACK ? 0 : g_trackState.TrackID - 1;
			if (newTrack < 0) newTrack = NUM_TRACKS - 1;
			if (ConvertAmigaTrack(g_trackState, newTrack))
				CreateTrackVertexBuffer(g_trackState);
		}
		else
			lastInput |= KEY_P1_LEFT;
		break;

	case PlatformKey::Right:
		if (g_gameMode == TRACK_MENU)
		{
			int32_t newTrack = g_trackState.TrackID == NO_TRACK ? 0 : g_trackState.TrackID + 1;
			if (newTrack >= NUM_TRACKS) newTrack = 0;
			if (ConvertAmigaTrack(g_trackState, newTrack))
				CreateTrackVertexBuffer(g_trackState);
		}
		else
			lastInput |= KEY_P1_RIGHT;
		break;

	case PlatformKey::Control:
		ctrlHeld = true;
		if (lastInput & KEY_P1_ACCEL)
			lastInput |= KEY_P1_BOOST;
		break;

	case PlatformKey::Up:
		if (g_gameMode == TRACK_MENU)
			NextSceneryType();
		else
		{
			lastInput |= KEY_P1_ACCEL;
			if (ctrlHeld)
				lastInput |= KEY_P1_BOOST;
		}
		break;

	case PlatformKey::Down:
		if (g_gameMode == TRACK_MENU)
			NextSceneryType();
		else
			lastInput |= KEY_P1_BRAKE;
		break;

	case PlatformKey::Space:
	case 'S':
	case 's':
		if (g_gameMode == GAME_IN_PROGRESS)
			bOutsideView = !bOutsideView;
		if (g_gameMode == TRACK_PREVIEW && g_trackState.TrackID != NO_TRACK)
			StartGame();
		if (g_gameMode == TRACK_MENU)
			SelectTrack();
		break;
	}
}

void AppHandleKeyUp(const uint32_t nChar)
{
	switch (nChar)
	{
	case PlatformKey::Left:
		lastInput &= ~KEY_P1_LEFT;
		break;

	case PlatformKey::Right:
		lastInput &= ~KEY_P1_RIGHT;
		break;

	case PlatformKey::Control:
		ctrlHeld = false;
		lastInput &= ~KEY_P1_BOOST;
		break;

	case PlatformKey::Up:
		lastInput &= ~(KEY_P1_ACCEL | KEY_P1_BOOST);
		break;

	case PlatformKey::Down:
		lastInput &= ~KEY_P1_BRAKE;
		break;
	}
}

void AppResetInput()
{
	lastInput = 0;
	ctrlHeld = false;
}

// Sound buffer setup — creates all game sound buffers using platform API
bool SetupSoundBuffers(SoundState& s)
{
	if ((s.WreckSoundBuffer = PlatformCreateSoundBuffer(L"WRECK")) == nullptr)
		return false;
	PlatformSoundSetPan(s.WreckSoundBuffer, PAN_RIGHT);
	PlatformSoundSetVolume(s.WreckSoundBuffer, 64);

	if ((s.HitCarSoundBuffer = PlatformCreateSoundBuffer(L"HITCAR")) == nullptr)
		return false;
	PlatformSoundSetFrequency(s.HitCarSoundBuffer, AMIGA_PAL_HZ / 238);
	PlatformSoundSetPan(s.HitCarSoundBuffer, PAN_RIGHT);
	PlatformSoundSetVolume(s.HitCarSoundBuffer, 56);

	if ((s.GroundedSoundBuffer = PlatformCreateSoundBuffer(L"GROUNDED")) == nullptr)
		return false;
	PlatformSoundSetFrequency(s.GroundedSoundBuffer, AMIGA_PAL_HZ / 400);
	PlatformSoundSetPan(s.GroundedSoundBuffer, PAN_RIGHT);

	if ((s.CreakSoundBuffer = PlatformCreateSoundBuffer(L"CREAK")) == nullptr)
		return false;
	PlatformSoundSetFrequency(s.CreakSoundBuffer, AMIGA_PAL_HZ / 238);
	PlatformSoundSetPan(s.CreakSoundBuffer, PAN_RIGHT);
	PlatformSoundSetVolume(s.CreakSoundBuffer, 64);

	if ((s.SmashSoundBuffer = PlatformCreateSoundBuffer(L"SMASH")) == nullptr)
		return false;
	PlatformSoundSetFrequency(s.SmashSoundBuffer, AMIGA_PAL_HZ / 280);
	PlatformSoundSetPan(s.SmashSoundBuffer, PAN_LEFT);
	PlatformSoundSetVolume(s.SmashSoundBuffer, 64);

	if ((s.OffRoadSoundBuffer = PlatformCreateSoundBuffer(L"OFFROAD")) == nullptr)
		return false;
	PlatformSoundSetPan(s.OffRoadSoundBuffer, PAN_RIGHT);
	PlatformSoundSetVolume(s.OffRoadSoundBuffer, 64);

	if ((s.EngineSoundBuffers[0] = PlatformCreateSoundBuffer(L"TICKOVER")) == nullptr)
		return false;
	static const wchar_t* engineNames[] = {
		nullptr, L"ENGINEPITCH2", L"ENGINEPITCH3", L"ENGINEPITCH4",
		L"ENGINEPITCH5", L"ENGINEPITCH6", L"ENGINEPITCH7", L"ENGINEPITCH8"
	};
	for (int i = 1; i < 8; i++)
	{
		if ((s.EngineSoundBuffers[i] = PlatformCreateSoundBuffer(engineNames[i])) == nullptr)
			return false;
	}

	for (int i = 0; i < 8; i++)
	{
		PlatformSoundSetPan(s.EngineSoundBuffers[i], PAN_LEFT);
		PlatformSoundSetVolume(s.EngineSoundBuffers[i], 48 / 2);
	}

	return true;
}

void DestroySoundBuffers(SoundState& s)
{
	// Release all sound buffers by stopping them - DirectSound
	// resources are freed when PlatformSoundShutdown releases the device

	PlatformDeleteSoundBuffer(s.WreckSoundBuffer);
	PlatformDeleteSoundBuffer(s.HitCarSoundBuffer);
	PlatformDeleteSoundBuffer(s.GroundedSoundBuffer);
	PlatformDeleteSoundBuffer(s.CreakSoundBuffer);
	PlatformDeleteSoundBuffer(s.SmashSoundBuffer);
	PlatformDeleteSoundBuffer(s.OffRoadSoundBuffer);
	for (int i = 0; i < 8; i++)
		PlatformDeleteSoundBuffer(s.EngineSoundBuffers[i]);
}

bool AppInit()
{
	if (!g_renderer.Init(WINDOW_WIDTH, WINDOW_HEIGHT))
	{
		PlatformShowError(L"Failed to initialize renderer", L"Error");
		return false;
	}

	// Build menu definition and create menus

	auto sep = [] { return MenuCommand{}; };

	auto isMenu = [] { return g_gameMode == TRACK_MENU; };
	auto isPreview = [] { return g_gameMode == TRACK_PREVIEW; };
	auto isGame = [] { return g_gameMode == GAME_IN_PROGRESS; };
	auto isOver = [] { return g_gameMode == GAME_OVER; };
	auto hasTrack = [] { return g_trackState.TrackID != NO_TRACK; };

	// --- Game menu ---
	MenuCommand gameMenu;
	gameMenu.text = L"&Game";
	gameMenu.children = {
		{
			L"&Start Race\tS", IDM_START_RACE,
			[]
			{
				if (g_gameMode == TRACK_MENU && g_trackState.TrackID != NO_TRACK)
				{
					g_gameState.bNewGame = true;
					ResetPlayer(g_gameState);
					g_gameMode = TRACK_PREVIEW;
					bPlayerPaused = bOpponentPaused = false;
				}
				else if (g_gameMode == TRACK_PREVIEW)
				{
					g_gameState.bNewGame = true;
					g_gameMode = GAME_IN_PROGRESS;
					ResetLapData(g_gameState, OPPONENT);
					ResetLapData(g_gameState, PLAYER);
					gameStartTime = PlatformGetTime();
					gameEndTime = 0;
					g_gameState.boostReserve = g_trackState.StandardBoost;
					g_gameState.boostUnit = 0;
					bPlayerPaused = bOpponentPaused = false;
				}
			},
			[=] { return (isMenu() && hasTrack()) || isPreview(); }
		},
		{
			L"Back to &Menu\tEsc", IDM_BACK_TO_MENU,
			[]
			{
				if (g_gameMode != TRACK_MENU)
				{
					g_gameMode = TRACK_MENU;
					g_gameState.opponentsID = NO_OPPONENT;
					ResetDrawBridge(g_trackState, g_gameState);
				}
			},
			[=] { return !isMenu(); }
		},
		sep(),
		{
			L"&Pause\tP", IDM_PAUSE,
			[] { bPaused = true; },
			[=] { return (isGame() || isPreview()) && !bPaused; }
		},
		{
			L"R&esume\tO", IDM_RESUME,
			[] { bPaused = false; },
			[] { return bPaused; }
		},
		sep(),
		{
			L"&Reverse Car\tR", IDM_REVERSE_CAR,
			[]
			{
				if (g_gameMode == GAME_IN_PROGRESS)
				{
					player1_y_angle = WrapAngle(player1_y_angle + _180_DEGREES);
					g_gameState.INITIALISE_PLAYER = true;
				}
			},
			[=] { return isGame(); }
		},
		sep(),
		{
			L"E&xit", IDM_EXIT,
			[] { PlatformClose(); }
		},
	};

	// --- Track menu ---
	MenuCommand trackMenu;
	trackMenu.text = L"&Track";
	for (int i = 0; i < NUM_TRACKS; i++)
	{
		int trackIdx = i;
		trackMenu.children.push_back(
			{
				GetTrackName(i), IDM_TRACK_FIRST + i,
				[trackIdx]
				{
					if (g_gameMode == TRACK_MENU)
					{
						if (!ConvertAmigaTrack(g_trackState, trackIdx))
						{
							PlatformShowError(L"Failed to convert track", L"Error");
							return;
						}
						CreateTrackVertexBuffer(g_trackState);
					}
				},
				[=] { return isMenu(); },
				[trackIdx] { return g_trackState.TrackID == trackIdx; }
			}
		);
	}

	// --- View menu ---
	MenuCommand viewMenu;
	viewMenu.text = L"&View";
	viewMenu.children = {
		{
			L"&Outside Camera", IDM_OUTSIDE_VIEW,
			[] { bOutsideView = !bOutsideView; },
			[=] { return isGame(); },
			[] { return bOutsideView; }
		},
		{
			L"Show S&tats\tF5", IDM_SHOW_STATS,
			[] { bShowStats = !bShowStats; },
			nullptr,
			[] { return bShowStats; }
		},
		sep(),
	};
	static std::wstring_view sceneryNames[] = {
		L"Scenery &1"sv, L"Scenery &2"sv, L"Scenery &3"sv, L"Scenery &4"sv, L"Scenery &5"sv
	};
	for (int i = 0; i < 5; i++)
	{
		int idx = i;
		viewMenu.children.push_back(
			{
				std::wstring(sceneryNames[i]), IDM_SCENERY_FIRST + i,
				[idx] { SetSceneryType(idx); },
				nullptr,
				[idx] { return GetSceneryType() == idx; }
			}
		);
	}

	// --- Speed menu ---
	MenuCommand speedMenu;
	speedMenu.text = L"S&peed";
	speedMenu.children = {
		{
			L"&Increase Speed\tF9", IDM_SPEED_INCREASE,
			[] { if (frameGap > 1) frameGap--; },
			[] { return frameGap > 1; }
		},
		{
			L"&Decrease Speed\tF10", IDM_SPEED_DECREASE,
			[] { frameGap++; }
		},
	};

	// --- Debug menu ---
	MenuCommand debugMenu;
	debugMenu.text = L"&Debug";
	debugMenu.children = {
		{
			L"Pause &Player\tF6", IDM_PAUSE_PLAYER,
			[] { bPlayerPaused = !bPlayerPaused; },
			nullptr,
			[] { return bPlayerPaused; }
		},
		{
			L"Pause &Opponent\tF7", IDM_PAUSE_OPPONENT,
			[] { bOpponentPaused = !bOpponentPaused; },
			nullptr,
			[] { return bOpponentPaused; }
		},
		sep(),
		{
			L"&Restart Race\tZ", IDM_RESTART_RACE,
			[] { g_gameState.bNewGame = true; },
			[=] { return isGame() || isOver(); }
		},
	};

	PlatformSetMenu({gameMenu, trackMenu, viewMenu, speedMenu, debugMenu});

	// Perform application initialization
	InitialiseData(g_trackState);

	// Create rendering resources
	CreateResources();

	// Initialise sound objects
	if (!PlatformSoundInit())
	{
		PlatformShowError(L"Failed to initialize DirectSound", L"Warning");
		// Continue without sound
	}
	else
	{
		if (!SetupSoundBuffers(g_soundState))
		{
			PlatformShowError(L"Failed to set up sound buffers", L"Warning");
		}
	}
	return true;
}

void AppRun()
{
	double previousTime = PlatformGetTime();
	double simulationAccumulator = 0.0;

	while (PlatformEvents())
	{
		const double fTime = PlatformGetTime();
		const double elapsed = std::clamp(fTime - previousTime, 0.0, 0.25);
		previousTime = fTime;
		simulationAccumulator += elapsed;
		const double simulationStep = SIMULATION_STEP_SECONDS *
			(static_cast<double>(frameGap + 1) / (DEFAULT_FRAME_GAP + 1));

		int simulationSteps = 0;
		while (simulationAccumulator >= simulationStep && simulationSteps < 5)
		{
			OnFrameMove(fTime, g_trackState, g_gameState);
			simulationAccumulator -= simulationStep;
			++simulationSteps;
		}
		if (simulationSteps == 5)
			simulationAccumulator = std::min(simulationAccumulator, simulationStep);

		// Render frame
		OnFrameRender(g_trackState, g_gameMode, fTime);

		// Draw FPS in top-right corner
		const auto fpsText = format(L"FPS: %.1f", g_fps);
		g_renderer.DrawGameText(g_renderer.GetWidth() - 8 - static_cast<int>(fpsText.size()) * 8, 4,
		                        fpsText, XRGB(255, 255, 0));

		PlatformPresentFrame(g_renderer.GetPixels(), g_renderer.GetWidth(), g_renderer.GetHeight());

		// FPS tracking
		g_fpsFrameCount++;
		if (fTime - g_fpsTime >= 1.0)
		{
			g_fps = static_cast<double>(g_fpsFrameCount) / (fTime - g_fpsTime);
			g_fpsFrameCount = 0;
			g_fpsTime = fTime;
		}

		// Simple frame rate limiter (~60 fps)
		PlatformSleep(1);
	}

	// Cleanup
	FreeResources();
	FreeData(g_soundState, g_trackState);
}

void AppHandleFrameSize(const int cx, const int cy)
{
	// Fill the entire client area — never letterbox. Adjust the projection so we
	// always reveal *more* world than the original 4:3 view, never crop:
	//   - Wider than 4:3  → keep the reference vertical FOV; horizontal FOV grows.
	//   - Taller than 4:3 → keep the reference horizontal FOV; vertical FOV grows.
	const int renderW = std::max(cx, 64);
	const int renderH = std::max(cy, 64);
	constexpr double refAspect = static_cast<double>(WINDOW_WIDTH) / static_cast<double>(WINDOW_HEIGHT);
	constexpr double refFovY = SCR_PI / 4.0;

	const double aspect = std::clamp(static_cast<double>(renderW) / static_cast<double>(renderH), 0.5, 3.0);
	double fovY;
	if (aspect >= refAspect)
		fovY = refFovY;
	else
		fovY = 2.0 * atan(tan(refFovY * 0.5) * refAspect / aspect);

	g_renderer.Resize(renderW, renderH);
	g_renderer.SetProjectionMatrix(Mat4PerspectiveFovLH(fovY, aspect, 0.5f, FURTHEST_Z));
}

static double LockAngleRad(double opposite,
                           double adjacent,
                           bool clockwise);


// Calculate rotation coefficients for Y, X, Z rotation order.
// Uses anti-clockwise rotation convention.
RotationMatrix CalcYXZTrigCoefficients(const double x_angle,
                                       const double y_angle,
                                       const double z_angle)
{
	const double rx = x_angle * ANGLE_TO_RADIANS;
	const double ry = y_angle * ANGLE_TO_RADIANS;
	const double rz = z_angle * ANGLE_TO_RADIANS;
	const double sin_x = std::sin(rx);
	const double cos_x = std::cos(rx);
	const double sin_y = std::sin(ry);
	const double cos_y = std::cos(ry);
	const double sin_z = std::sin(rz);
	const double cos_z = std::cos(rz);

	RotationMatrix rot;

	// Rotated x coefficients
	rot.coeffs[X_X_COMP] = cos_y * cos_z + sin_x * sin_y * sin_z;
	rot.coeffs[X_Y_COMP] = -(cos_x * sin_z);
	rot.coeffs[X_Z_COMP] = -(sin_y * cos_z) + sin_x * cos_y * sin_z;

	// Rotated y coefficients
	rot.coeffs[Y_X_COMP] = cos_y * sin_z - sin_x * sin_y * cos_z;
	rot.coeffs[Y_Y_COMP] = cos_x * cos_z;
	rot.coeffs[Y_Z_COMP] = -(sin_y * sin_z) - sin_x * cos_y * cos_z;

	// Rotated z coefficients
	rot.coeffs[Z_X_COMP] = cos_x * sin_y;
	rot.coeffs[Z_Y_COMP] = sin_x;
	rot.coeffs[Z_Z_COMP] = cos_x * cos_y;

	return rot;
}


// Transform local-space vector to world-space using rotation coefficients.
// Returns render-space world units (matching the viewpoint pipeline).
WorldVec3 WorldOffset(const RotationMatrix& rot,
                      const double x,
                      const double y,
                      const double z)
{
	WorldVec3 result;

	result.x = x * rot[X_X_COMP] + y * rot[Y_X_COMP] + z * rot[Z_X_COMP];
	result.y = x * rot[X_Y_COMP] + y * rot[Y_Y_COMP] + z * rot[Z_Y_COMP];
	result.z = x * rot[X_Z_COMP] + y * rot[Y_Z_COMP] + z * rot[Z_Z_COMP];

	return result;
}


// Calculate x/y angles to point viewpoint toward target. Returns double radians.
ViewAngle LockViewpointToTarget(const double viewpoint_x,
                                const double viewpoint_y,
                                const double viewpoint_z,
                                const double tgt_x,
                                const double tgt_y,
                                const double tgt_z)
{
	ViewAngle result;

	// y angle: yaw between viewpoint and target in the XZ plane.
	const double dx = tgt_x - viewpoint_x;
	const double dz = tgt_z - viewpoint_z;
	result.y_angle = LockAngleRad(dx, dz, false);

	// x angle: pitch using horizontal distance as the adjacent. atan2 is
	// scale-invariant, so we don't need to apply any unit normalisation.
	const double horiz = sqrt(dx * dx + dz * dz);
	const double dy = tgt_y - viewpoint_y;
	result.x_angle = LockAngleRad(dy, horiz, false);

	return result;
}


static double LockAngleRad(const double opposite,
                           const double adjacent,
                           const bool clockwise)
{
	// atan2 handles all four quadrants and (0,0) (returns 0).
	double radians = std::atan2(opposite, adjacent);
	if (clockwise) radians = -radians;
	return radians;
}


constexpr int32_t MAX_VERTICES_PER_CAR = 142 * 3;

static SWVertex* pCarVertices = nullptr;
static int32_t numCarVertices = 0;


static void StoreCarTriangle(const COORD_3D* c1, const COORD_3D* c2, const COORD_3D* c3, SWVertex* pVertices,
                             const uint32_t colour)
{
	if (numCarVertices + 3 > MAX_VERTICES_PER_CAR)
	{
		PlatformShowError(L"Exceeded numCarVertices", L"StoreCarTriangle");
		return;
	}

	const Vec3 v1(c1->x, c1->y, c1->z);
	const Vec3 v2(c2->x, c2->y, c2->z);
	const Vec3 v3(c3->x, c3->y, c3->z);

	pVertices[numCarVertices].pos = v1;
	pVertices[numCarVertices].color = colour;
	pVertices[numCarVertices].tu = 0;
	pVertices[numCarVertices].tv = 0;
	++numCarVertices;

	pVertices[numCarVertices].pos = v2;
	pVertices[numCarVertices].color = colour;
	pVertices[numCarVertices].tu = 0;
	pVertices[numCarVertices].tv = 0;
	++numCarVertices;

	pVertices[numCarVertices].pos = v3;
	pVertices[numCarVertices].color = colour;
	pVertices[numCarVertices].tu = 0;
	pVertices[numCarVertices].tv = 0;
	++numCarVertices;
}

static void StoreCarQuad(const COORD_3D& c0, const COORD_3D& c1, const COORD_3D& c2, const COORD_3D& c3,
						 SWVertex* pVertices, const uint32_t colour)
{
	StoreCarTriangle(&c0, &c1, &c2, pVertices, colour);
	StoreCarTriangle(&c0, &c2, &c3, pVertices, colour);
}

static void StoreWheel(const double x0, const double x1, const double z,
					   SWVertex* pVertices, const bool hubAtX0)
{
	constexpr int sides = 6;
	constexpr double centreY = -VCAR_HEIGHT / 8.0;
	constexpr double sqrtThree = 1.7320508075688772;
	constexpr double verticalRadius = VCAR_HEIGHT / (4.0 * sqrtThree);
	constexpr double longitudinalRadius = 24.0;
	const uint32_t tire = SCRGB(SCR_BASE_COLOUR + 0);
	COORD_3D inner[sides];
	COORD_3D outer[sides];
	for (int side = 0; side < sides; ++side)
	{
		const double angle = side * 2.0 * PI / sides;
		const double y = centreY + std::sin(angle) * verticalRadius;
		const double wheelZ = z + std::cos(angle) * longitudinalRadius;
		inner[side] = {x0, y, wheelZ};
		outer[side] = {x1, y, wheelZ};
	}
	for (int side = 0; side < sides; ++side)
	{
		const int next = (side + 1) % sides;
		StoreCarQuad(inner[side], inner[next], outer[next], outer[side], pVertices, tire);
	}
	for (int side = 1; side < sides - 1; ++side)
	{
		StoreCarTriangle(&inner[0], &inner[side + 1], &inner[side], pVertices, tire);
		StoreCarTriangle(&outer[0], &outer[side], &outer[side + 1], pVertices, tire);
	}

	// A smaller hex on the outward face reads as a hub at low resolution.
	const double hubX = hubAtX0 ? x0 - 0.5 : x1 + 0.5;
	const uint32_t hub = SCRGB(SCR_BASE_COLOUR + 7);
	COORD_3D hubVertices[sides];
	for (int side = 0; side < sides; ++side)
	{
		const double angle = side * 2.0 * PI / sides;
		hubVertices[side] = {
			hubX,
			centreY + std::sin(angle) * verticalRadius * 0.48,
			z + std::cos(angle) * longitudinalRadius * 0.48
		};
	}
	for (int side = 1; side < sides - 1; ++side)
	{
		if (hubAtX0)
			StoreCarTriangle(&hubVertices[0], &hubVertices[side + 1], &hubVertices[side], pVertices, hub);
		else
			StoreCarTriangle(&hubVertices[0], &hubVertices[side], &hubVertices[side + 1], pVertices, hub);
	}
}


static void CreateCarInVB(SWVertex* pVertices)
{
	// car co-ordinates
	static COORD_3D car[16 + 8] = {
		//x,				y,					z
		{-VCAR_WIDTH / 2, -VCAR_HEIGHT / 4, -VCAR_LENGTH / 2}, // rear left wheel
		{-VCAR_WIDTH / 2, 0, -VCAR_LENGTH / 2},
		{-VCAR_WIDTH / 4, 0, -VCAR_LENGTH / 2},
		{-VCAR_WIDTH / 4, -VCAR_HEIGHT / 4, -VCAR_LENGTH / 2},

		{VCAR_WIDTH / 4, -VCAR_HEIGHT / 4, -VCAR_LENGTH / 2}, // rear right wheel
		{VCAR_WIDTH / 4, 0, -VCAR_LENGTH / 2},
		{VCAR_WIDTH / 2, 0, -VCAR_LENGTH / 2},
		{VCAR_WIDTH / 2, -VCAR_HEIGHT / 4, -VCAR_LENGTH / 2},

		{-VCAR_WIDTH / 2, -VCAR_HEIGHT / 4, VCAR_LENGTH / 2}, // front left wheel
		{-VCAR_WIDTH / 2, 0, VCAR_LENGTH / 2},
		{-VCAR_WIDTH / 4, 0, VCAR_LENGTH / 2},
		{-VCAR_WIDTH / 4, -VCAR_HEIGHT / 4, VCAR_LENGTH / 2},

		{VCAR_WIDTH / 4, -VCAR_HEIGHT / 4, VCAR_LENGTH / 2}, // front right wheel
		{VCAR_WIDTH / 4, 0, VCAR_LENGTH / 2},
		{VCAR_WIDTH / 2, 0, VCAR_LENGTH / 2},
		{VCAR_WIDTH / 2, -VCAR_HEIGHT / 4, VCAR_LENGTH / 2},

		{-VCAR_WIDTH / 4, -VCAR_HEIGHT / 8, -VCAR_LENGTH / 2}, // car rear points
		{-(3 * VCAR_WIDTH) / 16, VCAR_HEIGHT / 4, -VCAR_LENGTH / 2},
		{3 * VCAR_WIDTH / 16, VCAR_HEIGHT / 4, -VCAR_LENGTH / 2},
		{VCAR_WIDTH / 4, -VCAR_HEIGHT / 8, -VCAR_LENGTH / 2},

		{-VCAR_WIDTH / 4, -VCAR_HEIGHT / 8, VCAR_LENGTH / 2}, // car front points
		{-VCAR_WIDTH / 4, 0, VCAR_LENGTH / 2},
		{VCAR_WIDTH / 4, 0, VCAR_LENGTH / 2},
		{VCAR_WIDTH / 4, -VCAR_HEIGHT / 8, VCAR_LENGTH / 2}
	};

	// rear left wheel
	StoreWheel(-VCAR_WIDTH / 2.0, -VCAR_WIDTH / 4.0, -VCAR_LENGTH / 2.0, pVertices, true);
	StoreWheel(VCAR_WIDTH / 4.0, VCAR_WIDTH / 2.0, -VCAR_LENGTH / 2.0, pVertices, false);
	StoreWheel(-VCAR_WIDTH / 2.0, -VCAR_WIDTH / 4.0, VCAR_LENGTH / 2.0, pVertices, true);
	StoreWheel(VCAR_WIDTH / 4.0, VCAR_WIDTH / 2.0, VCAR_LENGTH / 2.0, pVertices, false);

	// car left side
	uint32_t colour = SCRGB(SCR_BASE_COLOUR + 12);
	StoreCarTriangle(&car[4 + 16], &car[5 + 16], &car[1 + 16], pVertices, colour);
	StoreCarTriangle(&car[4 + 16], &car[1 + 16], &car[0 + 16], pVertices, colour);
	// car right side
	StoreCarTriangle(&car[3 + 16], &car[2 + 16], &car[6 + 16], pVertices, colour);
	StoreCarTriangle(&car[3 + 16], &car[6 + 16], &car[7 + 16], pVertices, colour);

	// car back
	colour = SCRGB(SCR_BASE_COLOUR + 10);
	StoreCarTriangle(&car[0 + 16], &car[1 + 16], &car[2 + 16], pVertices, colour);
	StoreCarTriangle(&car[0 + 16], &car[2 + 16], &car[3 + 16], pVertices, colour);
	// car front
	StoreCarTriangle(&car[7 + 16], &car[6 + 16], &car[5 + 16], pVertices, colour);
	StoreCarTriangle(&car[7 + 16], &car[5 + 16], &car[4 + 16], pVertices, colour);

	// car top
	colour = SCRGB(SCR_BASE_COLOUR + 15);
	StoreCarTriangle(&car[1 + 16], &car[5 + 16], &car[6 + 16], pVertices, colour);
	StoreCarTriangle(&car[1 + 16], &car[6 + 16], &car[2 + 16], pVertices, colour);
	// car bottom
	colour = SCRGB(SCR_BASE_COLOUR + 9);
	StoreCarTriangle(&car[3 + 16], &car[7 + 16], &car[4 + 16], pVertices, colour);
	StoreCarTriangle(&car[3 + 16], &car[4 + 16], &car[0 + 16], pVertices, colour);

	// Raised cockpit: a compact contrasting cabin breaks up the original wedge.
	const COORD_3D cockpit[8] = {
		{-24, 30, -42}, {24, 30, -42}, {-24, 30, 48}, {24, 30, 48},
		{-15, 64, -24}, {15, 64, -24}, {-15, 58, 30}, {15, 58, 30}
	};
	colour = SCRGB(SCR_BASE_COLOUR + 6);
	StoreCarQuad(cockpit[0], cockpit[4], cockpit[6], cockpit[2], pVertices, colour);
	StoreCarQuad(cockpit[1], cockpit[3], cockpit[7], cockpit[5], pVertices, colour);
	StoreCarQuad(cockpit[0], cockpit[1], cockpit[5], cockpit[4], pVertices, colour);
	StoreCarQuad(cockpit[2], cockpit[6], cockpit[7], cockpit[3], pVertices, colour);
	colour = SCRGB(SCR_BASE_COLOUR + 15);
	StoreCarQuad(cockpit[4], cockpit[5], cockpit[7], cockpit[6], pVertices, colour);
}


void CreateCarVertexBuffer(void)
{
	if (pCarVertices == nullptr)
	{
		pCarVertices = new SWVertex[MAX_VERTICES_PER_CAR];
	}

	numCarVertices = 0;
	CreateCarInVB(pCarVertices);
}


void FreeCarVertexBuffer(void)
{
	delete[] pCarVertices;
	pCarVertices = nullptr;
}


void DrawCar(SoftwareRenderer& r)
{
	if (!pCarVertices || numCarVertices < 3) return;

	r.DrawTriangleList(pCarVertices, 0, numCarVertices / 3, nullptr);
}

static void DrawPlayerCar(SoftwareRenderer& r)
{
	if (!pCarVertices || numCarVertices < 3) return;

	constexpr int wheelTriangles = 24;
	constexpr int wheelVertices = wheelTriangles * 3;
	for (int wheel = 0; wheel < 4; ++wheel)
	{
		r.SetWorldMatrix(matWorldCarWheels[wheel]);
		r.DrawTriangleList(pCarVertices, wheel * wheelVertices, wheelTriangles, nullptr);
	}

	r.SetWorldMatrix(matWorldCar);
	r.DrawTriangleList(pCarVertices, 4 * wheelVertices, (numCarVertices - 4 * wheelVertices) / 3, nullptr);
}
