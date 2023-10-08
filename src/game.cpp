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

#include <ctime>

#include "platform.h"
#include "game.h"
#include "render.software.h"

using namespace std::string_view_literals;


constexpr int32_t DEFAULT_FRAME_GAP = 4;

constexpr int32_t HEIGHT_ABOVE_ROAD = 100;

GameState g_gameState;
TrackState g_trackState;
SoundState g_soundState;

GameModeType g_gameMode = TRACK_MENU;

std::vector<SWTexture> g_roadTexture;

SoftwareRenderer g_renderer;

uint32_t lastInput = 0;
static bool ctrlHeld = false;

int32_t frameGap = DEFAULT_FRAME_GAP;
static bool bFrameMoved = false;

bool bShowStats = false;
bool bPaused = false;
bool bPlayerPaused = false;
bool bOpponentPaused = false;
bool bOutsideView = false;
double gameStartTime, gameEndTime;

double g_fpsTime = 0.0;
int g_fpsFrameCount = 0;
float g_fps = 0.0f;

static int32_t player1_x = 0, player1_y = 0, player1_z = 0;
static int32_t player1_x_angle = 0;
int32_t player1_y_angle = 0;
static int32_t player1_z_angle = 0;

static int32_t opponent_x = 0, opponent_y = 0, opponent_z = 0;
static float opponent_x_angle = 0.0f, opponent_y_angle = 0.0f, opponent_z_angle = 0.0f;

static int32_t viewpoint1_x, viewpoint1_y, viewpoint1_z;
static int32_t viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle;
static int32_t target_x, target_y, target_z;

void InitialiseData(TrackState& t)
{
	CreateSinCosTable();
	ConvertAmigaTrack(t, LITTLE_RAMP);
	srand(static_cast<unsigned>(std::time(nullptr)));
}

void FreeData(SoundState& s, const TrackState& t)
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
	for (const auto name : roadTexNames)
	{
		const auto bmp = PlatformLoadBitmapResource(name);
		if (bmp)
			g_roadTexture.emplace_back(SWTexture{bmp->pixels, bmp->width, bmp->height});
	}

	// Set projection transform
	const float fAspect = static_cast<float>(g_renderer.GetWidth()) / static_cast<float>(g_renderer.GetHeight());
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
	static int32_t circle_y_angle = 0;

	constexpr int32_t centre = NUM_TRACK_CUBES * CUBE_SIZE / 2;
	constexpr int32_t radius = (NUM_TRACK_CUBES - 2) * CUBE_SIZE / PRECISION;

	target_x = NUM_TRACK_CUBES * CUBE_SIZE / 2;
	target_y = 0;
	target_z = NUM_TRACK_CUBES * CUBE_SIZE / 2;

	// Orbit camera around the track
	if (!bPaused) circle_y_angle += 128;
	circle_y_angle &= MAX_ANGLE - 1;

	auto [sin, cos] = GetSinCos(circle_y_angle);

	viewpoint1_x = centre + sin * radius;
	viewpoint1_y = -CUBE_SIZE * 3;
	viewpoint1_z = centre + cos * radius;

	const auto trackMenuView = LockViewpointToTarget(viewpoint1_x,
	                                                 viewpoint1_y,
	                                                 viewpoint1_z,
	                                                 target_x,
	                                                 target_y,
	                                                 target_z);
	viewpoint1_x_angle = trackMenuView.x_angle;
	viewpoint1_y_angle = trackMenuView.y_angle;
	viewpoint1_z_angle = 0;
}

static void CalcTrackPreviewViewpoint(const TrackState& t)
{
	target_x = opponent_x;
	target_y = opponent_y;
	target_z = opponent_z;

	constexpr int32_t centre = NUM_TRACK_CUBES * CUBE_SIZE / 2;

	viewpoint1_x = centre;

	if (t.TrackID == DRAW_BRIDGE)
		viewpoint1_y = opponent_y - CUBE_SIZE * 5 / 2;
	else
		viewpoint1_y = opponent_y - CUBE_SIZE / 2;

	viewpoint1_z = centre;

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
	viewpoint1_z_angle = 0;
}

static void CalcGameViewpoint()
{
	if (bOutsideView)
	{
		const auto rot = CalcYXZTrigCoefficients(player1_x_angle,
		                                         player1_y_angle,
		                                         player1_z_angle);

		const auto offset = WorldOffset(rot, 0, 0xc0, 0x300);
		viewpoint1_x = player1_x - offset.x;
		viewpoint1_y = player1_y - offset.y;
		viewpoint1_z = player1_z - offset.z;

		viewpoint1_x_angle = player1_x_angle;
		viewpoint1_y_angle = player1_y_angle;
		viewpoint1_z_angle = player1_z_angle;
	}
	else
	{
		viewpoint1_x = player1_x;
		viewpoint1_y = player1_y - (HEIGHT_ABOVE_ROAD << LOG_PRECISION);
		viewpoint1_z = player1_z;

		viewpoint1_x_angle = player1_x_angle;
		viewpoint1_y_angle = player1_y_angle;
		viewpoint1_z_angle = player1_z_angle;
	}
}

static Mat4 matWorldTrack, matWorldCar, matWorldOpponentsCar;

static void SetCarWorldTransform()
{
	Mat4 matRot = Mat4::Identity();
	const float xa = static_cast<float>(player1_x_angle) * 2 * SCR_PI / 65536.0f;
	const float ya = static_cast<float>(player1_y_angle) * 2 * SCR_PI / 65536.0f;
	const float za = static_cast<float>(player1_z_angle) * 2 * SCR_PI / 65536.0f;
	Mat4 matTemp = Mat4RotationZ(za);
	matRot = Mat4Multiply(matRot, matTemp);
	matTemp = Mat4RotationX(xa);
	matRot = Mat4Multiply(matRot, matTemp);
	matTemp = Mat4RotationY(ya);
	matRot = Mat4Multiply(matRot, matTemp);
	const Mat4 matTrans = Mat4Translation(static_cast<float>(player1_x >> LOG_PRECISION),
	                                      static_cast<float>(-player1_y >> LOG_PRECISION) + VCAR_HEIGHT * 3 / 8,
	                                      static_cast<float>(player1_z >> LOG_PRECISION));
	matWorldCar = Mat4Multiply(matRot, matTrans);
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
	const Mat4 matTrans = Mat4Translation(static_cast<float>(opponent_x >> LOG_PRECISION),
	                                      static_cast<float>(-opponent_y >> LOG_PRECISION) + VCAR_HEIGHT / 4,
	                                      static_cast<float>(opponent_z >> LOG_PRECISION));
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
	static int32_t frameCount = 0;
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

	// Track preview and game mode run at reduced frame rate
	if (g_gameMode == TRACK_PREVIEW || g_gameMode == GAME_IN_PROGRESS)
	{
		if (g_gameMode == GAME_IN_PROGRESS)
		{
			// Following function should run at 50Hz
			if (!bPaused) FramesWheelsEngine(g_soundState, g_soundState.EngineSoundBuffers);
		}

		if (frameCount > 0)
			--frameCount;

		if (frameCount == 0)
		{
			frameCount = frameGap;
		}
		else
		{
			return;
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

		// Prepare transforms for rendering
		viewpoint1_x >>= LOG_PRECISION;
		viewpoint1_z >>= LOG_PRECISION;

		target_x >>= LOG_PRECISION;
		target_y = -target_y;
		target_y >>= LOG_PRECISION;
		target_z >>= LOG_PRECISION;

		// Set the track's world transform matrix
		matWorldTrack = Mat4::Identity();

		// Set the view transform matrix using LookAt
		const Vec3 vUpVec(0.0f, 1.0f, 0.0f);
		const Vec3 vEyePt(static_cast<float>(viewpoint1_x), static_cast<float>(-viewpoint1_y >> LOG_PRECISION),
		                  static_cast<float>(viewpoint1_z));
		const Vec3 vLookatPt(static_cast<float>(target_x), static_cast<float>(target_y), static_cast<float>(target_z));
		const Mat4 matView = Mat4LookAtLH(vEyePt, vLookatPt, vUpVec);
		g_renderer.SetViewMatrix(matView);
	}
	else if (g_gameMode == GAME_IN_PROGRESS)
	{
		CalcGameViewpoint();

		viewpoint1_x >>= LOG_PRECISION;
		viewpoint1_z >>= LOG_PRECISION;

		matWorldTrack = Mat4::Identity();

		SetOpponentsCarWorldTransform();

		if (bOutsideView)
		{
			SetCarWorldTransform();
		}

		// Build view matrix manually: translate then rotate
		const Mat4 matTrans = Mat4Translation(static_cast<float>(-viewpoint1_x),
		                                      static_cast<float>(viewpoint1_y >> LOG_PRECISION),
		                                      static_cast<float>(-viewpoint1_z));
		Mat4 matRot = Mat4::Identity();
		const float xa = static_cast<float>(-viewpoint1_x_angle) * 2 * SCR_PI / 65536.0f;
		const float ya = static_cast<float>(-viewpoint1_y_angle) * 2 * SCR_PI / 65536.0f;
		const float za = static_cast<float>(-viewpoint1_z_angle) * 2 * SCR_PI / 65536.0f;
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
	const int screenH = g_renderer.GetHeight();
	constexpr uint32_t textColor = XRGB(255, 255, 0);

	if (bShowStats)
	{
		g_renderer.DrawGameText(2, 0, L"Version 1.0", textColor);
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

			// Output opponent's name for four seconds at race start
			if (PlatformGetTime() - gameStartTime < 4.0 && game.opponentsID != NO_OPPONENT)
			{
				const auto op = format(L"Opponent: %s", GetOpponentName(game.opponentsID));
				g_renderer.DrawTextLargeCentered(10, op, textColor);
			}
			if (game.lapNumber[PLAYER] > 0)
				swprintf_s(lapText, 3, L"%d", game.lapNumber[PLAYER]);
			g_renderer.DrawGameText(2, screenH - 15 * 2, format(L"Lap: %s   Boost: %d", lapText,
			                                                    game.boostReserve), textColor);
			g_renderer.DrawGameText(2, screenH - 15 * 1, format(L"Opponent Distance: %d",
			                                                    CalculateOpponentsDistance(game)), textColor);
			g_renderer.DrawGameText(280, screenH - 15 * 2, format(L"Speed: %d", CalculateDisplaySpeed(g_gameState)),
			                        textColor);
			g_renderer.DrawGameText(280, screenH - 15 * 1, format(L"Damage: %d", game.new_damage), textColor);
			if (game.raceFinished)
			{
				const double currentTime = PlatformGetTime();
				if (gameEndTime == 0.0)
					gameEndTime = currentTime;

				const double diffTime = currentTime - gameEndTime;

				uint32_t bigTextColor;
				if (GameMode == GAME_OVER)
				{
					bigTextColor = XRGB(255, 255, 0);
					g_renderer.DrawGameText(124, screenH - 25 * 12,
					                        L"GAME OVER: Use Game menu to return to track menu",
					                        bigTextColor);
				}
				else
				{
					const int32_t intTime = static_cast<int32_t>(diffTime);
					if (diffTime - static_cast<double>(intTime) < 0.5)
						bigTextColor = XRGB(255, 255, 255);
					else
						bigTextColor = XRGB(0, 0, 0);

					if (game.raceWon)
						g_renderer.DrawGameText(250, screenH - 25 * 12, L"RACE WON", bigTextColor);
					else
						g_renderer.DrawGameText(250, screenH - 25 * 12, L"RACE LOST", bigTextColor);
				}
			}
		}
		break;
	}
}


void OnFrameRender(const TrackState& t, const GameModeType GameMode, const double fTime)
{
	g_renderer.ClearDepth();

	//g_renderer.SetDepthTestEnabled(false);
	//g_renderer.SetCullMode(SoftwareRenderer::CULL_NONE);

	// Draw Backdrop
	DrawBackdrop(g_renderer, viewpoint1_y, viewpoint1_x_angle, viewpoint1_y_angle, viewpoint1_z_angle);

	// Enable lighting for 3D geometry
	g_renderer.SetLightingEnabled(true);

	// Draw Track
	g_renderer.SetWorldMatrix(matWorldTrack);
	DrawTrack(t, GameMode, g_renderer, g_gameState.player_current_piece,
	          g_gameState.player_current_segment, g_roadTexture, !g_gameState.drop_start_done);

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
			g_renderer.SetWorldMatrix(matWorldCar);
			DrawCar(g_renderer);
		}
		break;
	}

	// Disable lighting for UI / 2D overlays
	g_renderer.SetLightingEnabled(false);

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

void AppInit()
{
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
					player1_y_angle += _180_DEGREES;
					player1_y_angle &= MAX_ANGLE - 1;
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
}

void AppRun()
{
	// Create software renderer
	if (!g_renderer.Init(WINDOW_WIDTH, WINDOW_HEIGHT))
	{
		PlatformShowError(L"Failed to initialize renderer", L"Error");
		return;
	}

	while (PlatformEvents())
	{
		const double fTime = PlatformGetTime();

		// Update game logic
		OnFrameMove(fTime, g_trackState, g_gameState);

		// Render frame
		OnFrameRender(g_trackState, g_gameMode, fTime);

		// Draw FPS in top-right corner
		g_renderer.DrawGameText(g_renderer.GetWidth() - 90, 2, format(L"FPS: %.1f", g_fps), XRGB(255, 255, 0));

		PlatformPresentFrame(g_renderer.GetPixels(), g_renderer.GetWidth(), g_renderer.GetHeight());

		// FPS tracking
		g_fpsFrameCount++;
		if (fTime - g_fpsTime >= 1.0)
		{
			g_fps = static_cast<float>(g_fpsFrameCount) / static_cast<float>(fTime - g_fpsTime);
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
	constexpr float targetAspect = static_cast<float>(WINDOW_WIDTH) / static_cast<float>(WINDOW_HEIGHT);
	int renderW, renderH;
	if (static_cast<float>(cx) / static_cast<float>(cy) > targetAspect)
	{
		renderH = cy;
		renderW = static_cast<int>(cy * targetAspect);
	}
	else
	{
		renderW = cx;
		renderH = static_cast<int>(cx / targetAspect);
	}
	if (renderW > 0 && renderH > 0)
	{
		g_renderer.Resize(renderW, renderH);
		const float fAspect = static_cast<float>(renderW) / static_cast<float>(renderH);
		const Mat4 matProj = Mat4PerspectiveFovLH(SCR_PI / 4.0f, fAspect, 0.5f, FURTHEST_Z);
		g_renderer.SetProjectionMatrix(matProj);
	}
}

static constexpr int32_t SIN_COS_TABLE_SIZE = MAX_ANGLE + MAX_ANGLE / 4; // sine/cosine overlap

static int16_t Sin_Cos[SIN_COS_TABLE_SIZE];

static int32_t LockAngle(int32_t opposite,
                         int32_t adjacent,
                         int32_t clockwise);


void CreateSinCosTable()
{
	double angle = 0;
	constexpr double step = static_cast<double>(2) * PI / static_cast<double>(MAX_ANGLE);
	for (int32_t i = 0; i < SIN_COS_TABLE_SIZE; i++)
	{
		double value = sin(angle);
		value = value * static_cast<double>(PRECISION);

		Sin_Cos[i] = static_cast<int16_t>(value);
		angle += step;
	}
}


SinCos GetSinCos(const int32_t angle)
{
	return {Sin_Cos[angle], Sin_Cos[angle + MAX_ANGLE / 4]};
}


// Calculate rotation coefficients for Y, X, Z rotation order
// Uses anti-clockwise rotation convention
RotationMatrix CalcYXZTrigCoefficients(const int32_t x_angle,
                                       const int32_t y_angle,
                                       const int32_t z_angle)
{
	const int16_t sin_x = Sin_Cos[x_angle];
	const int16_t sin_y = Sin_Cos[y_angle];
	const int16_t sin_z = Sin_Cos[z_angle];

	const int16_t cos_x = Sin_Cos[x_angle + MAX_ANGLE / 4];
	const int16_t cos_y = Sin_Cos[y_angle + MAX_ANGLE / 4];
	const int16_t cos_z = Sin_Cos[z_angle + MAX_ANGLE / 4];

	RotationMatrix rot;

	// Rotated x coefficients
	rot.coeffs[X_X_COMP] = static_cast<int16_t>((cos_y * cos_z + sin_x * sin_y / PRECISION * sin_z) /
		PRECISION);
	rot.coeffs[X_Y_COMP] = static_cast<int16_t>(-(cos_x * sin_z) / PRECISION);
	rot.coeffs[X_Z_COMP] = static_cast<int16_t>((-(sin_y * cos_z) + sin_x * cos_y / PRECISION * sin_z) /
		PRECISION);

	// Rotated y coefficients
	rot.coeffs[Y_X_COMP] = static_cast<int16_t>((cos_y * sin_z - sin_x * sin_y / PRECISION * cos_z) /
		PRECISION);
	rot.coeffs[Y_Y_COMP] = static_cast<int16_t>(cos_x * cos_z / PRECISION);
	rot.coeffs[Y_Z_COMP] = static_cast<int16_t>((-(sin_y * sin_z) - sin_x * cos_y / PRECISION * cos_z) /
		PRECISION);

	// Rotated z coefficients
	rot.coeffs[Z_X_COMP] = static_cast<int16_t>(cos_x * sin_y / PRECISION);
	rot.coeffs[Z_Y_COMP] = sin_x;
	rot.coeffs[Z_Z_COMP] = static_cast<int16_t>(cos_x * cos_y / PRECISION);

	return rot;
}


// Transform local-space vector to world-space using rotation coefficients
COORD_3D WorldOffset(const RotationMatrix& rot,
                     const int32_t x,
                     const int32_t y,
                     const int32_t z)
{
	COORD_3D result;

	result.x = x * static_cast<int32_t>(rot[X_X_COMP]) +
		y * static_cast<int32_t>(rot[Y_X_COMP]) +
		z * static_cast<int32_t>(rot[Z_X_COMP]);

	result.y = x * static_cast<int32_t>(rot[X_Y_COMP]) +
		y * static_cast<int32_t>(rot[Y_Y_COMP]) +
		z * static_cast<int32_t>(rot[Z_Y_COMP]);

	result.z = x * static_cast<int32_t>(rot[X_Z_COMP]) +
		y * static_cast<int32_t>(rot[Y_Z_COMP]) +
		z * static_cast<int32_t>(rot[Z_Z_COMP]);

	return result;
}


// Calculate x/y angles to point viewpoint toward target
ViewAngle LockViewpointToTarget(const int32_t viewpoint_x,
                                const int32_t viewpoint_y,
                                const int32_t viewpoint_z,
                                const int32_t target_x,
                                const int32_t target_y,
                                const int32_t target_z)
{
	ViewAngle result;

	// y angle
	int32_t opp = target_x - viewpoint_x;
	int32_t adj = target_z - viewpoint_z;
	result.y_angle = LockAngle(opp, adj, false);

	// x angle
	const double a = (target_x - viewpoint_x) >> LOG_PRECISION;
	const double b = (target_z - viewpoint_z) >> LOG_PRECISION;
	const double h = sqrt(a * a + b * b);
	adj = static_cast<int32_t>(h * PRECISION);
	opp = target_y - viewpoint_y;
	result.x_angle = LockAngle(opp, adj, false);

	return result;
}


static int32_t LockAngle(const int32_t opposite,
                         const int32_t adjacent,
                         const int32_t clockwise)
{
	int32_t viewpoint_angle;
	double radians;

	const double o = opposite;
	const double a = adjacent;

	// use inverse tan to calculate basic angle in radians
	if (a == 0) // prevent division by zero
		radians = PI / static_cast<double>(2); // 90 degrees
	else
		radians = atan(o / a); // inverse tan

	// convert radians to internal angle (also round up)
	const double angle = radians * static_cast<double>(MAX_ANGLE) / (static_cast<double>(2) * PI);
	// convert to absolute and round up as follows (because abs() isn't for doubles)
	if (angle > 0)
		viewpoint_angle = static_cast<int32_t>(angle + 0.5);
	else
		viewpoint_angle = static_cast<int32_t>(0.5 - angle);

	// convert angle from first quadrant to full range
	if (o >= 0)
	{
		if (a >= 0)
		{
			// first quadrant
			viewpoint_angle = static_cast<int32_t>(angle);
		}
		else
		{
			// second quadrant
			viewpoint_angle = static_cast<int32_t>(angle) + _180_DEGREES;
		}
	}
	else
	{
		if (a <= 0)
		{
			// third quadrant
			viewpoint_angle = static_cast<int32_t>(angle) + _180_DEGREES;
		}
		else
		{
			// fourth quadrant
			viewpoint_angle = static_cast<int32_t>(angle) + _360_DEGREES;
		}
	}

	// default is anti-clockwise, so convert to clockwise if necessary
	if (clockwise)
	{
		viewpoint_angle = -viewpoint_angle & MAX_ANGLE - 1;
	}

	return viewpoint_angle;
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

	const Vec3 v1(static_cast<float>(c1->x), static_cast<float>(c1->y), static_cast<float>(c1->z));
	const Vec3 v2(static_cast<float>(c2->x), static_cast<float>(c2->y), static_cast<float>(c2->z));
	const Vec3 v3(static_cast<float>(c3->x), static_cast<float>(c3->y), static_cast<float>(c3->z));

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
	uint32_t colour = SCRGB(SCR_BASE_COLOUR + 0);
	// viewing from back
	StoreCarTriangle(&car[0], &car[1], &car[2], pVertices, colour);
	StoreCarTriangle(&car[0], &car[2], &car[3], pVertices, colour);
	// viewing from front
	StoreCarTriangle(&car[3], &car[2], &car[1], pVertices, colour);
	StoreCarTriangle(&car[3], &car[1], &car[0], pVertices, colour);

	// rear right wheel
	// viewing from back
	StoreCarTriangle(&car[0 + 4], &car[1 + 4], &car[2 + 4], pVertices, colour);
	StoreCarTriangle(&car[0 + 4], &car[2 + 4], &car[3 + 4], pVertices, colour);
	// viewing from front
	StoreCarTriangle(&car[3 + 4], &car[2 + 4], &car[1 + 4], pVertices, colour);
	StoreCarTriangle(&car[3 + 4], &car[1 + 4], &car[0 + 4], pVertices, colour);

	// front left wheel
	// viewing from back
	StoreCarTriangle(&car[0 + 8], &car[1 + 8], &car[2 + 8], pVertices, colour);
	StoreCarTriangle(&car[0 + 8], &car[2 + 8], &car[3 + 8], pVertices, colour);
	// viewing from front
	StoreCarTriangle(&car[3 + 8], &car[2 + 8], &car[1 + 8], pVertices, colour);
	StoreCarTriangle(&car[3 + 8], &car[1 + 8], &car[0 + 8], pVertices, colour);

	// front right wheel
	// viewing from back
	StoreCarTriangle(&car[0 + 12], &car[1 + 12], &car[2 + 12], pVertices, colour);
	StoreCarTriangle(&car[0 + 12], &car[2 + 12], &car[3 + 12], pVertices, colour);
	// viewing from front
	StoreCarTriangle(&car[3 + 12], &car[2 + 12], &car[1 + 12], pVertices, colour);
	StoreCarTriangle(&car[3 + 12], &car[1 + 12], &car[0 + 12], pVertices, colour);
	/**/

	// car left side
	colour = SCRGB(SCR_BASE_COLOUR + 12);
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

	//r.SetDepthTestEnabled(true);
	//r.SetCullMode(SoftwareRenderer::CULL_CCW);

	r.DrawTriangleList(pCarVertices, 0, numCarVertices / 3, nullptr);
}
