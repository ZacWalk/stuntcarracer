// Opponent.cpp — Opponent AI movement, pre-recorded speed playback,
// car-to-car collision detection, and race position tracking.

#include "platform.h"
#include "game.h"

#include <algorithm>

using namespace std::string_view_literals;

#define	OPPONENT_SHADOW

static constexpr int32_t NUM_X_SPANS = 32;

static constexpr int32_t LOCAL_Y_FACTOR = 4;

enum OppWheelPosition
{
	REAR_LEFT = 0,
	REAR_RIGHT,
	FRONT,
	NUM_OPP_WHEEL_POSITIONS
};

// Indices into opp_steering[] array
enum { STEER_LEFT = 0, STEER_RIGHT = 1, STEER_COUNT = 2 };

uint8_t opponents_speed_values[NUM_TRACKS][MAX_PIECES_PER_TRACK] =

	// opponents_speed_values (initialized below)
	{
		{
			/* Little Ramp data */
			0x76, 0x6c, 0x62, 0x58, 0x7a, 0x7a, 0x70, 0x66, 0x5c, 0x52, 0x48, 0x48, 0x48, 0x7a, 0x7a, 0x7a,
			0x7a, 0x7a, 0x7a, 0x7a, 0x70, 0x66, 0x5c, 0x52, 0x48, 0x48, 0x48, 0x48, 0x78, 0x6e, 0x64, 0x5a,
			0x50, 0x46, 0x7a, 0x70, 0x66, 0x5c, 0x52, 0x48, 0x48, 0x48, 0x48, 0x7c
		},
		{
			/* Stepping Stones data */
			0xf2, 0xe8, 0xde, 0xd4, 0x67, 0x5d, 0x53, 0x49, 0x3f, 0x4b, 0x41, 0x41, 0xc1, 0xd2, 0xc8, 0xbe,
			0xc7, 0xbd, 0xc5, 0xbb, 0xc4, 0xba, 0x55, 0x4b, 0x41, 0x41, 0x41, 0x60, 0x56, 0x4c, 0x42, 0x7d,
			0x7d, 0x73, 0x69, 0x5f, 0x55, 0x4b, 0x41, 0x41, 0x41, 0xfd, 0xfd, 0xfd, 0xf3, 0x7d, 0x7d, 0x73,
			0x69, 0x5f, 0x55, 0x4b, 0x41, 0x41, 0x41, 0x7c
		},
		{
			/* Hump Back data */
			0x52, 0x4d, 0x77, 0x77, 0x77, 0x6d, 0x63, 0x59, 0x4f, 0x45, 0x45, 0x45, 0x77, 0x77, 0x77, 0x77,
			0x77, 0x77, 0x77, 0x6d, 0x63, 0x59, 0x4f, 0x45, 0x45, 0x45, 0x56, 0x4c, 0x77, 0x77, 0x6d, 0x63,
			0x59, 0x4f, 0x45, 0x45, 0x45, 0x4f, 0x61, 0x57, 0x4d, 0x45, 0x4f, 0x45, 0x45, 0x63, 0x59, 0x4f,
			0x45, 0x45, 0x45, 0x66, 0x5c
		},
		{
			/* Big Ramp data */
			0x7a, 0x7a, 0x7a, 0x7a, 0x7a, 0x7a, 0x70, 0x66, 0x5c, 0x52, 0x48, 0x48, 0x48, 0x58, 0x4e, 0x4b,
			0x69, 0x5f, 0x55, 0x4b, 0x46, 0x66, 0x5c, 0x52, 0x48, 0x48, 0x48, 0x48, 0x7e, 0xf4, 0xea, 0xe0,
			0xd6, 0x7a, 0x7a, 0x70, 0x66, 0x5c, 0x52, 0x48, 0x48, 0x48, 0x48, 0x7c
		},
		{
			/* Ski Jump data */
			0x42, 0xec, 0xe2, 0xd8, 0x77, 0x77, 0x77, 0x6d, 0x63, 0x59, 0x4f, 0x4f, 0x4f, 0x4f, 0x63, 0x59,
			0x4f, 0x4f, 0x72, 0x68, 0x5e, 0x54, 0x4a, 0x40, 0x36, 0x4f, 0x4f, 0x4f, 0x6a, 0xe0, 0xd6, 0xcc,
			0xc2, 0x63, 0x59, 0x4f, 0xcf, 0xcf, 0xcf, 0xc9, 0x56, 0x56, 0x56, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e,
			0x7e, 0x74, 0x6a, 0x60, 0x56, 0x56, 0x56, 0x56, 0x56, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x74,
			0x6a, 0x60, 0x56, 0x56, 0x56, 0x7e, 0x7e, 0x74, 0x6a, 0x60, 0x56, 0x56, 0x5a, 0x52
		},
		{
			/* Draw Bridge data */
			0x76, 0x76, 0x6c, 0x62, 0x69, 0x5f, 0x55, 0x50, 0x58, 0x58, 0x58, 0x76, 0x76, 0x76, 0x6c, 0x62,
			0x58, 0x58, 0x58, 0x4d, 0x43, 0x76, 0x76, 0x76, 0x76, 0x76, 0x6c, 0x62, 0x58, 0x58, 0x58, 0x58,
			0x58, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0xf8, 0xee, 0xe4, 0x5a, 0x50, 0xc6,
			0x76, 0x76, 0x76, 0xbb, 0xbb, 0x76, 0x76, 0x76, 0x6c, 0x62, 0xd8, 0xd8, 0xd8, 0xe4, 0xf6, 0xec,
			0xe2, 0xd8, 0x76, 0x76, 0x76, 0x76, 0x6c, 0x62, 0x58, 0x58, 0x58, 0x58, 0x58, 0x7c
		},
		{
			/* High Jump data */
			0xe7, 0xdd, 0xd3, 0x77, 0x77, 0x77, 0x77, 0x6d, 0x63, 0x59, 0x4f, 0x4f, 0x4f, 0x7a, 0x7a, 0x7a,
			0x7a, 0x7a, 0x70, 0x66, 0x5c, 0x52, 0x52, 0x55, 0x59, 0x4f, 0x4f, 0x77, 0x77, 0x77, 0x77, 0x6d,
			0x63, 0x59, 0x4f, 0x4f, 0xcf, 0xe7, 0xdd, 0xd3, 0xce, 0x77, 0x77, 0x77, 0x77, 0x6d, 0x63, 0x59,
			0x4f, 0x4f, 0x4f, 0x7c, 0x41, 0x41, 0x41, 0x7c
		},
		{
			/* Roller Coaster data */
			0x66, 0x5c, 0x52, 0x48, 0x3e, 0x34, 0x2a, 0x29, 0x6a, 0x60, 0x56, 0x56, 0x56, 0x40, 0x36, 0x7e,
			0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x74, 0x6a, 0x60, 0x56, 0x56, 0x54, 0x4a, 0x7e, 0x7e, 0x7e, 0x7e,
			0x7e, 0x7e, 0x7e, 0x74, 0x6a, 0x60, 0x56, 0x56, 0x56, 0x56, 0x56, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e,
			0x7e, 0x74, 0x6a, 0x60, 0x56, 0x56, 0x56, 0x56, 0x56, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x74,
			0x6a, 0x60, 0x56, 0x56, 0x56, 0x7e, 0x7e, 0x74, 0x6a, 0x60, 0x56, 0x56, 0x5a, 0x52
		}
	};

std::wstring_view opponentNames[NUM_OPPONENTS] =
{
	L"Hot Rod"sv,
	L"Whizz Kid"sv,
	L"Bad Guy"sv,
	L"The Dodger"sv,
	L"Big Ed"sv,
	L"Max Boost"sv,
	L"Dare Devil"sv,
	L"High Flyer"sv,
	L"Bully Boy"sv,
	L"Jumping Jack"sv,
	L"Road Hog"sv
};

std::wstring_view GetOpponentName(const int32_t opponentID)
{
	return opponentNames[opponentID];
}

// Opponent attributes
static constexpr uint8_t OBSTRUCTS_PLAYER = 2;
static constexpr uint8_t WHEELIE = 4;
static constexpr uint8_t DRIVES_NEAR_EDGE = 8;
static constexpr uint8_t UNUSED4 = 16;
static constexpr uint8_t PUSH_PLAYER = 32;
static constexpr uint8_t UNUSED6 = 64;

static uint8_t opponent_attributes[NUM_OPPONENTS] =
{
	// Hot Rod
	PUSH_PLAYER | OBSTRUCTS_PLAYER,
	// Whizz Kid
	PUSH_PLAYER,
	// Bad Guy
	UNUSED6 | PUSH_PLAYER | OBSTRUCTS_PLAYER,
	// The Dodger
	PUSH_PLAYER,
	// Big Ed
	PUSH_PLAYER | UNUSED4 | DRIVES_NEAR_EDGE | WHEELIE | OBSTRUCTS_PLAYER,
	// Max Boost
	WHEELIE,
	// Dare Devil
	PUSH_PLAYER | UNUSED4,
	// High Flyer
	UNUSED4 | WHEELIE,
	// Bully Boy
	UNUSED6 | DRIVES_NEAR_EDGE | OBSTRUCTS_PLAYER,
	// Jumping Jack
	UNUSED4,
	// Road Hog
	DRIVES_NEAR_EDGE
};

// Values for each track
static uint8_t opp_track_speed_values[] =
{
	// Standard league
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x41, 0x3a, 0x3e, 0x41, 0x48, 0x51, 0x48, 0x4f,

	// Super league
	0x07, 0x03, 0x03, 0x03, 0x03, 0x03, 0x07, 0x03,
	0x66, 0x57, 0x57, 0x59, 0x59, 0x69, 0x62, 0x64
};

static int32_t opponents_distance_into_section;
static int32_t opponents_road_x_position;

// Three co-ordinates needed for Opponent (as per original Amiga StuntCarRacer)
static COORD_3D opp_rear_left_road_pos;
static COORD_3D opp_rear_right_road_pos;
static int32_t opp_front_road_pos_y; //X,Z not needed

// Additional co-ordinates needed for PC StuntCarRacer (for calculating opponent orientation)
static COORD_3D opp_front_left_road_pos;
static COORD_3D opp_front_right_road_pos;

static COORD_3D opp_shadow_rear_left;
static COORD_3D opp_shadow_rear_right;
static COORD_3D opp_shadow_front_left;
static COORD_3D opp_shadow_front_right;

// wheel heights
static int32_t opp_actual_height[NUM_OPP_WHEEL_POSITIONS];

static int32_t opp_smallest_difference;

static int32_t opp_old_rear_left_difference;
static int32_t opp_old_rear_right_difference;
static int32_t opp_old_front_difference;

static int32_t opp_new_rear_left_difference;
static int32_t opp_new_rear_right_difference;
static int32_t opp_new_front_difference;

static bool opp_touching_road;

static int32_t opp_y_acceleration[NUM_OPP_WHEEL_POSITIONS];
static int32_t opp_y_speed[NUM_OPP_WHEEL_POSITIONS];

static int32_t opp_engine_power = 236; // (236 standard, 314 super)
static int32_t opponents_engine_z_acceleration;
static int32_t opponents_max_speed;
static int32_t opponents_z_speed;
static bool opponents_required_z_speed_reached;

struct OppSurfaceResult
{
	int32_t position;
	bool next_segment;
};

struct WheelDiffResult
{
	int32_t new_difference;
	int32_t old_difference;
	int32_t touching_road;
};

struct SurfaceCoords
{
	int32_t x1, y1, z1;
	int32_t x2, y2, z2;
	int32_t x3, y3, z3;
	int32_t x4, y4, z4;
};

static void ResetOpponent(GameState& game);
static void CalculateOpponentsRoadWheelPositions(const GameState& game, const TrackState& t);
static SurfaceCoords GetSurfaceCoords(const TrackState& t, int32_t piece, int32_t segment);
static OppSurfaceResult CalcSurfacePosition(int32_t distance, int32_t z_shift);
static int32_t CalculateOpponentsRoadWheelHeight(const SurfaceCoords& sc, int32_t sx, int32_t sz);
static void OpponentMovement(const TrackState& t, GameState& player);

static void UpdateOpponentsActualWheelHeights(const TrackState& t, const GameState& game);
static WheelDiffResult CalculateWheelDifference(int32_t road_height,
                                                int32_t actual_height,
                                                int32_t height_adjust,
                                                int32_t old_difference,
                                                int32_t touching_road);
static int32_t LimitOpponentWheels(int32_t max_difference, int32_t wheel1, int32_t wheel2);
static void AverageWheelYSpeeds(int32_t wheel1, int32_t wheel2);

static void RandomizeOpponentsSteering(const GameState& game, const TrackState& t);

static void GetOpponentsEngineAcceleration();
static void AdjustOpponentsEngineAcceleration(const GameState& game, const TrackState& t);
static void UpdateOpponentsZSpeed(const TrackState& t, const GameState& game);

static void CalculateDistancesBetweenPlayers(const TrackState& t, GameState& player);

static void OpponentPlayerInteraction(const TrackState& t, GameState& player);
static void MoveOpponentToOneSide();
static void OpponentPushPlayer(const GameState& player);
static void CarToCarCollisionDetection(GameState& player);
void CarToCarCollision(GameState& player, const SoundState& sound);

static void ResetOpponent(GameState& game)
{
	game.opponentsID = rand() % NUM_OPPONENTS;

	opp_old_rear_left_difference = 0;
	opp_old_rear_right_difference = 0;
	opp_old_front_difference = 0;

	for (int32_t i = 0; i < NUM_OPP_WHEEL_POSITIONS; i++)
	{
		opp_y_speed[i] = 0;
	}

	opponents_z_speed = 0;
	opponents_required_z_speed_reached = false;

	game.player_close_to_opponent = false;
	game.opponent_behind_player = false;
}

OpponentPose OpponentBehaviour(
	TrackState& track,
	GameState& game,
	const bool oppPaused)
{
	float opponent_x_angle = 0.0f, opponent_y_angle = 0.0f, opponent_z_angle = 0.0f;

	// reset opponent
	if (game.bNewGame)
	{
		ResetOpponent(game);

		game.opponents_current_piece = track.PlayersStartPiece;
		opponents_distance_into_section = 0x400; // half way into section
		opponents_road_x_position = 0x4c;

		// initialise opponent data
		CalculateOpponentsRoadWheelPositions(game, track);
		// Position the opponent a random amount above the road
		const int32_t r = (rand() & 0x7f) + 0x68;
		opp_actual_height[REAR_LEFT] = opp_rear_left_road_pos.y + r;
		opp_actual_height[REAR_RIGHT] = opp_rear_right_road_pos.y + r;
		opp_actual_height[FRONT] = opp_front_road_pos_y + r;

		// Set opponent_max_speed
		int32_t s = rand() & static_cast<int32_t>(opp_track_speed_values[track.TrackID]);
		s += static_cast<int32_t>(opp_track_speed_values[track.TrackID + 8]);
		opponents_max_speed = s;

		game.bNewGame = false;
	}

	CalculatePlayersRoadPosition(track, game);
	if (!oppPaused)
	{
		OpponentMovement(track, game);
		CalculateDistancesBetweenPlayers(track, game);
		OpponentPlayerInteraction(track, game);
	}
	else
		CalculateDistancesBetweenPlayers(track, game);

	CalculateOpponentsRoadWheelPositions(game, track);

	//
	// Calculate opponent's new centre point ...
	//

	/*
	 * Calculate opponent's x position
	 */
	int32_t opponent_x = (opp_front_left_road_pos.x + opp_front_right_road_pos.x + opp_rear_left_road_pos.x +
		opp_rear_right_road_pos.x) / 4;
	opponent_x <<= LOG_PRECISION;

	// Calculate opponent's y position (visible height = max of road and actual)
	const int32_t vis_rear_left_y = std::max(opp_rear_left_road_pos.y, opp_actual_height[REAR_LEFT]);
	const int32_t vis_rear_right_y = std::max(opp_rear_right_road_pos.y, opp_actual_height[REAR_RIGHT]);
	const int32_t vis_front_y = std::max(opp_front_road_pos_y, opp_actual_height[FRONT]);
	const int32_t rear_y = (vis_rear_left_y + vis_rear_right_y) / 2;
	int32_t opponent_y = (rear_y + vis_front_y) / 2;

	// Raise the opponent slightly (to stop them sinking into road due to inaccurate heights)
	opponent_y += 20;
	opponent_y <<= LOG_PRECISION - 3;

	/*
	 * Calculate opponent's z position
	 */
	int32_t opponent_z = (opp_front_left_road_pos.z + opp_front_right_road_pos.z + opp_rear_left_road_pos.z +
		opp_rear_right_road_pos.z) / 4;
	opponent_z <<= LOG_PRECISION;

	//
	// Calculate opponent's new angles
	//

	// Along car's x axis, only use y and z components
	double yd = static_cast<double>(rear_y - vis_front_y) / 2;
	// Note y is halved because of unit differences between y and x,z
	const int32_t rear_x = (opp_rear_left_road_pos.x + opp_rear_right_road_pos.x) / 2;
	const int32_t rear_z = (opp_rear_left_road_pos.z + opp_rear_right_road_pos.z) / 2;
	const int32_t front_x = (opp_front_left_road_pos.x + opp_front_right_road_pos.x) / 2;
	const int32_t front_z = (opp_front_left_road_pos.z + opp_front_right_road_pos.z) / 2;
	double xd = rear_x - front_x;
	double zd = rear_z - front_z;
	const double carzd = sqrt(xd * xd + zd * zd);
	opponent_x_angle = static_cast<float>(atan2(yd, carzd));

	// Along car's y axis, only use x and z components
	xd = static_cast<double>(opp_rear_left_road_pos.x - opp_rear_right_road_pos.x);
	zd = static_cast<double>(opp_rear_left_road_pos.z - opp_rear_right_road_pos.z);
	opponent_y_angle = static_cast<float>(atan2(zd, -xd));

	// Along car's z axis, only use x and y components
	yd = static_cast<double>(vis_rear_left_y - vis_rear_right_y) / 2;
	// Note y is halved because of unit differences between y and x,z
	const double carxd = sqrt(xd * xd + zd * zd);
	opponent_z_angle = static_cast<float>(atan2(-yd, carxd));

	// output opponent values for use by functions that draw the world
	return {
		opponent_x,
		-(opponent_y * LOCAL_Y_FACTOR),
		opponent_z,
		opponent_x_angle,
		opponent_y_angle,
		opponent_z_angle
	};
}

// Opponent steering state: [STEER_LEFT] = left z-shift, [STEER_RIGHT] = right z-shift,
// [STEER_COUNT] = random steering countdown
static int32_t opp_steering[3] = {0, 0, 0};

static int32_t opponents_x_spans[NUM_X_SPANS] =
{
	27, 27, 27, 27, 27, 26, 26, 26, 25, 25, 25, 24, 23, 23, 22, 21, 20, 19, 18, 17, 15, 14, 11, 9, 7, 7, 7, 7, 7, 7, 7,
	7
};

// All three road heights tested against Amiga
static void CalculateOpponentsRoadWheelPositions(const GameState& game, const TrackState& t)
{
	int32_t piece = game.opponents_current_piece;
	int32_t left_side_x, left_side_z, right_side_x, right_side_z;

	/*
	 * Rear wheels
	 */
	// Rear wheel position
	int32_t distance = opponents_distance_into_section - 64;
	if (distance < 0)
	{
		// DIRECTION DEPENDANT

		// go to previous piece
		piece--;
		if (piece < 0) piece = t.NumTrackPieces - 1;

		distance += t.Track[piece].numSegments * 256;
	}
	// Fetch 4 surface co-ords surrounding rear wheels (opponents.distance.into.section.minus64 / 256)
	int32_t segment = distance >> 8;
	auto sc = GetSurfaceCoords(t, piece, segment);

	// Calculate segment left side x,z at opponents.distance.into.section.minus64
	const auto surfLeft = CalcSurfacePosition(distance, opp_steering[STEER_LEFT]);
	int32_t surface_position = surfLeft.position;
	if (!surfLeft.next_segment)
	{
		left_side_x = sc.x2 + ((surface_position * (sc.x1 - sc.x2)) >> 8);
		left_side_z = sc.z2 + ((surface_position * (sc.z1 - sc.z2)) >> 8);
	}
	else
	{
		// Use other end's value as base
		// (Amiga StuntCarRacer does this, but not correct as should really use next segment's values)
		left_side_x = sc.x1 + ((surface_position * (sc.x1 - sc.x2)) >> 8);
		left_side_z = sc.z1 + ((surface_position * (sc.z1 - sc.z2)) >> 8);
	}

	// Calculate segment right side x,z at opponents.distance.into.section.minus64
	const auto surfRight = CalcSurfacePosition(distance, opp_steering[STEER_RIGHT]);
	surface_position = surfRight.position;
	if (!surfRight.next_segment)
	{
		right_side_x = sc.x3 + ((surface_position * (sc.x4 - sc.x3)) >> 8);
		right_side_z = sc.z3 + ((surface_position * (sc.z4 - sc.z3)) >> 8);
	}
	else
	{
		// Use other end's value as base
		// (Amiga StuntCarRacer does this, but not correct as should really use next segment's values)
		right_side_x = sc.x4 + ((surface_position * (sc.x4 - sc.x3)) >> 8);
		right_side_z = sc.z4 + ((surface_position * (sc.z4 - sc.z3)) >> 8);
	}

	int32_t i = abs(opp_actual_height[REAR_LEFT] - opp_actual_height[REAR_RIGHT]) >> 4;
	if (i >= NUM_X_SPANS) i = NUM_X_SPANS - 1;
	const int32_t opponents_x_span = opponents_x_spans[i];

	// For calculating the opponent's shadow co-ordinates, the original StuntCarRacer spans are slightly
	// too big, so need to be reduced to take into account the greater width of sloped segments
	const int32_t xd = right_side_x - left_side_x;
	const int32_t yd = (sc.y3 - sc.y2) / LOCAL_Y_FACTOR;
	const int32_t zd = right_side_z - left_side_z;
	const int32_t base_width = static_cast<int32_t>(sqrt(xd * xd + zd * zd));
	const int32_t slope_width = static_cast<int32_t>(sqrt(base_width * base_width + yd * yd));
	const int32_t opponents_shadow_x_span = opponents_x_span * base_width / slope_width;

	int32_t sz = distance & 0xff; // z position of rear wheels

	// Calculate rear left road co-ordinate (as per Amiga opp.rear.left.road.height)
	int32_t sx = opponents_road_x_position - opponents_x_span; // x position of rear left wheel
	opp_rear_left_road_pos.y = CalculateOpponentsRoadWheelHeight(sc, sx, sz);
	opp_rear_left_road_pos.x = left_side_x + ((sx * xd) >> 8);
	opp_rear_left_road_pos.z = left_side_z + ((sx * zd) >> 8);
	// Calculate rear left shadow co-ordinate
	sx = opponents_road_x_position - opponents_shadow_x_span;
	opp_shadow_rear_left.y = CalculateOpponentsRoadWheelHeight(sc, sx, sz);
	opp_shadow_rear_left.x = left_side_x + ((sx * xd) >> 8);
	opp_shadow_rear_left.z = left_side_z + ((sx * zd) >> 8);

	// Calculate rear right road co-ordinate (as per Amiga opp.rear.right.road.height)
	sx = opponents_road_x_position + opponents_x_span; // x position of rear right wheel
	opp_rear_right_road_pos.y = CalculateOpponentsRoadWheelHeight(sc, sx, sz);
	opp_rear_right_road_pos.x = left_side_x + ((sx * xd) >> 8);
	opp_rear_right_road_pos.z = left_side_z + ((sx * zd) >> 8);
	// Calculate rear right shadow co-ordinate
	sx = opponents_road_x_position + opponents_shadow_x_span;
	opp_shadow_rear_right.y = CalculateOpponentsRoadWheelHeight(sc, sx, sz);
	opp_shadow_rear_right.x = left_side_x + ((sx * xd) >> 8);
	opp_shadow_rear_right.z = left_side_z + ((sx * zd) >> 8);

	// Calculate position of piece's bottom front left corner, within world
	const int32_t piece_x = t.Track[piece].x << (LOG_CUBE_SIZE - LOG_PRECISION);
	const int32_t piece_z = t.Track[piece].z << (LOG_CUBE_SIZE - LOG_PRECISION);
	// Position rear road co-ordinates within world
	opp_rear_left_road_pos.x += piece_x;
	opp_rear_right_road_pos.x += piece_x;
	opp_rear_left_road_pos.z += piece_z;
	opp_rear_right_road_pos.z += piece_z;
	// Position rear shadow co-ordinates within world
	opp_shadow_rear_left.x += piece_x;
	opp_shadow_rear_right.x += piece_x;
	opp_shadow_rear_left.z += piece_z;
	opp_shadow_rear_right.z += piece_z;

	//

	//

	/*
	 * Front wheels
	 */

	// Calculate front left and right road x,z co-ordinates
	int32_t diff = opp_rear_right_road_pos.x - opp_rear_left_road_pos.x;
	int32_t xdiff = diff + (diff >> 1); // car length is 1.5 times width
	diff = opp_rear_right_road_pos.z - opp_rear_left_road_pos.z;
	int32_t zdiff = diff + (diff >> 1); // car length is 1.5 times width
	opp_front_left_road_pos.x = opp_rear_left_road_pos.x - zdiff;
	opp_front_left_road_pos.z = opp_rear_left_road_pos.z + xdiff;
	opp_front_right_road_pos.x = opp_rear_right_road_pos.x - zdiff;
	opp_front_right_road_pos.z = opp_rear_right_road_pos.z + xdiff;
	/* Don't need to add piece_x,piece_z as already have world position (from rear wheels) */

	// Calculate front left and right shadow x,z co-ordinates
	diff = opp_shadow_rear_right.x - opp_shadow_rear_left.x;
	xdiff = diff + (diff >> 1); // car length is 1.5 times width
	diff = opp_shadow_rear_right.z - opp_shadow_rear_left.z;
	zdiff = diff + (diff >> 1); // car length is 1.5 times width
	opp_shadow_front_left.x = opp_shadow_rear_left.x - zdiff;
	opp_shadow_front_left.z = opp_shadow_rear_left.z + xdiff;
	opp_shadow_front_right.x = opp_shadow_rear_right.x - zdiff;
	opp_shadow_front_right.z = opp_shadow_rear_right.z + xdiff;
	/* Don't need to add piece_x,piece_z as already have world position (from rear wheels) */

	// Add 128 to get z of opponent's front
	distance += 128;
	if (distance >= t.Track[piece].numSegments * 256)
	{
		// DIRECTION DEPENDANT

		distance -= t.Track[piece].numSegments * 256;

		// go to next piece
		piece++;
		if (piece > t.NumTrackPieces - 1) piece = 0;
	}
	// Fetch 4 surface co-ords surrounding front wheels
	segment = distance >> 8;
	sc = GetSurfaceCoords(t, piece, segment);

	sz = distance & 0xff; // z position of front wheel
	sx = opponents_road_x_position & 0xff; // x position of front wheel

	// Calculate front road y co-ordinate (as per Amiga opp.front.road.height)
	opp_front_road_pos_y = CalculateOpponentsRoadWheelHeight(sc, sx, sz);

	// Calculate front left road y co-ordinate
	sx = opponents_road_x_position - opponents_x_span; // x position of front left wheel
	opp_front_left_road_pos.y = CalculateOpponentsRoadWheelHeight(sc, sx, sz);

	// Calculate front right road y co-ordinate
	sx = opponents_road_x_position + opponents_x_span; // x position of front right wheel
	opp_front_right_road_pos.y = CalculateOpponentsRoadWheelHeight(sc, sx, sz);
}

static SurfaceCoords GetSurfaceCoords(const TrackState& t, const int32_t piece, int32_t segment)
{
	if (segment < 0 || segment >= t.Track[piece].numSegments)
	{
		PlatformShowError(L"GetSurfaceCoords(opponent) segment out of range", L"Error");
	}

	SurfaceCoords sc;
	sc.x2 = t.Track[piece].coords[(segment * 4)].x;
	sc.y2 = t.Track[piece].coords[(segment * 4)].y;
	sc.z2 = t.Track[piece].coords[(segment * 4)].z;

	sc.x3 = t.Track[piece].coords[segment * 4 + 1].x;
	sc.y3 = t.Track[piece].coords[segment * 4 + 1].y;
	sc.z3 = t.Track[piece].coords[segment * 4 + 1].z;

	segment++;
	sc.x1 = t.Track[piece].coords[(segment * 4)].x;
	sc.y1 = t.Track[piece].coords[(segment * 4)].y;
	sc.z1 = t.Track[piece].coords[(segment * 4)].z;

	sc.x4 = t.Track[piece].coords[segment * 4 + 1].x;
	sc.y4 = t.Track[piece].coords[segment * 4 + 1].y;
	sc.z4 = t.Track[piece].coords[segment * 4 + 1].z;

	return sc;
}

static OppSurfaceResult CalcSurfacePosition(const int32_t distance, const int32_t z_shift)
{
	int32_t surface_position = distance & 0xff;

	bool next_seg = false;
	surface_position += z_shift;
	if (surface_position >= 256)
	{
		next_seg = true;
		surface_position &= 0xff;
	}

	return {surface_position, next_seg};
}

static int32_t CalculateOpponentsRoadWheelHeight(const SurfaceCoords& sc, const int32_t sx, const int32_t sz)
{
	// calculate height of surface at x,z position using linear interpolation

	// i.e. calculate y at offset (sx, sz)
	//		given (sc.x1, sc.y1, sc.z1)
	//			  (sc.x2, sc.y2, sc.z2)
	//			  (sc.x3, sc.y3, sc.z3)
	//			  (sc.x4, sc.y4, sc.z4)

	// first do x interpolation
	const int32_t sya = sc.y1 + ((sx * (sc.y4 - sc.y1)) >> 8);
	const int32_t syb = sc.y2 + ((sx * (sc.y3 - sc.y2)) >> 8);

	// now do z interpolation
	const int32_t y = (syb << 8) + sz * (sya - syb);

	return y >> 9;
}


// Tested against Amiga
static void OpponentMovement(const TrackState& t, GameState& game)
{
	static int32_t byte_count = 0;

	if (!game.drop_start_done)
		return;

	UpdateOpponentsActualWheelHeights(t, game);
	RandomizeOpponentsSteering(game, t);
	GetOpponentsEngineAcceleration();
	AdjustOpponentsEngineAcceleration(game, t);
	UpdateOpponentsZSpeed(t, game);

	// Advance opponent along the track
	int32_t value = (opponents_z_speed * (t.Track[game.opponents_current_piece].lengthReduction << 7)) << 1;
	value >>= 16;
	value *= REDUCTION;
	value >>= 8;
	value <<= 3;
	const int32_t byte = value & 0xff;
	value >>= 8;
	byte_count += byte;
	if (byte_count > 0xff)
	{
		++value;
		byte_count &= 0xff;
	}
	opponents_distance_into_section += value;

	if (opponents_distance_into_section >= t.Track[game.opponents_current_piece].numSegments * 256)
	{
		// DIRECTION DEPENDANT

		opponents_distance_into_section -= t.Track[game.opponents_current_piece].numSegments * 256;

		// go to next piece
		game.opponents_current_piece++;
		if (game.opponents_current_piece > t.NumTrackPieces - 1) game.opponents_current_piece = 0;
	}
}

// Tested against Amiga
static void UpdateOpponentsActualWheelHeights(const TrackState& t, const GameState& game)
{
	int32_t height_adjust;

	opp_smallest_difference = -32768;

	if (t.Track[game.opponents_current_piece].type & 0x80) // curve
		height_adjust = 124; // increase collision when on a curve
	else
		height_adjust = 40;

	int32_t touching_road = 0;

	{
		const auto r = CalculateWheelDifference(opp_rear_left_road_pos.y,
		                                        opp_actual_height[REAR_LEFT],
		                                        height_adjust,
		                                        opp_old_rear_left_difference,
		                                        touching_road);
		opp_new_rear_left_difference = r.new_difference;
		opp_old_rear_left_difference = r.old_difference;
		touching_road = r.touching_road;
	}

	{
		const auto r = CalculateWheelDifference(opp_rear_right_road_pos.y,
		                                        opp_actual_height[REAR_RIGHT],
		                                        height_adjust,
		                                        opp_old_rear_right_difference,
		                                        touching_road);
		opp_new_rear_right_difference = r.new_difference;
		opp_old_rear_right_difference = r.old_difference;
		touching_road = r.touching_road;
	}

	{
		const auto r = CalculateWheelDifference(opp_front_road_pos_y,
		                                        opp_actual_height[FRONT],
		                                        height_adjust,
		                                        opp_old_front_difference,
		                                        touching_road);
		opp_new_front_difference = r.new_difference;
		opp_old_front_difference = r.old_difference;
		touching_road = r.touching_road;
	}

	opp_touching_road = touching_road != 0;

	// Make accelerations from 6 parts wheel difference in question and 1 part
	// of the other two wheels (only one central front wheel is considered)
	const int32_t total_diff = opp_new_rear_left_difference + opp_new_rear_right_difference + opp_new_front_difference;

	opp_y_acceleration[REAR_LEFT] = (total_diff + opp_new_rear_left_difference + (opp_new_rear_left_difference << 2)) >>
		3;
	opp_y_acceleration[REAR_RIGHT] = (total_diff + opp_new_rear_right_difference + (opp_new_rear_right_difference << 2))
		>> 3;
	opp_y_acceleration[FRONT] = (total_diff + opp_new_front_difference + (opp_new_front_difference << 2)) >> 3;

	// Randomly make opponent do a wheelie (if they have that attribute)
	if (opponent_attributes[game.opponentsID] & WHEELIE)
	{
		int32_t i = opp_y_speed[FRONT] | opp_y_acceleration[FRONT];
		if ((i & 0xfffc) == 0) // If front of car isn't moving much vertically
		{
			i = rand() & 0xf;
			if (i == 0)
				opp_y_speed[FRONT] = 160; // Make opponent do a wheelie
		}
	}

	// Update all wheel y speeds and heights
	for (int32_t w = 0; w < NUM_OPP_WHEEL_POSITIONS; w++)
	{
		const int32_t acceleration = (opp_y_acceleration[w] * REDUCTION) >> 8;
		opp_y_speed[w] += acceleration;
		opp_actual_height[w] += (opp_y_speed[w] * REDUCTION) >> 9;
	}

	// Limit movement of opponent's wheels
	const int32_t diff = LimitOpponentWheels(296, REAR_LEFT, REAR_RIGHT);

	if (diff < 0)
		// Use rear right wheel (because this is higher than rear left)
		LimitOpponentWheels(368, REAR_RIGHT, FRONT);
	else
		LimitOpponentWheels(368, REAR_LEFT, FRONT);
}

static WheelDiffResult CalculateWheelDifference(const int32_t road_height,
                                                const int32_t actual_height,
                                                const int32_t height_adjust,
                                                const int32_t old_difference,
                                                int32_t touching_road)
{
	int32_t new_difference = road_height - actual_height;
	if (new_difference > opp_smallest_difference)
		opp_smallest_difference = new_difference;

	new_difference += height_adjust;
	if (new_difference < 0)
	{
		// wheel above road
		if (new_difference < -96) new_difference = -96; // set to maximum amount above road
	}

	int32_t amount_below_road = new_difference - old_difference;
	amount_below_road = ((amount_below_road * INCREASE) >> 8) + new_difference;

	if (amount_below_road < 0) amount_below_road = 0;
	if (amount_below_road > 1023) amount_below_road = 1023;

	touching_road |= amount_below_road;

	amount_below_road -= height_adjust;
	return {amount_below_road, new_difference, touching_road};
}

// Adjusts opponent wheel heights and y speeds to limit car's x and z angle
// Especially important when in the air on more extreme tracks (e.g. Roller Coaster)
static int32_t LimitOpponentWheels(const int32_t max_difference, const int32_t wheel1, const int32_t wheel2)
{
	const int32_t diff = opp_actual_height[wheel1] - opp_actual_height[wheel2];

	const int32_t drop = max_difference - abs(diff);
	if (drop < 0)
	{
		// Drop highest wheel
		if (diff >= 0)
			opp_actual_height[wheel1] += drop;
		else
			opp_actual_height[wheel2] += drop;

		if (wheel2 != FRONT)
		{
			// Get here on first call to function
			// Average rear wheel y speeds
			AverageWheelYSpeeds(REAR_LEFT, REAR_RIGHT);
			return diff;
		}

		// Average rear wheel y speeds
		AverageWheelYSpeeds(REAR_LEFT, REAR_RIGHT);
		// Average front and rear wheels y speeds (both rear wheel values are currently the same)
		AverageWheelYSpeeds(FRONT, REAR_RIGHT);
		// Average rear wheels y speeds again
		AverageWheelYSpeeds(REAR_LEFT, REAR_RIGHT);
	}

	if (wheel2 != FRONT)
		return diff; // Finish if first call to function

	// Following is reached on second call to function
	if (opp_touching_road)
		return diff;

	// Adjust wheel y speeds when opponent in air, possibly to make the car pitch forwards
	const int32_t speed_diff = opp_y_speed[wheel1] - opp_y_speed[FRONT];
	if (speed_diff < 16)
	{
		static constexpr int32_t y_speed_adjustments[] = {4, 4, -4};

		for (int32_t i = 0; i < NUM_OPP_WHEEL_POSITIONS; i++)
		{
			opp_y_speed[i] += y_speed_adjustments[i];
		}
	}

	return diff;
}

static void AverageWheelYSpeeds(const int32_t wheel1, const int32_t wheel2)
{
	const int32_t average = (opp_y_speed[wheel1] + opp_y_speed[wheel2]) >> 1;
	opp_y_speed[wheel1] = average;
	opp_y_speed[wheel2] = average;
}

static int32_t opp_prev_piece_type = 0;
static int32_t opp_steering_table_offset = 0;
static int32_t opp_steering_z_delta = 0;

static int32_t steering_sine_table[] =
{
	0x20, 0x50, 0x60, 0x70, 0x70, 0x60, 0x50, 0x20,
	-0x20, -0x50, -0x60, -0x70, -0x70, -0x60, -0x50, -0x20
};

// Tested against Amiga
static void RandomizeOpponentsSteering(const GameState& game, const TrackState& t)
{
	int32_t d2;

	if (!opp_touching_road)
		return;

	int32_t d1 = 0;
	opp_steering[STEER_LEFT] = opp_steering[STEER_RIGHT] = 0;
	opp_steering_z_delta = 0;
	int32_t d0 = opp_steering[STEER_COUNT];
	if (d0)
	{
		// Active steering count: apply random steering adjustments
		opp_steering[STEER_COUNT] -= 1;

		d0 += opp_steering_table_offset;
		d0 &= 0xf;
		d2 = d0;
		d0 = steering_sine_table[d2];
		if (d0 < 0)
		{
			d0 = -d0;
			d1++;
		}
		opp_steering[d1] = d0;

		d2 += 5;
		d2 &= 0xf;
		d0 = steering_sine_table[d2];
		opp_steering_z_delta = d0;
	}
	else
	{
		// No active steering: possibly initiate new random steering
		d2 = game.opponents_current_piece;
		if (!(opponents_speed_values[t.TrackID][d2] & 0x80) // not a forced-speed piece
			&& !game.opponent_behind_player
			&& !(t.Track[game.opponents_current_piece].type & 0x80) // not a curve
			&& opp_prev_piece_type & 0x80) // previous piece was curved
		{
			d2 = 8;
			if (opp_prev_piece_type & 0x40) // diagonal piece (45 degrees)
				d2 = 16;

			opp_steering_table_offset = d2;

			const int32_t value = rand() & 0x1f;
			if (game.opponentsID >= value)
				opp_steering[STEER_COUNT] = 16;
		}
	}

	d0 = t.Track[game.opponents_current_piece].oppositeDirection ? 0x40 : 0;
	d0 ^= t.Track[game.opponents_current_piece].type;
	opp_prev_piece_type = d0;
}

// Tested against Amiga
static void GetOpponentsEngineAcceleration()
{
	int32_t power = opp_engine_power;

	if (opp_steering[STEER_COUNT] != 0)
		power -= 25;

	if (opp_touching_road)
		opponents_engine_z_acceleration = power;
	else
		opponents_engine_z_acceleration = 0;
}

// Tested against Amiga
static void AdjustOpponentsEngineAcceleration(const GameState& game, const TrackState& t)
{
	if (!opp_touching_road)
		return;

	const int32_t speed_value = opponents_speed_values[t.TrackID][game.opponents_current_piece];
	int32_t speed = speed_value;
	if ((speed & 0x80) == 0)
	{
		if (speed > opponents_max_speed)
			speed = opponents_max_speed;
	}

	const int32_t opponents_required_z_speed = speed & 0x7f;

	speed = opponents_z_speed >> 8;
	speed -= opponents_required_z_speed;
	if (speed == 0)
	{
		opponents_required_z_speed_reached = true;
		return;
	}
	if (speed > 0)
	{
		// Speed is greater than required speed
		opponents_required_z_speed_reached = true;
		opponents_engine_z_acceleration = -opponents_engine_z_acceleration;
		if (speed < 14)
			return;
	}

	if (speed_value & 0x80 || speed >= 0 || !opponents_required_z_speed_reached)
	{
		opponents_engine_z_acceleration <<= 1;
		return;
	}

	if (speed >= -2)
		return; // Value is -2 or -1

	opponents_required_z_speed_reached = false;
	opponents_engine_z_acceleration <<= 1;
}

// Tested against Amiga
static void UpdateOpponentsZSpeed(const TrackState& t, const GameState& game)
{
	int32_t acceleration_adjust = 0, a;

	if (opponents_z_speed >= 0)
	{
		int32_t s = opponents_z_speed >> 7;

		// Reduce speed value if opponent close behind player
		if (game.player_close_to_opponent && game.opponent_behind_player)
		{
			s -= 20;
			if (s < 0) s = 0;
		}

		// A fraction of the square of the speed is subtracted from acceleration
		// This only has a small effect
		acceleration_adjust = ((opponents_z_speed >> 8) * s) >> 6;

		// Reduce the acceleration further if on the road
		if (opp_touching_road)
		{
			if (opponents_engine_z_acceleration >= 0)
			{
				// Subtract fraction of speed from acceleration
				s = opponents_z_speed >> 8;
				a = opponents_engine_z_acceleration - s;

				if (t.Track[game.opponents_current_piece].type & 0x80) // curve
				{
					// Subtract again when on a curve, then reduce further
					a -= s;
					a -= 35;
				}
				opponents_engine_z_acceleration = a;
			}
		}
	}

	a = opponents_engine_z_acceleration - acceleration_adjust;
	if (opp_touching_road)
	{
		int32_t d = (opp_rear_left_road_pos.y + opp_rear_right_road_pos.y) >> 1;
		d -= opp_front_road_pos_y;
		// d is -'ve when opponent pitched backwards, +'ve when pitched forwards

		int32_t pitch = abs(d);
		if (pitch >= 512) pitch = 510;

		pitch >>= 1; // pitch value / 2
		int32_t adjust = pitch + (pitch >> 2); // (5 * pitch value) / 8

		if (d < 0) adjust = -adjust;

		// Acceleration is reduced when opponent pitched backwards, increased when pitched forwards
		// i.e. effect of gravity
		a += adjust;
	}

	const int32_t acceleration = (a * REDUCTION) >> 8;
	opponents_z_speed += acceleration;
	if (opponents_z_speed < 0) opponents_z_speed = 0;
}


static int32_t difference_between_players = 0;
static int32_t smallest_distance_between_players = 0;

static void CalculateDistancesBetweenPlayers(const TrackState& t, GameState& player)
{
	static int32_t distances_around_road[MAX_PIECES_PER_TRACK], total_road_distance;
	static int32_t previousTrackID = NO_TRACK;

	// Re-calculate road distances when track changes
	if (previousTrackID != t.TrackID)
	{
		int32_t distance = 0;
		for (int32_t piece = 0; piece < t.NumTrackPieces; piece++)
		{
			distances_around_road[piece] = distance << 5;

			distance += t.Track[piece].numSegments;
		}
		total_road_distance = distance << 5;

		previousTrackID = t.TrackID;
	}

	int32_t diff = (opponents_distance_into_section - player.players_distance_into_section) >> 3;
	diff += distances_around_road[player.opponents_current_piece] - distances_around_road[player.player_current_piece];
	// NOTE: following value can only be relied upon when player and opponent are on same piece
	// (it's wrong when they're on different sides of track start/end, e.g. opponent on piece 0, player on piece 43)
	difference_between_players = diff;

	const int32_t abs_diff = abs(diff);
	const int32_t opposite = total_road_distance - abs_diff; // difference between players in opposite direction

	// compare two road distances
	if (abs_diff < opposite)
	{
		// get smallest distance
		smallest_distance_between_players = abs_diff;
		diff = -diff;
	}
	else
		smallest_distance_between_players = opposite;

	player.opponent_behind_player = diff > 0;
}


int32_t CalculateIfWinning(const TrackState& track, const GameState& game,
                           const int32_t start_finish_piece)
{
	int32_t result = game.lapNumber[OPPONENT] - game.lapNumber[PLAYER];
	if (result != 0) // on different laps
		return result;

	int32_t p = game.player_current_piece - start_finish_piece;
	if (p < 0)
		p += track.NumTrackPieces;

	int32_t o = game.opponents_current_piece - start_finish_piece;
	if (o < 0)
		o += track.NumTrackPieces;

	result = o - p;
	if (result != 0) // on different pieces
		return result;

	result = difference_between_players;
	return result;
}

static int32_t collision_bounce_count = 0, collision_z_calculated = 0;
static int32_t x_difference, player_to_right;
static int32_t cars_collided;
static int32_t car_to_car_x_acceleration, car_to_car_y_acceleration, car_to_car_z_acceleration;


static void CarToCarCollisionDetection(GameState& player)
{
	int32_t d0, d3;

	if (!player.drop_start_done)
		return;

	// Y difference check (when at least one car is not touching road)
	if (!opp_touching_road || !player.touching_road)
	{
		const int32_t players_smaller_y = player.player_y >> 11;
		d0 = players_smaller_y - opp_actual_height[REAR_LEFT];
		const int32_t d4 = d0;
		d0 += 40;
		d0 = abs(d0);

		if (d0 >= 192)
		{
			collision_bounce_count = 3;
			return;
		}

		if (collision_bounce_count)
		{
			--collision_bounce_count;
			d3 = 256 - d0;
			if (d4 < 0)
				d3 = -d3;

			d3 <<= 4;
			car_to_car_y_acceleration = d3;
		}
	}

	// X acceleration
	if (x_difference < 45 && (smallest_distance_between_players & 0xff) <= 8)
	{
		car_to_car_x_acceleration = player_to_right ? 0x800 : -0x800;
	}

	// Z acceleration
	if (!(collision_z_calculated & 0x80))
	{
		d3 = 3;
		d0 = opponents_z_speed - player.player_z_speed;
		if (d0 < 0)
			d3 = -3;

		d0 >>= 1;
		d0 += d3;
		car_to_car_z_acceleration = d0;
	}

	// Apply damage
	cars_collided = 0x80;
	collision_z_calculated = 0x80;

	const int32_t damage = (512 + abs(car_to_car_x_acceleration) + abs(car_to_car_y_acceleration)
		+ abs(car_to_car_z_acceleration)) >> 8;

	player.rear_damage = std::min(player.rear_damage + damage, 255);
	player.front_right_damage = std::min(player.front_right_damage + damage, 255);
	player.front_left_damage = std::min(player.front_left_damage + damage, 255);

	player.damaged = 0x80;
}

static int32_t cars_collided_delay = 0;


void CarToCarCollision(GameState& player, const SoundState& sound)
{
	if (cars_collided_delay > 0)
		--cars_collided_delay;

	if (!cars_collided)
		return;

	cars_collided = 0;

	int32_t d0 = opponents_z_speed - car_to_car_z_acceleration;
	if (d0 < 0) d0 = 0;
	opponents_z_speed = d0;

	d0 = car_to_car_y_acceleration >> 4;
	opp_y_speed[REAR_LEFT] -= d0;
	opp_y_speed[REAR_RIGHT] -= d0;
	opp_y_speed[FRONT] -= d0;

	player.car_collision_x_acceleration += car_to_car_x_acceleration;
	player.car_collision_y_acceleration += car_to_car_y_acceleration;
	player.car_collision_z_acceleration += car_to_car_z_acceleration;

	car_to_car_x_acceleration = 0;
	car_to_car_y_acceleration = 0;
	car_to_car_z_acceleration = 0;

	// Play collision sound if necessary 

	if (cars_collided_delay > 0)
		return;

	PlatformSoundPlay(sound.HitCarSoundBuffer, false); // not looping

	cars_collided_delay = 5;
}

static int32_t opponents_suggested_road_x_position;


// Tested against Amiga
static void OpponentPlayerInteraction(const TrackState& t, GameState& player)
{
	int32_t d2 = 0;
	player.player_close_to_opponent = false;

	int32_t d0 = opponents_road_x_position & 0xff;
	opponents_suggested_road_x_position = d0;

	d0 -= player.rear_wheel_surface_x_position;
	if (d0 < 0)
	{
		d0 = -d0;
		--d2; // flag that player is to right of opponent
	}
	x_difference = d0;
	player_to_right = d2;

	bool applyCurveCheck = true;

	auto setFarAwayPosition = [&]()
	{
		// Player and opponent at least $100 from each other (ahead or behind)
		d2 = 64;
		if (opponent_attributes[player.opponentsID] & DRIVES_NEAR_EDGE)
			d2 = 110;

		if (player.opponentsID & 1)
			d2 = 255 - d2; // to other side of road

		opponents_suggested_road_x_position = d2;
	};

	if (smallest_distance_between_players >> 8 != 0)
	{
		setFarAwayPosition();
	}
	else
	{
		// Player and opponent are within $100 of each other (ahead or behind)
		d0 = smallest_distance_between_players;

		// Determine if player is close to opponent
		// Either: Opponent less than 64 behind player
		// OR Player less than 64 behind and less than 50 to the left or right of opponent
		if (d0 < 64 && (player.opponent_behind_player || x_difference < 50))
			player.player_close_to_opponent = true;

		// Check for car-to-car collision (very close proximity)
		bool collisionDetected = false;
		if (d0 < 16 && x_difference < 50)
		{
			const int32_t roadPosHigh = player.players_road_x_position >> 8;
			if (roadPosHigh < 1 || (roadPosHigh == 1 && (player.players_road_x_position & 0xff) < 0x80))
			{
				CarToCarCollisionDetection(player);
				collisionDetected = true;
			}
		}

		if (!collisionDetected)
		{
			collision_bounce_count = 0;
			collision_z_calculated = 0;
			d0 = smallest_distance_between_players & 0xff;
		}

		if (collisionDetected || (smallest_distance_between_players & 0xff) < 24)
		{
			// Close range: move to one side or set far away position
			if (opponent_attributes[player.opponentsID] & DRIVES_NEAR_EDGE
				&& !player.opponent_behind_player
				&& (smallest_distance_between_players & 0xff) >= 14)
			{
				setFarAwayPosition();
			}
			else
			{
				MoveOpponentToOneSide();
				applyCurveCheck = false;
			}
		}
		else
		{
			// Medium-to-far range behavior (distance >= 24)
			if (player.opponent_behind_player)
			{
				OpponentPushPlayer(player);
				applyCurveCheck = false;
			}
			else if (d0 < 50)
			{
				if (opponent_attributes[player.opponentsID] & OBSTRUCTS_PLAYER)
				{
					// put opponent at same position as player
					opponents_suggested_road_x_position = player.rear_wheel_surface_x_position;
				}
				else
				{
					OpponentPushPlayer(player);
				}
			}
			else if (d0 < 200 && opponent_attributes[player.opponentsID] & PUSH_PLAYER)
			{
				// opponent pushing player off track
				OpponentPushPlayer(player);
			}
			else
			{
				setFarAwayPosition();
			}
		}
	}

	// Move opponent to middle of road if approaching or on a curve
	if (applyCurveCheck)
	{
		int32_t piece = player.opponents_current_piece;
		for (int32_t count = 2; count > 0; count--)
		{
			d0 = GetPieceAngleAndTemplate(piece);
			d0 &= 0xf; // templateNum
			if (t.sections_car_can_be_put_on[d0] & 0x80)
				opponents_suggested_road_x_position = 128; // middle of road

			// go to next piece
			piece++;
			if (piece > t.NumTrackPieces - 1) piece = 0;
		}
	}

	// Steering adjustment: gradually steer toward suggested position
	int32_t steerDelta = opp_steering_z_delta;
	if (steerDelta == 0)
	{
		steerDelta = opponents_suggested_road_x_position - (opponents_road_x_position & 0xff);
	}

	if (steerDelta == 0)
		return;

	if (steerDelta < 0)
	{
		if (steerDelta >= -16)
			return;
		d0 = -9;
	}
	else
	{
		if (steerDelta < 16)
			return;
		d0 = 9;
	}

	d0 += opponents_road_x_position & 0xff;

	if (!opp_touching_road)
		return;

	if (d0 < 0) //temp, remove
		PlatformShowError(L"Less than 0", L"Error"); //temp
	if (d0 >= 225)
		return;

	if (d0 < 32)
		return;

	opponents_road_x_position = d0;
}

// Tested against Amiga
// position opponent on left or right
static void MoveOpponentToOneSide()
{
	const int32_t d0 = x_difference;

	if (d0 >= 56)
		return;
	if (player_to_right & 0x80)
		opponents_suggested_road_x_position = 32;
	else
		opponents_suggested_road_x_position = 256 - 32;
}

// Tested against Amiga
// opponent pushing player off track
static void OpponentPushPlayer(const GameState& player)
{
	int32_t d0 = x_difference;

	if (d0 >= 56)
		return;

	d0 = player.rear_wheel_surface_x_position;

	if (player_to_right & 0x80)
	{
		if (d0 < 96)
			opponents_suggested_road_x_position = 256 - 32;
		else
			opponents_suggested_road_x_position = 32;
	}
	else
	{
		if (d0 >= 256 - 96)
			opponents_suggested_road_x_position = 32;
		else
			opponents_suggested_road_x_position = 256 - 32;
	}
}

int32_t CalculateOpponentsDistance(const GameState& game)
{
	// should do every fourth frame

	int32_t dist = smallest_distance_between_players;
	dist += dist >> 2;
	dist >>= 2;

	if (game.opponent_behind_player)
		dist = -dist;

	return dist;
}
