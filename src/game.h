#pragma once

//
// game.h — Shared declarations for the game module.
//
// Contents:
//   - Screen / rendering constants and SCRGB / GetScreenDimensions helpers
//   - GameModeType enum and IDM_* menu command IDs
//   - Fixed-point 3D math constants (MAX_ANGLE, rotation matrix indices)
//   - COORD_2D / COORD_3D / COORD_XZ / COORD_Y geometry types
//   - GameState / TrackState / SoundState and other cross-module structs
//   - Function declarations exported by game.cpp and the game.* modules
//

#include <cstdint>
#include <string_view>
#include <vector>

struct SoundState;
class SoftwareRenderer;
struct SWTexture;
struct PlatformSoundBuffer;
struct Point2D;

// ---------------------------------------------------------------------------
// Screen & rendering constants
// ---------------------------------------------------------------------------

constexpr int32_t SCR_BASE_COLOUR = 26;
constexpr double PI = 3.14159265358979323846;
constexpr float SCR_PI = PI;
constexpr float FURTHEST_Z = 131072.0f;

constexpr int32_t PRECISION = 16384;
constexpr int32_t LOG_PRECISION = 14;
constexpr int32_t PC_FACTOR = 2;
constexpr int MAX_POLY_SIDES = 8;

struct ScreenSize
{
	int32_t width;
	int32_t height;
};

ScreenSize GetScreenDimensions(const SoftwareRenderer& r);

uint32_t SCRGB(int32_t colour_index);

// ---------------------------------------------------------------------------
// Game mode & menu commands
// ---------------------------------------------------------------------------

enum GameModeType
{
	TRACK_MENU = 0,
	TRACK_PREVIEW,
	GAME_IN_PROGRESS,
	GAME_OVER
};

constexpr int IDM_TRACK_FIRST = 1000;
constexpr int IDM_START_RACE = 1010;
constexpr int IDM_BACK_TO_MENU = 1011;
constexpr int IDM_PAUSE = 1012;
constexpr int IDM_RESUME = 1013;
constexpr int IDM_REVERSE_CAR = 1014;
constexpr int IDM_EXIT = 1015;

constexpr int IDM_OUTSIDE_VIEW = 1100;
constexpr int IDM_SHOW_STATS = 1101;
constexpr int IDM_SCENERY_FIRST = 1110;

constexpr int IDM_SPEED_INCREASE = 1200;
constexpr int IDM_SPEED_DECREASE = 1201;

constexpr int IDM_PAUSE_PLAYER = 1300;
constexpr int IDM_PAUSE_OPPONENT = 1301;
constexpr int IDM_RESTART_RACE = 1302;

// ---------------------------------------------------------------------------
// Fixed-point 3D math
// ---------------------------------------------------------------------------

constexpr int32_t MAX_ANGLE = 65536;

constexpr int32_t _360_DEGREES = MAX_ANGLE;
constexpr int32_t _270_DEGREES = 3 * MAX_ANGLE / 4;
constexpr int32_t _180_DEGREES = MAX_ANGLE / 2;
constexpr int32_t _90_DEGREES = MAX_ANGLE / 4;
constexpr int32_t _0_DEGREES = 0;

// Rotation matrix component indices
constexpr int X_X_COMP = 0;
constexpr int X_Y_COMP = 1;
constexpr int X_Z_COMP = 2;
constexpr int Y_X_COMP = 3;
constexpr int Y_Y_COMP = 4;
constexpr int Y_Z_COMP = 5;
constexpr int Z_X_COMP = 6;
constexpr int Z_Y_COMP = 7;
constexpr int Z_Z_COMP = 8;
constexpr int NUM_TRIG_COEFFS = Z_Z_COMP + 1;

constexpr int32_t NO_OPPONENT = -1;
constexpr int32_t NUM_OPPONENTS = 11;

struct COORD_3D
{
	int32_t x;
	int32_t y;
	int32_t z;
};

struct COORD_2D
{
	int32_t x;
	int32_t y;
};

struct COORD_XZ
{
	int32_t x;
	int32_t z;
};

struct COORD_Y
{
	int32_t y;
};

struct SinCos
{
	int16_t sin;
	int16_t cos;
};

struct ViewAngle
{
	int32_t x_angle;
	int32_t y_angle;
};

struct RotationMatrix
{
	int16_t coeffs[NUM_TRIG_COEFFS];
	int16_t operator[](const int i) const { return coeffs[i]; }
};

void CreateSinCosTable();
SinCos GetSinCos(int32_t angle);

RotationMatrix CalcYXZTrigCoefficients(int32_t x_angle,
                                       int32_t y_angle,
                                       int32_t z_angle);

COORD_3D WorldOffset(const RotationMatrix& rot,
                     int32_t x,
                     int32_t y,
                     int32_t z);

ViewAngle LockViewpointToTarget(int32_t viewpoint_x,
                                int32_t viewpoint_y,
                                int32_t viewpoint_z,
                                int32_t target_x,
                                int32_t target_y,
                                int32_t target_z);


// ---------------------------------------------------------------------------
// Track
// ---------------------------------------------------------------------------

constexpr int32_t MAX_PIECES_PER_TRACK = 100;
constexpr int32_t MAX_SEGMENTS_PER_PIECE = 13;
constexpr int32_t NUM_TRACK_CUBES = 16;
constexpr int32_t CUBE_SIZE = 0x04000000; // (0x800 * PC_FACTOR * PRECISION)
constexpr int32_t LOG_CUBE_SIZE = 26;
constexpr int32_t TRACK_BOTTOM_Y = 0;

// Track numbers do not correspond to track league positions
constexpr int32_t NO_TRACK = -1;
constexpr int32_t LITTLE_RAMP = 0;
constexpr int32_t STEPPING_STONES = 1;
constexpr int32_t HUMP_BACK = 2;
constexpr int32_t BIG_RAMP = 3;
constexpr int32_t SKI_JUMP = 4;
constexpr int32_t DRAW_BRIDGE = 5;
constexpr int32_t HIGH_JUMP = 6;
constexpr int32_t ROLLER_COASTER = 7;
constexpr int32_t NUM_TRACKS = 8;

struct TRACK_PIECE
{
	int32_t x, y, z; // front left corner, within world
	int32_t roughPieceAngle; // 0, 90, 180, 270 degrees (internal angle format)
	int32_t oppositeDirection;
	int32_t curveToLeft;
	int32_t type; // 0x00 STRAIGHT, 0x40 DIAGONAL, 0x80 CURVE RIGHT, 0xC0 CURVE LEFT
	int32_t lengthReduction;
	int32_t steeringAmount;
	int32_t numSegments;
	int32_t firstSegment;
	int32_t initialColour;
	uint8_t roadColour[MAX_SEGMENTS_PER_PIECE];
	uint8_t sidesColour;
	COORD_3D* coords;
	int32_t coordsSize;
};

struct TrackState
{
	int32_t TrackID = NO_TRACK;
	TRACK_PIECE Track[MAX_PIECES_PER_TRACK] = {};
	int32_t Track_Map[NUM_TRACK_CUBES][NUM_TRACK_CUBES] = {};
	int32_t NumTrackPieces = 0;
	int32_t NumTrackSegments = 0;
	int32_t PlayersStartPiece = 0;
	int32_t StartLinePiece = 0;
	int32_t HalfALapPiece = 0;
	int32_t StandardBoost = 0;
	int32_t SuperBoost = 0;
	uint8_t sections_car_can_be_put_on[16] = {
		0x00, 0x80, 0x20, 0xc0, 0x00, 0x73, 0x80, 0xc0,
		0xa9, 0x59, 0x00, 0x02, 0xa9, 0x5e, 0x85, 0x4b
	};
};

wchar_t* GetTrackName(int32_t track);
char GetPieceAngleAndTemplate(int32_t piece);
int32_t ConvertAmigaTrack(TrackState& t, int32_t track);
void FreeTrackData(const TrackState& t);
void CreateTrackVertexBuffer(const TrackState& t);
void FreeTrackVertexBuffer();
void DrawTrack(const TrackState& t, GameModeType GameMode, SoftwareRenderer& r, int32_t playerCurrentPiece,
               int32_t playerCurrentSegment, const std::vector<SWTexture>& roadTextures, bool disableCulling = false);
void CreateShadowVertexBuffer();
void FreeShadowVertexBuffer();

// ---------------------------------------------------------------------------
// Car physics & driving
// ---------------------------------------------------------------------------

constexpr int32_t CAR_WIDTH = 64;
constexpr int32_t CAR_LENGTH = 128;

constexpr uint32_t KEY_P1_LEFT = 0x00000001u;
constexpr uint32_t KEY_P1_RIGHT = 0x00000002u;
constexpr uint32_t KEY_P1_BRAKE = 0x00000008u;
constexpr uint32_t KEY_P1_ACCEL = 0x00000010u;
constexpr uint32_t KEY_P1_BOOST = 0x00000020u;

constexpr int32_t REDUCTION = 238; // (238/256)
constexpr int32_t INCREASE = 276; // (276/256)

enum CarType
{
	OPPONENT = 0,
	PLAYER,
	NUM_CARS
};

struct CarPose
{
	int32_t x;
	int32_t y;
	int32_t z;
	int32_t x_angle;
	int32_t y_angle;
	int32_t z_angle;
};

struct GameState
{
	GameState() = default;
	GameState(const GameState&) = delete;
	GameState& operator=(const GameState&) = delete;
	GameState(GameState&&) = default;
	GameState& operator=(GameState&&) = default;

	int32_t player_current_piece = 0;
	int32_t player_current_segment = 0;
	int32_t players_distance_into_section = 0;
	int32_t players_road_x_position = 0;
	int32_t rear_wheel_surface_x_position = 0;
	int32_t player_y = 0;
	int32_t player_z_speed = 0;

	int32_t front_left_damage = 0;
	int32_t front_right_damage = 0;
	int32_t rear_damage = 0;
	int32_t damaged = 0;
	int32_t new_damage = 0;

	int32_t car_collision_x_acceleration = 0;
	int32_t car_collision_y_acceleration = 0;
	int32_t car_collision_z_acceleration = 0;

	int32_t boostReserve = 0;
	int32_t boostUnit = 0;
	int32_t INITIALISE_PLAYER = true;

	bool drop_start_done = false;
	bool touching_road = false;

	static constexpr int32_t MAX_AMIGA_VOLUME = 64;
	static constexpr int32_t DIRECTX_VOLUME_FACTOR = 100; // hundredths of decibels

	static constexpr int32_t GRAVITY_ACCELERATION = 317;

	static constexpr int32_t ROAD_WIDTH = 0x0180;

	static constexpr int32_t SURFACE_SIZE = 1024;
	static constexpr int32_t LOG_SURFACE_SIZE = 10;

	static constexpr int32_t OFF_ROAD_HEIGHT = 0x1000;

	static constexpr int32_t OFF_TRACK_LIMIT = 64; // count after which player is put back on track

	static constexpr int32_t LOCAL_Y_FACTOR = 4;

	int32_t player_x = 0,
	        player_z = 0;

	int32_t player_x_angle = 0,
	        player_y_angle = 0,
	        player_z_angle = 0;

	int32_t player_world_x_speed = 0,
	        player_world_y_speed = 0,
	        player_world_z_speed = 0;

	int32_t player_x_speed = 0,
	        player_y_speed = 0;

	int32_t accelerate = 0, brake = 0;

	int32_t accelerating = false; // previous control state

	int32_t engine_power = 240; // (240 standard, 320 super)
	int32_t boost_unit_value = 16; // (16 standard, 12 super)

	int32_t left_right_value = 0;
	int32_t engine_z_acceleration = 0;
	int32_t boost_activated = 0;

	int32_t rear_wheel_x_offset = 0, rear_wheel_z_offset = 0;
	int32_t front_left_wheel_x_offset = 0, front_left_wheel_z_offset = 0;
	int32_t front_right_wheel_x_offset = 0, front_right_wheel_z_offset = 0;

	int32_t front_left_road_height = OFF_ROAD_HEIGHT;
	int32_t front_right_road_height = OFF_ROAD_HEIGHT;
	int32_t rear_road_height = OFF_ROAD_HEIGHT;

	int32_t front_left_actual_height = 0;
	int32_t front_right_actual_height = 0;
	int32_t rear_actual_height = 0;

	int32_t off_left = 0, off_right = 0;
	int32_t wheel_off_road = 0, distance_off_road = 0;
	int32_t at_side_byte = 0, which_side_byte = 0;
	int32_t smaller_limit_required = false;

	int32_t wreck_wheel_height_reduction = 0; // 0x200 if wrecked

	int32_t on_chains = false;
	int32_t chain_height_remaining = 0;

	int32_t player_distance_off_road = 0;
	int32_t off_map_status = 0;

	int32_t off_track_count = 0;

	int32_t gravity_x_acceleration = 0,
	        gravity_y_acceleration = 0,
	        gravity_z_acceleration = 0;

	int32_t grounded_delay = 0;
	int32_t grounded_count = 0;
	int32_t damage_value = 0;
	int32_t damaged_count = 0;

	int32_t front_left_amount_below_road = 0,
	        front_right_amount_below_road = 0,
	        rear_amount_below_road = 0;

	int32_t old_front_left_difference = 0,
	        old_front_right_difference = 0,
	        old_rear_difference = 0;

	int32_t smashed_countdown = 0;

	int32_t car_to_road_collision_z_acceleration = 0;

	int32_t player_x_acceleration = 0,
	        player_y_acceleration = 0,
	        player_z_acceleration = 0;

	int32_t total_world_x_acceleration = 0,
	        total_world_y_acceleration = 0,
	        total_world_z_acceleration = 0;

	int32_t player_x_rotation_speed = 0,
	        player_y_rotation_speed = 0,
	        player_z_rotation_speed = 0;

	int32_t player_final_x_rotation_speed = 0,
	        player_final_y_rotation_speed = 0,
	        player_final_z_rotation_speed = 0;

	int32_t player_x_rotation_acceleration = 0,
	        player_y_rotation_acceleration = 0,
	        player_z_rotation_acceleration = 0;

	int32_t Replay = false, ReplayRequested = false, ReplayFinished = false;

	bool raceFinished = false;
	bool raceWon = false;
	int32_t lapNumber[NUM_CARS] = {};
	bool bNewGame = false;

	int32_t opponentsID = NO_OPPONENT;
	int32_t opponents_current_piece = 0;
	bool player_close_to_opponent = false;
	bool opponent_behind_player = false;
};


void ResetPlayer(GameState& player);

int32_t LimitViewpointY(TrackState& track, GameState& player, int32_t y);
int32_t CalculateDisplaySpeed(const GameState& player);
void FramesWheelsEngine(SoundState& sound, PlatformSoundBuffer* engineSoundBuffers[]);
void EngineSoundStopped();
void CalculatePlayersRoadPosition(TrackState& track, GameState& player);
void DrawOtherGraphics(GameState& player, const SoundState& sound);
void UpdateDamage(GameState& player, const SoundState& sound);

void MoveDrawBridge(const TrackState& t, const GameState& p);
void ResetDrawBridge(const TrackState& t, GameState& p);

// ---------------------------------------------------------------------------
// Visible car model
// ---------------------------------------------------------------------------

constexpr int32_t VCAR_WIDTH = 162; // ((width 27+27 * segment width 384) / surface factor 256) * PC_FACTOR
constexpr int32_t VCAR_LENGTH = 256; // ((length 128 * segment length 256) / surface factor 256) * PC_FACTOR
constexpr int32_t VCAR_HEIGHT = 162;

void CreateCarVertexBuffer();
void FreeCarVertexBuffer();
void DrawCar(SoftwareRenderer& r);

// ---------------------------------------------------------------------------
// Opponent
// ---------------------------------------------------------------------------

struct OpponentPose
{
	int32_t x;
	int32_t y;
	int32_t z;
	float x_angle;
	float y_angle;
	float z_angle;
};

std::wstring_view GetOpponentName(int32_t opponentID);

CarPose CarBehaviour(TrackState& track, GameState& player, const SoundState& sound,
                     uint32_t input,
                     int32_t x,
                     int32_t y,
                     int32_t z,
                     int32_t x_angle,
                     int32_t y_angle,
                     int32_t z_angle);

OpponentPose OpponentBehaviour(TrackState& track,
                               GameState& game,
                               bool bOpponentPaused);

void CarToCarCollision(GameState& player, const SoundState& sound);

int32_t CalculateIfWinning(const TrackState& track, const GameState& game,
                           int32_t start_finish_piece);

int32_t CalculateOpponentsDistance(const GameState& game);

void ResetLapData(GameState& game, int32_t car);
void UpdateLapData(GameState& player, const TrackState& track);

// ---------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------

struct SoundState
{
	SoundState() = default;
	SoundState(const SoundState&) = delete;
	SoundState& operator=(const SoundState&) = delete;
	SoundState(SoundState&&) = default;
	SoundState& operator=(SoundState&&) = default;

	PlatformSoundBuffer* WreckSoundBuffer = nullptr;
	PlatformSoundBuffer* HitCarSoundBuffer = nullptr;
	PlatformSoundBuffer* GroundedSoundBuffer = nullptr;
	PlatformSoundBuffer* CreakSoundBuffer = nullptr;
	PlatformSoundBuffer* SmashSoundBuffer = nullptr;
	PlatformSoundBuffer* OffRoadSoundBuffer = nullptr;
	PlatformSoundBuffer* EngineSoundBuffers[8] = {};
	bool engineSoundPlaying = false;
};

// ---------------------------------------------------------------------------
// Backdrop & scenery
// ---------------------------------------------------------------------------

void DrawBackdrop(SoftwareRenderer& r,
                  int32_t viewpoint_y,
                  int32_t viewpoint_x_angle,
                  int32_t viewpoint_y_angle,
                  int32_t viewpoint_z_angle);

void NextSceneryType();
int32_t GetSceneryType();
void SetSceneryType(int32_t type);

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void InitialiseData(TrackState& t);
void CreateResources();
void FreeResources();

bool SetupSoundBuffers(SoundState& s);
void DestroySoundBuffers(SoundState& s);
void FreeData(SoundState& s, const TrackState& t);

void OnFrameMove(double fTime, const TrackState& t, const GameState& p);
void OnFrameRender(const TrackState& t, GameModeType GameMode, double fTime);
