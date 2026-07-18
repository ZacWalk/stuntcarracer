// game.drive.cpp — Car physics and player driving behaviour.
//
// NOTE: All statics and globals that have been initialised will need to be
//       reinitialised when the car is repositioned on the track.
//
// Coordinate conventions (all render-space, double):
//   game.player_x, game.player_z — horizontal world position (same scale as
//       CarPose.x/z, the COORD_3D track coords, and WORLD_CUBE_SIZE).
//   game.player_y               — vertical position with the renderer's
//       "negative-up" convention (matches CarPose.y, viewpoint1_y, etc.).
//   Wheel road_height / actual_height values remain in the legacy 256-per-
//       render-Y "road-height units" — they're sampled directly from the
//       interpolated track Y (CalculateWorldRoadHeight returns y/256).
//   Angles are doubles in MAX_ANGLE units; convert with ANGLE_TO_RADIANS
//       before feeding std::sin / std::cos.

#include "platform.h"
#include "game.h"


// WRECKED/NOT_WRECKED kept as macros (they reference a local variable)
#define	WRECKED			(game.wreck_wheel_height_reduction != 0)
#define	NOT_WRECKED		(game.wreck_wheel_height_reduction == 0)

static void CarControl(GameState& game, uint32_t input);
static void BoostPower(GameState& game, int32_t boost_flag,
                       int32_t accel_input,
                       int32_t brake_input);

struct PieceResult
{
	bool found;
	int32_t piece;
};

struct RelativeXZ
{
	double rx;
	double rz;
};

struct OffRoadResult
{
	double distance;
	double ex;
	double ez;
};

struct SurfaceResult
{
	double sx;
	double sz;
	double road_x;
	int32_t segment;
};

struct WheelCollisionResult
{
	double height_difference;
	double old_difference;
	double amount_below_road;
	int32_t damage;
};

struct InclinationResult
{
	double sin;
	double cos;
};

struct CurveMeasurements
{
	double y_angle;
	double radius;
	double distance_from_centre;
};

static void CarMovement(TrackState& track, GameState& game, const SoundState& sound);
static PieceResult GetPieceUsingMap(const TrackState& track, double x, double z);
static RelativeXZ CalcXZRelativeToPiece(const TrackState& track, double x, double z, int32_t piece);

static void CalculateWheelXZOffsets(GameState& player, const RotationMatrix& rot);

static void CalculateRoadWheelHeights(TrackState& track, GameState& player);
static double CalculateRoadWheelHeight(GameState& player, double height, double prev_height);
static double CalculateIfCarOffRoad(GameState& player, double height);
static double CalculateWorldRoadHeight(TrackState& track, GameState& player, int32_t wheel, double x, double z);

struct SurfaceCoords
{
	double x1, y1, z1;
	double x2, y2, z2;
	double x3, y3, z3;
	double x4, y4, z4;
};

static SurfaceCoords GetSurfaceCoords(const TrackState& track, int32_t piece, int32_t segment);
static OffRoadResult CalcDistanceOffRoad(double x, double z,
                                         double ox, double oz,
                                         double ux, double uz,
                                         double vx, double vz);
static SurfaceResult CalcSurfacePosition(const TrackState& track, int32_t piece,
                                         double x, double z,
                                         double ox, double oz,
                                         double ux, double uz,
                                         double vx, double vz);

static void CalculateActualWheelHeights(GameState& player);
static void CalculateXZSpeeds(GameState& player, const RotationMatrix& rot);
static void CalculateGravityAcceleration(GameState& player, const RotationMatrix& rot);
static void CarCollisionDetection(const TrackState& track, GameState& player, const SoundState& sound);
static WheelCollisionResult CalculateWheelCollision(GameState& player, double road_height,
                                                    double actual_height,
                                                    double old_difference,
                                                    double amount_below_road,
                                                    int32_t damage);
static void CalculateCarCollisionAcceleration(GameState& player, double average_amount_below_road);
static InclinationResult CalculateInclinationSinCos(double inclination_in);
static void LiftCarOntoTrack(GameState& player);
static void EnforceTrackFloor(TrackState& track, GameState& player);

static void CalculateTotalAcceleration(GameState& player);
static double GetTwiceCollisionYAcceleration(const GameState& player);
static void CalculateXAcceleration(GameState& player);

static void CalculateSteering(const TrackState& track, GameState& player);
static void CalculateSteeringAcceleration(GameState& player, int32_t steering_amount);
static void AlignCarWithRoad(GameState& player);
static void AdjustSteeringAcceleration(GameState& player);
static int32_t IdentifyPiece(const TrackState& track, double x, double z, int32_t piece_hint);
static void GetPieceCoords(const TrackState& track, int32_t piece);

static void CalculateWorldAcceleration(GameState& player, const RotationMatrix& rot);
static void ReduceWorldAcceleration(GameState& game);

static void CalculateXZRotationAcceleration(GameState& player);
static void UpdatePlayersRotationSpeed(GameState& player);
static void CalculateFinalRotationSpeed(GameState& player);
static void UpdatePlayersWorldSpeed(GameState& player);
static void UpdatePlayersPosition(GameState& player);

static double CalcSectionYAngle(const TrackState& track, int32_t piece,
                                double x,
                                double z);
static CurveMeasurements CalcCurveMeasurements(const TrackState& track, int32_t piece,
                                               double x,
                                               double z);

static void PositionCarAbovePiece(TrackState& track, GameState& player, int32_t piece);
static void UpdateEngineRevs(const GameState& player);
static void DrawDustClouds(const GameState& player, const SoundState& sound);
static void DrawSparks(const GameState& game, const SoundState& sound);

void ResetPlayer(GameState& player)
{
	// resets almost everything at the moment, just to make sure
	player.player_x = 0;
	player.player_y = 0;
	player.player_z = 0;

	player.player_x_angle = 0;
	player.player_y_angle = 0;
	player.player_z_angle = 0;

	player.player_world_x_speed = 0;
	player.player_world_y_speed = 0;
	player.player_world_z_speed = 0;

	// calculated
	player.player_x_speed = 0;
	player.player_y_speed = 0;
	player.player_z_speed = 0;

	player.accelerating = false;

	player.engine_power = 240; // (240 standard, 320 super)
	player.boost_unit_value = 16; // (16 standard, 12 super)

	// calculated
	player.left_right_value = 0;
	player.engine_z_acceleration = 0;
	player.boost_activated = 0;

	// calculated
	player.rear_wheel_x_offset = 0, player.rear_wheel_z_offset = 0;
	player.front_left_wheel_x_offset = 0, player.front_left_wheel_z_offset = 0;
	player.front_right_wheel_x_offset = 0, player.front_right_wheel_z_offset = 0;

	player.front_left_road_height = GameState::OFF_ROAD_HEIGHT;
	player.front_right_road_height = GameState::OFF_ROAD_HEIGHT;
	player.rear_road_height = GameState::OFF_ROAD_HEIGHT;
	// calculated
	player.front_left_actual_height = 0;
	player.front_right_actual_height = 0;
	player.rear_actual_height = 0;
	// calculated
	player.off_left = 0, player.off_right = 0;
	player.wheel_off_road = 0, player.distance_off_road = 0;
	player.at_side_byte = 0, player.which_side_byte = 0;
	player.smaller_limit_required = false;

	player.wreck_wheel_height_reduction = 0; // 0x200 if wrecked

	player.drop_start_done = true;
	player.touching_road = false;

	player.on_chains = false;
	player.chain_height_remaining = 0;

	// calculated
	player.player_distance_off_road = 0;
	player.off_map_status = 0;
	// calculated
	player.gravity_x_acceleration = 0;
	player.gravity_y_acceleration = 0;
	player.gravity_z_acceleration = 0;
	player.grounded_delay = 0;

	// calculated
	player.grounded_count = 0;
	player.damage_value = 0;

	player.damaged_count = 0;
	player.damaged = 0;

	player.front_left_amount_below_road = 0;
	player.front_right_amount_below_road = 0;
	player.rear_amount_below_road = 0;

	player.old_front_left_difference = 0;
	player.old_front_right_difference = 0;
	player.old_rear_difference = 0;

	player.front_left_damage = 0;
	player.front_right_damage = 0;
	player.rear_damage = 0;

	player.new_damage = 0;
	player.smashed_countdown = 0;

	// calculated
	player.car_collision_x_acceleration = 0;
	player.car_collision_y_acceleration = 0;
	player.car_collision_z_acceleration = 0;
	player.car_to_road_collision_z_acceleration = 0;

	// calculated
	player.player_x_acceleration = 0;
	player.player_y_acceleration = 0;
	player.player_z_acceleration = 0;

	// calculated
	player.total_world_x_acceleration = 0;
	player.total_world_y_acceleration = 0;
	player.total_world_z_acceleration = 0;

	player.player_x_rotation_speed = 0;
	player.player_y_rotation_speed = 0;
	player.player_z_rotation_speed = 0;

	// calculated
	player.player_final_x_rotation_speed = 0;
	player.player_final_y_rotation_speed = 0;
	player.player_final_z_rotation_speed = 0;

	// calculated
	player.player_x_rotation_acceleration = 0;
	player.player_y_rotation_acceleration = 0;
	player.player_z_rotation_acceleration = 0;
}


CarPose CarBehaviour(TrackState& track, GameState& game, const SoundState& sound,
                     const uint32_t input,
                     const double x,
                     const double y,
                     const double z,
                     const double x_angle,
                     const double y_angle,
                     const double z_angle)
{
	// CarPose and game.player_x/y/z all live in render-space world units now.
	// No boundary unit conversion is needed.

	// temporarily set game values to values provided when required
	if (game.INITIALISE_PLAYER)
	{
		game.INITIALISE_PLAYER = false;

		game.player_x = x;
		game.player_y = y;
		game.player_z = z;
		game.player_x_angle = x_angle;
		game.player_y_angle = y_angle;
		game.player_z_angle = z_angle;
	}

	// reset game on new race or after the car has fallen off the map
	if (game.off_track_count > GameState::OFF_TRACK_LIMIT ||
		game.bNewGame)
	{
		ResetPlayer(game);

		if (game.bNewGame)
		{
			// reset all animated objects
			ResetDrawBridge(track, game);
		}

		const int32_t restart_piece = game.off_track_count > GameState::OFF_TRACK_LIMIT
			                              ? game.player_current_piece
			                              : track.PlayersStartPiece;
		PositionCarAbovePiece(track, game, restart_piece);
		game.drop_start_done = false;
		game.on_chains = true;
		game.chain_height_remaining = 48.0; // render-space Y units (was 0xc00*256/LOCAL_Y_FACTOR = 0x30000 internal)

		// Initialize road heights properly to avoid corrupted averaging
		// (road heights were reset to OFF_ROAD_HEIGHT, which corrupts the
		// first frames of CalculateRoadWheelHeight's averaging)
		{
			const auto rot = CalcYXZTrigCoefficients(game.player_x_angle,
			                                         game.player_y_angle,
			                                         game.player_z_angle);
			const double saved_z_speed = game.player_z_speed;
			game.player_z_speed = 0xA00; // bypass averaging in CalculateRoadWheelHeight
			CalculateWheelXZOffsets(game, rot);
			CalculateRoadWheelHeights(track, game);
			game.player_z_speed = saved_z_speed;
		}

		game.off_track_count = 0;
	}

	CarControl(game, input);

	// Chain-controlled descent: override Y physics during the lowering phase
	const double saved_player_y = game.player_y;

	CarMovement(track, game, sound);

	if (game.on_chains)
	{
		// Restore Y to pre-physics value and apply constant descent rate instead.
		// This prevents the spring-damper collision system from launching the car
		// through the track due to the large initial displacement.
		game.player_y = saved_player_y;
		game.player_world_y_speed = 0;

		// Render-space Y is negative-up, so descending = INCREASING player_y.
		constexpr double CHAIN_DESCENT_RATE = 1.0;
		const double descent = std::min(game.chain_height_remaining, CHAIN_DESCENT_RATE);
		game.player_y += descent;
		game.chain_height_remaining -= descent;

		if (game.chain_height_remaining <= 0)
		{
			game.on_chains = false;
			game.drop_start_done = true;
		}
	}

	UpdateEngineRevs(game);

	if (game.touching_road && !game.on_chains) game.drop_start_done = true;

	CarPose result;

	// All of game.player_x/y/z are already in render-space.
	result.x = game.player_x;
	result.y = game.player_y;
	result.z = game.player_z;

	// Reverse x and z angle because StuntCarRacer's DrawWorld rotates around x and z in
	// the opposite direction to the trig. coefficients calculated by StuntCarRacer's
	// CarBehaviour (i.e. clockwise becomes anti-clockwise or vice-versa)
	result.x_angle = WrapAngle(-game.player_x_angle);
	result.y_angle = WrapAngle(game.player_y_angle);
	result.z_angle = WrapAngle(-game.player_z_angle);

	return result;
}

static constexpr int32_t Y_ADJUSTMENT_THRESHOLD = 0x480;

double LimitViewpointY(TrackState& track, GameState& player, double y)
{
	// y is in render-space world units (matches CarPose.y). Internally we
	// resample the wheel positions and, if a front wheel is clipping deep
	// into the road surface, return the render-space y that would put it
	// back at the road plane minus the threshold tolerance.
	const double saved_player_z_speed = player.player_z_speed;
	double ry = 0.0, ly = 0.0;

	// calculate required sin.cos values using player x, y and z angles
	const auto rot = CalcYXZTrigCoefficients(player.player_x_angle,
	                                         player.player_y_angle,
	                                         player.player_z_angle);

	player.player_z_speed = 0xA00; // prevent CalculateRoadWheelHeight from averaging current and previous heights

	CalculateWheelXZOffsets(player, rot);
	CalculateRoadWheelHeights(track, player);
	CalculateActualWheelHeights(player);

	player.player_z_speed = saved_player_z_speed; // restore original value

	const double sin_x = std::sin(player.player_x_angle * ANGLE_TO_RADIANS);
	const double sin_z = std::sin(player.player_z_angle * ANGLE_TO_RADIANS);

	// Inverse of CalculateActualWheelHeights' formula, expressed directly in
	// render-space y. The road_height → player_y conversion is /-16 (matching
	// the *-16 forward direction); the sin-offset coefficients are the
	// CalculateActualWheelHeights coefficients divided by -16:
	//   pitch (sin_x * 2048) / -16 → -128
	//   roll  (sin_z * 1024) / -16 → -64
	if (player.front_right_road_height - player.front_right_actual_height > Y_ADJUSTMENT_THRESHOLD)
	{
		const double road_target = player.front_right_road_height - Y_ADJUSTMENT_THRESHOLD;
		ry = -road_target / 16.0 + sin_x * 128.0 - sin_z * 64.0;
	}

	if (player.front_left_road_height - player.front_left_actual_height > Y_ADJUSTMENT_THRESHOLD)
	{
		const double road_target = player.front_left_road_height - Y_ADJUSTMENT_THRESHOLD;
		ly = -road_target / 16.0 + sin_x * 128.0 + sin_z * 64.0;
	}

	if (ry && ly)
		y = (ry + ly) / 2.0; // average of two render-space y candidates
	else if (ry)
		y = ry;
	else if (ly)
		y = ly;

	return y;
}

static void CarControl(GameState& game, const uint32_t input)
{
	//	Keys that control car are :-
	//			Left Arrow = left, Right Arrow = right
	//			Up Arrow = accelerate
	//			Ctrl + Up Arrow = accelerate + boost
	//			Down Arrow = brake/reverse

	const int32_t left = input & KEY_P1_LEFT,
	              right = input & KEY_P1_RIGHT,
	              boost = input & KEY_P1_BOOST;

	game.accelerate = (input & KEY_P1_ACCEL) != 0;
	game.brake = input & KEY_P1_BRAKE;

	game.left_right_value = 0;
	if (game.touching_road && !game.on_chains)
	{
		if (left)
			game.left_right_value = -15;
		if (right)
			game.left_right_value = 15;
	}

	const int32_t boost_flag = !boost; // active low

	if (game.player_z_speed < 120 * 256 && !game.on_chains && NOT_WRECKED)
	{
		if (game.accelerate)
		{
			game.engine_z_acceleration = game.engine_power;
			game.accelerating = true;
		}
		else if (game.brake)
		{
			game.engine_z_acceleration = -240;
			game.accelerating = false;
		}
		else if (game.accelerating)
		{
			// car keeps accelerating even when control released
			game.engine_z_acceleration = game.engine_power;
			game.accelerating = true; // already true
		}
		else
			game.engine_z_acceleration = 0;
	}
	else
		game.engine_z_acceleration = 0;

	BoostPower(game, boost_flag,
	           game.accelerate,
	           game.brake);
}

static void BoostPower(GameState& game, const int32_t boost_flag,
                       const int32_t accel_input,
                       const int32_t brake_input)
{
	game.boost_activated = 0;

	if (!boost_flag && NOT_WRECKED)
	{
		if (game.accelerating || accel_input || brake_input)
		{
			if (game.boostReserve > 0)
			{
				--game.boostUnit;
				if (game.boostUnit < 0)
				{
					game.boostUnit = game.boost_unit_value;
					--game.boostReserve;
				}

				game.boost_activated = 0x80;
				game.engine_z_acceleration *= 2;
			}
		}
	}
}

static void CarMovement(TrackState& track, GameState& game, const SoundState& sound)
{
	// currently uses player_x/y/z and player_x/y/z_angle

	//calculate required sin.cos values using game x, y and z angles
	const auto rot = CalcYXZTrigCoefficients(game.player_x_angle,
	                                         game.player_y_angle,
	                                         game.player_z_angle);

	CalculateWheelXZOffsets(game, rot);
	CalculateRoadWheelHeights(track, game);
	CalculateActualWheelHeights(game);

	CalculateXZSpeeds(game, rot);

	CalculateGravityAcceleration(game, rot);
	CarCollisionDetection(track, game, sound);

	CalculateTotalAcceleration(game);
	CalculateSteering(track, game);
	CalculateWorldAcceleration(game, rot);
	ReduceWorldAcceleration(game);
	CalculateXZRotationAcceleration(game);
	UpdatePlayersRotationSpeed(game);
	CalculateFinalRotationSpeed(game);

	UpdatePlayersWorldSpeed(game);
	UpdatePlayersPosition(game);

	// Hard post-integration floor: the spring-damper LiftCarOntoTrack runs
	// inside CarCollisionDetection using height differences sampled BEFORE
	// the velocity integration. When the car moves rapidly into a rising
	// surface (e.g. driving into the base of a ramp, or landing on one),
	// the integrated XZ position can sample a much higher road than the
	// start-of-frame check ever saw, leaving the car visibly underneath the
	// track. Re-sample at the new position and clamp so the wheels can
	// never end up below the road surface.
	EnforceTrackFloor(track, game);

	// Set off-map flags when car is too far off road
	if (game.player_distance_off_road >= 256 - GameState::ROAD_WIDTH / 2)
	{
		game.off_map_status = 0x80;
	}
	else
	{
		game.off_map_status = 0;
		game.off_track_count = 0;
		game.smaller_limit_required = false;
	}

	// Off-map climb check: player_y is render-space negative-up, so values
	// MORE NEGATIVE than -4096 indicate the car has flown abnormally high
	// (was `player_y_internal >= 0x1000000` in the original physics units).
	if (game.off_map_status != 0 && game.touching_road && game.player_y > -4096.0)
	{
		game.off_track_count++;
		game.smaller_limit_required = true;
	}
}

static PieceResult GetPieceUsingMap(const TrackState& track, const double x, const double z)
{
	// locate the map square that the point is in
	const int32_t map_x = static_cast<int32_t>(std::floor(x / WORLD_CUBE_SIZE));
	const int32_t map_z = static_cast<int32_t>(std::floor(z / WORLD_CUBE_SIZE));

	if (map_x < 0 || map_x >= NUM_TRACK_CUBES ||
		map_z < 0 || map_z >= NUM_TRACK_CUBES)
	{
		// off the map
		return {false, 0};
	}

	// lookup piece number within map
	const int32_t piece = track.Track_Map[map_x][map_z];
	if (piece == -1)
	{
		// no piece at this place on the map
		return {false, 0};
	}

	return {true, piece};
}

static RelativeXZ CalcXZRelativeToPiece(const TrackState& track, const double x, const double z, const int32_t piece)
{
	// calculate x/z position of piece's front left corner, within world
	// (in render-space, matching the COORD_3D track coords and player_x/z).
	const double piece_x = track.Track[piece].x * WORLD_CUBE_SIZE;
	const double piece_z = track.Track[piece].z * WORLD_CUBE_SIZE;

	// calculate point's x/z position relative to the piece (and in same range)
	return {x - piece_x, z - piece_z};
}

static void CalculateWheelXZOffsets(GameState& player, const RotationMatrix& rot)
{
	// rear wheel is just (0, 0, -CAR_LENGTH/2) split into components.
	// Wheel offsets are in render-space units, matching player_x/z.
	constexpr double half_length = (-CAR_LENGTH / 2.0) * PC_FACTOR;
	player.rear_wheel_x_offset = rot[Z_X_COMP] * half_length;
	player.rear_wheel_z_offset = rot[Z_Z_COMP] * half_length;

	// front left wheel is just (-CAR_WIDTH/2, 0, CAR_LENGTH/2) split into components
	constexpr double half_width = (CAR_WIDTH / 2.0) * PC_FACTOR;
	constexpr double front_z = (CAR_LENGTH / 2.0) * PC_FACTOR;
	player.front_left_wheel_x_offset = rot[X_X_COMP] * -half_width + rot[Z_X_COMP] * front_z;
	player.front_left_wheel_z_offset = rot[X_Z_COMP] * -half_width + rot[Z_Z_COMP] * front_z;

	// front right wheel is just (CAR_WIDTH/2, 0, CAR_LENGTH/2) split into components
	player.front_right_wheel_x_offset = rot[X_X_COMP] * half_width + rot[Z_X_COMP] * front_z;
	player.front_right_wheel_z_offset = rot[X_Z_COMP] * half_width + rot[Z_Z_COMP] * front_z;
}

using WheelPositionType = enum
{
	FRONT_LEFT = 0,
	FRONT_RIGHT,
	REAR,
	NUM_WHEEL_POSITIONS,
	CENTRE // NOTE: This isn't a wheel position, but is used by CalculatePlayersRoadPosition
};

// VALUES FROM FOLLOWING ARE SLIGHTLY DIFFERENT TO AMIGA STUNT CAR RACER, BUT WORK OK

static void CalculateRoadWheelHeights(TrackState& track, GameState& player)
{
	COORD_3D wheel_pos[NUM_WHEEL_POSITIONS];

	player.at_side_byte = 0;

	// create array of wheel world x/z positions
	wheel_pos[FRONT_LEFT].x = player.front_left_wheel_x_offset + player.player_x;
	wheel_pos[FRONT_LEFT].z = player.front_left_wheel_z_offset + player.player_z;

	wheel_pos[FRONT_RIGHT].x = player.front_right_wheel_x_offset + player.player_x;
	wheel_pos[FRONT_RIGHT].z = player.front_right_wheel_z_offset + player.player_z;

	wheel_pos[REAR].x = player.rear_wheel_x_offset + player.player_x;
	wheel_pos[REAR].z = player.rear_wheel_z_offset + player.player_z;

	// initialise heights to previous values (from globals)
	wheel_pos[FRONT_LEFT].y = player.front_left_road_height;
	wheel_pos[FRONT_RIGHT].y = player.front_right_road_height;
	wheel_pos[REAR].y = player.rear_road_height;

	// calculate world road height at wheel positions
	for (int32_t i = 0; i < NUM_WHEEL_POSITIONS; i++)
	{
		const double height = CalculateWorldRoadHeight(track, player, i, wheel_pos[i].x, wheel_pos[i].z);

		wheel_pos[i].y = CalculateRoadWheelHeight(player, height, wheel_pos[i].y);

		if (i == REAR)
			player.player_distance_off_road = std::abs(player.distance_off_road);
	}

	// store heights in global variables
	player.front_left_road_height = wheel_pos[FRONT_LEFT].y;
	player.front_right_road_height = wheel_pos[FRONT_RIGHT].y;
	player.rear_road_height = wheel_pos[REAR].y;
}

static double CalculateRoadWheelHeight(GameState& player, double height, const double prev_height)
{
	if (player.wheel_off_road)
		height = CalculateIfCarOffRoad(player, height);

	player.wheel_off_road = false;

	// Smoothing the road sample with the previous frame's value adds a
	// 1-frame lag, which lets the car visibly clip into rising surfaces
	// (the base of a ramp, jump landings) before the spring sees the new
	// height. The spring-damper itself already provides plenty of damping,
	// so use the raw sample once we're past the chain-drop intro - that's
	// the only phase that genuinely needs the average to settle without
	// the spring overshooting.
	if (player.drop_start_done)
		return height;

	// Bypass averaging on fast forward speed or steep car pitch (matches
	// the original Amiga StuntCarRacer behaviour during the chain drop).
	const double angle = player.player_x_angle < _180_DEGREES
		                     ? player.player_x_angle
		                     : player.player_x_angle - _360_DEGREES;
	if (std::abs(player.player_z_speed) >= 0xA00 || std::abs(angle) >= 0x600)
		return height;

	return (height + prev_height) / 2.0;
}

static double CalculateIfCarOffRoad(GameState& player, double height)
{
	// calculate how far the current wheel is off the left or right of the road
	const double x = std::abs(player.distance_off_road);

	if (x > 3 * CAR_WIDTH / 4)
	{
		// signal whole car is off road
		height = GameState::OFF_ROAD_HEIGHT;
		player.at_side_byte = player.at_side_byte >> 1 | 0x80;
	}
	else
	{
		// use the amount the wheel is off the road to drop the height of the wheel,
		// to make the car fall off the edge gradually (i.e. invisible sloping sides)
		height -= x * 16 + 0x100;

		if (height < GameState::OFF_ROAD_HEIGHT)
		{
			// signal whole car is off road
			height = GameState::OFF_ROAD_HEIGHT;
			player.at_side_byte = player.at_side_byte >> 1 | 0x80;
		}
		else
		{
			// store which side the car is falling off

			// logic here is different to Amiga StuntCarRacer, due to distance_off_road being different
			// from wheel.road.x.position and also plus.180.degrees not being used
			const int32_t w = static_cast<int32_t>(player.distance_off_road / 256.0);

			if (w & 0x80)
			{
				if (player.off_left)
					player.which_side_byte = 0x80; // left
				else if (player.off_right)
					player.which_side_byte = 0x40; // right
			}
		}
	}

	return height;
}

static double CalculateWorldRoadHeight(TrackState& track, GameState& player, int32_t wheel, double x, double z)
{
	// starts with the piece/surface that was used last time
	// this avoids locating the wrong map square,
	// e.g. for diagonal pieces that run into adjacent squares

	static int32_t piece = -1, segment = -1;
	static int32_t first_time = true, prevTrackID = NO_TRACK;
	SurfaceCoords sc = {};

	// Reset variables when the track changes
	if (track.TrackID != prevTrackID)
	{
		piece = -1;
		segment = -1;
		first_time = true;
		prevTrackID = track.TrackID;
	}

	//

	// Handle the case when the point has been off the road and then returned to an
	// entirely different area of the road (e.g. car placed back onto track in different place)
	auto pieceResult = GetPieceUsingMap(track, x, z);
	if (!pieceResult.found)
	{
		if (first_time)
		{
			// get the four (x,y,z) points for the first surface of the default piece
			piece = 0;
			segment = 0;
			sc = GetSurfaceCoords(track, piece, segment);
		}
	}
	else
	{
		int32_t this_piece = pieceResult.piece;
		if (first_time ||
			abs(this_piece - piece) > 1) // moved by more than one piece
		{
			// check the move is not from the last to first piece, or vice versa
			if (!(this_piece == track.NumTrackPieces - 1 && piece == 0) &&
				!(this_piece == 0 && piece == track.NumTrackPieces - 1))
			{
				// get the four (x,y,z) points for the current surface of the piece
				piece = this_piece;
				segment = 0;
				sc = GetSurfaceCoords(track, piece, segment);
			}
		}
	}

	first_time = false; // ensure flag is cleared

	// `sc` is a function-local that was zero-initialised above. The search loops
	// only refresh it when piece/segment is advanced, so when this function is
	// called repeatedly with the same piece (e.g. from CalculateRoadWheelHeights
	// for each wheel), `sc` would otherwise stay all-zero and CalcSurfacePosition
	// / interpolation would return 0 — placing the road plane at world Y=0 and
	// dropping the car through the track. Always load it for the current piece.
	if (piece >= 0 && segment >= 0)
		sc = GetSurfaceCoords(track, piece, segment);

	//

	// find the surface that the point is located within
	// first check point is not before or after surface (z direction)
	double xs, xp, zs, zp, rx = 0, rz = 0;
	int32_t before_surface = true, after_surface = true;

	// 'before surface' loop
	int32_t num_piece_changes = 0;
	while (before_surface)
	{
		// really only need to do following when piece changes, but do it always at the moment
		auto rel = CalcXZRelativeToPiece(track, x, z, piece);
		rx = rel.rx;
		rz = rel.rz;

		// calculate top dot product => before_surface
		xs = sc.x1 - sc.x4;
		zs = sc.z1 - sc.z4; // current segment vector
		xp = rx - sc.x4;
		zp = rz - sc.z4; // current point vector
		before_surface = xs * zp - xp * zs < 0;

		if (before_surface)
		{
			// future improvement: try a move in one direction,
			// if this is worse then move in other direction

			// DIRECTION DEPENDANT - WHOLE SECTION
			if (segment < track.Track[piece].numSegments - 1)
			{
				segment++;
			}
			else
			{
				// go to next piece if already at last surface
				piece++;
				if (piece > track.NumTrackPieces - 1) piece = 0;
				segment = 0;

				num_piece_changes++;
			}

			// get the four (x,y,z) points for the new surface
			sc = GetSurfaceCoords(track, piece, segment);
		}

		// prevent an infinite loop
		if (num_piece_changes >= track.NumTrackPieces)
		{
			break;
		}
	}

	// 'after surface' loop
	num_piece_changes = 0;
	while (after_surface)
	{
		// really only need to do following when piece changes, but do it always at the moment
		auto rel2 = CalcXZRelativeToPiece(track, x, z, piece);
		rx = rel2.rx;
		rz = rel2.rz;

		// calculate bottom dot product => after_surface
		xs = sc.x3 - sc.x2;
		zs = sc.z3 - sc.z2; // current segment vector
		xp = rx - sc.x2;
		zp = rz - sc.z2; // current point vector
		after_surface = xs * zp - xp * zs < 0;

		if (after_surface)
		{
			// future improvement: try a move in one direction,
			// if this is worse then move in other direction

			// DIRECTION DEPENDANT - WHOLE SECTION
			if (segment > 0)
			{
				segment--;
			}
			else
			{
				// go to previous piece if already at first surface
				piece--;
				if (piece < 0) piece = track.NumTrackPieces - 1;
				segment = track.Track[piece].numSegments - 1;

				num_piece_changes++;
			}

			// get the four (x,y,z) points for the new surface
			sc = GetSurfaceCoords(track, piece, segment);
		}

		// prevent an infinite loop
		if (num_piece_changes >= track.NumTrackPieces)
		{
			break;
		}
	}

	//

	// now know that point is between start edge and end edge of surface
	// find out if point is off left or right of surface

	if (wheel != CENTRE) // don't do this for CENTRE position (to allow road_x to include being off the road)
	{
		// calculate left dot product => off_left
		xs = sc.x2 - sc.x1;
		zs = sc.z2 - sc.z1; // current segment vector
		xp = rx - sc.x1;
		zp = rz - sc.z1; // current point vector
		player.off_left = xs * zp - xp * zs < 0;

		// calculate right dot product => off_right
		xs = sc.x4 - sc.x3;
		zs = sc.z4 - sc.z3; // current segment vector
		xp = rx - sc.x3;
		zp = rz - sc.z3; // current point vector
		player.off_right = xs * zp - xp * zs < 0;

		player.wheel_off_road = false;
		player.distance_off_road = 0;
		if (player.off_left || player.off_right)
		{
			// wheel is off road
			player.wheel_off_road = true;

			// need to make sure that road height at edge of surface is calculated
			// (i.e. point has to be within bounds of piece), therefore the local
			// point (rx/rz) is modified by the following, to be at the relevant edge

			OffRoadResult offRoad = {};
			if (player.off_left)
			{
				// get distance from and position at left edge
				offRoad = CalcDistanceOffRoad(rx, rz, sc.x2, sc.z2, sc.x1, sc.z1, sc.x3, sc.z3);
			}
			else if (player.off_right)
			{
				// get distance from and position at right edge
				offRoad = CalcDistanceOffRoad(rx, rz, sc.x3, sc.z3, sc.x4, sc.z4, sc.x2, sc.z2);
			}

			// note: following value is -'ve
			player.distance_off_road = offRoad.distance;

			rx = offRoad.ex;
			rz = offRoad.ez;
		}
	}

	//

	// calculate height of surface at x,z position using linear interpolation

	int32_t sx, sz, calculated_segment;
	// get distance from left edge / top edge, i.e. sx / sz
	calculated_segment = segment;

	if (wheel != CENTRE)
	{
		auto surf = CalcSurfacePosition(track, piece, rx, rz, sc.x2, sc.z2, sc.x1, sc.z1, sc.x3, sc.z3);
		sx = static_cast<int32_t>(surf.sx);
		sz = static_cast<int32_t>(surf.sz);
		calculated_segment = surf.segment;

		if (wheel == REAR)
		{
			// Reduce sx to (0 - 255)
			player.rear_wheel_surface_x_position = sx >> (GameState::LOG_SURFACE_SIZE - 8);
		}
	}
	else
	{
		// Called by CalculatePlayersRoadPosition
		// Set player_current_piece, player_current_segment, players_distance_into_section and players_road_x_position
		auto surf = CalcSurfacePosition(track, piece, rx, rz, sc.x2, sc.z2, sc.x1, sc.z1, sc.x3, sc.z3);
		sx = static_cast<int32_t>(surf.sx);
		sz = static_cast<int32_t>(surf.sz);
		calculated_segment = surf.segment;

		player.player_current_piece = piece;
		player.player_current_segment = calculated_segment;

		player.players_distance_into_section = calculated_segment * 256 + (sz >> (GameState::LOG_SURFACE_SIZE - 8));
		if (calculated_segment >= track.Track[piece].numSegments)
		{
			PlatformShowError(L"calculated_segment out of range", L"Error");
		}

		player.players_road_x_position = static_cast<int32_t>(surf.road_x);
	}

	// If the curve calculation output a different segment to the one identified
	//				earlier then the co-ordinates must be retrieved for the calculated segment.
	if (calculated_segment != segment)
	{
		segment = calculated_segment;
		// get the four (x,y,z) points for the new surface
		sc = GetSurfaceCoords(track, piece, segment);
	}

	// i.e. calculate y at offset (sx, sz)
	//		given (sc.x1, sc.y1, sc.z1)
	//			  (sc.x2, sc.y2, sc.z2)
	//			  (sc.x3, sc.y3, sc.z3)
	//			  (sc.x4, sc.y4, sc.z4)

	// first do x interpolation
	double sya = sc.y1 + sx * (sc.y4 - sc.y1) / static_cast<double>(GameState::SURFACE_SIZE);
	double syb = sc.y2 + sx * (sc.y3 - sc.y2) / static_cast<double>(GameState::SURFACE_SIZE);

	// now do z interpolation
	double y = syb * GameState::SURFACE_SIZE + sz * (sya - syb);

	// Compose the per-axis interpolation rescale (16) with the caller-side
	// rescale (1 / 4096) so the function returns ready-to-use "PC StuntCarRacer
	// magnitude" heights. Combined factor: 16 / 4096 = 1 / 256.
	return y / 256.0;
}

static SurfaceCoords GetSurfaceCoords(const TrackState& track, const int32_t piece, int32_t segment)
{
	const auto& t = track.Track[piece];

	if (segment < 0 || segment >= t.numSegments)
	{
		PlatformShowError(L"GetSurfaceCoords segment out of range", L"Error");
		return {};
	}

	SurfaceCoords sc;
	sc.x2 = t.coords[(segment * 4)].x;
	sc.y2 = t.coords[(segment * 4)].y;
	sc.z2 = t.coords[(segment * 4)].z;

	sc.x3 = t.coords[segment * 4 + 1].x;
	sc.y3 = t.coords[segment * 4 + 1].y;
	sc.z3 = t.coords[segment * 4 + 1].z;

	segment++;
	sc.x1 = t.coords[(segment * 4)].x;
	sc.y1 = t.coords[(segment * 4)].y;
	sc.z1 = t.coords[(segment * 4)].z;

	sc.x4 = t.coords[segment * 4 + 1].x;
	sc.y4 = t.coords[segment * 4 + 1].y;
	sc.z4 = t.coords[segment * 4 + 1].z;

	return sc;
}

static OffRoadResult CalcDistanceOffRoad(const double x, const double z,
                                         const double ox, const double oz,
                                         double ux, double uz,
                                         double vx, double vz)
{
	// ox, oz - origin point

	// z vector
	ux -= ox;
	uz -= oz;

	// x vector
	vx -= ox;
	vz -= oz;

	// calculate (perpendicular ?) distance from left or right edge
	// method is similar to that used when texture mapping
	double distance;

	const double v = (x - ox) * uz + (oz - z) * ux; // needs to be divided by denominator
	const double denominator = uz * vx - ux * vz;
	if (denominator == 0)
		distance = 0; // prevent division by zero
	else
		distance = v * GameState::ROAD_WIDTH / denominator;

	// calculate position where perpendicular meets edge of road
	OffRoadResult result;
	result.distance = distance;
	if (denominator == 0)
	{
		// prevent division by zero
		result.ex = x;
		result.ez = z;
	}
	else
	{
		result.ex = x - v * vx / denominator;
		result.ez = z - v * vz / denominator;
	}

	return result;
}

static SurfaceResult CalcSurfacePosition(const TrackState& track, const int32_t piece,
                                         const double x, const double z,
                                         const double ox, const double oz,
                                         double ux, double uz,
                                         double vx, double vz)
{
	SurfaceResult result = {};

	const auto& t = track.Track[piece];
	if (t.type & 0x80) // curve
	{
		double d;

		// This method treats curved pieces as true circular arcs
		// (based upon calculate.players.road.position) which removes the 'jitter' problem

		// must be a curve (type will be -'ve)
		const auto curve = CalcCurveMeasurements(track, piece, x, z);
		double piece_y_angle = curve.y_angle;
		const double radius = curve.radius;
		const double distance_from_centre = curve.distance_from_centre;

		// adjust for normal direction of travel
		if (t.oppositeDirection)
			piece_y_angle = MAX_ANGLE / 8 - piece_y_angle;

		// limit piece_y_angle to valid range
		if (piece_y_angle < 0) piece_y_angle = 0;
		if (piece_y_angle >= MAX_ANGLE / 8) piece_y_angle = MAX_ANGLE / 8 - 1;

		// calculate surface x position
		if (distance_from_centre < static_cast<double>(radius))
			d = static_cast<double>(radius) - distance_from_centre;
		else
			d = distance_from_centre - static_cast<double>(radius);

		double surface_x = d * GameState::SURFACE_SIZE / (GameState::ROAD_WIDTH * PC_FACTOR);
		result.road_x = d / PC_FACTOR;

		if (surface_x >= GameState::SURFACE_SIZE) surface_x = GameState::SURFACE_SIZE - 1;
		result.sx = surface_x;

		// calculate surface z position and output calculated segment
		const int32_t numSegments = t.numSegments;
		const double piece_z = piece_y_angle * GameState::SURFACE_SIZE * numSegments / (MAX_ANGLE / 8);

		result.sz = std::fmod(piece_z, static_cast<double>(GameState::SURFACE_SIZE));
		result.segment = static_cast<int32_t>(piece_z / GameState::SURFACE_SIZE);
		return result;
	}
	// straight or diagonal straight

	// For straight/diagonal pieces only — not used for curved pieces because it is only
	// accurate for rectangular segments

	// ox, oz - origin point

	// z vector
	ux -= ox;
	uz -= oz;

	// x vector
	vx -= ox;
	vz -= oz;

	// calculate (perpendicular ?) distance from left and top edge
	// method is similar to that used when texture mapping

	// left edge - calculate surface x position
	const double v = (x - ox) * uz + (oz - z) * ux; // needs to be divided by denominator
	double denominator = uz * vx - ux * vz;
	if (denominator == 0)
	{
		result.sx = 0; // prevent division by zero
		result.road_x = 0;
	}
	else
	{
		result.sx = v * GameState::SURFACE_SIZE / denominator;
		result.road_x = v * GameState::ROAD_WIDTH / denominator;
	}

	// Clamp sx to valid range
	if (result.sx < 0) result.sx = 0;
	if (result.sx >= GameState::SURFACE_SIZE) result.sx = GameState::SURFACE_SIZE - 1;

	// top edge - calculate surface z position
	const double u = (x - ox) * vz + (oz - z) * vx; // needs to be divided by denominator
	denominator = ux * vz - uz * vx;
	if (denominator == 0)
		result.sz = 0; // prevent division by zero
	else
		result.sz = u * GameState::SURFACE_SIZE / denominator;

	// Clamp sz to valid range
	if (result.sz < 0) result.sz = 0;
	if (result.sz >= GameState::SURFACE_SIZE) result.sz = GameState::SURFACE_SIZE - 1;

	return result;
}

static void CalculateActualWheelHeights(GameState& player)
{
	// Compute each wheel's vertical position in road_height units (256-per-
	// render-Y), starting from player_y (render-space, negative-up). The car
	// frame is half_x = 16 render-Y above the ground at zero pitch/roll, so the
	// magic-looking 16 multiplier is just "render-Y → road-height units". The
	// sin offsets shift each wheel relative to the car's pitch/roll, also in
	// road_height units (2048 per unit-sin for pitch, 1024 for roll).

	const double sin_x = std::sin(player.player_x_angle * ANGLE_TO_RADIANS);
	const double sin_z = std::sin(player.player_z_angle * ANGLE_TO_RADIANS);

	const double base_height = -player.player_y * 16.0;

	player.rear_actual_height = base_height - sin_x * 2048.0;

	player.front_right_actual_height = base_height + sin_x * 2048.0 - sin_z * 1024.0;

	player.front_left_actual_height = base_height + sin_x * 2048.0 + sin_z * 1024.0;
}

static void CalculateXZSpeeds(GameState& player, const RotationMatrix& rot)
{
	// this function basically does the same as RotateCoordinate.

	player.player_x_speed = player.player_world_x_speed * rot[X_X_COMP]
		+ player.player_world_y_speed * rot[X_Y_COMP]
		+ player.player_world_z_speed * rot[X_Z_COMP];

	player.player_y_speed = 0;

	player.player_z_speed = player.player_world_x_speed * rot[Z_X_COMP]
		+ player.player_world_y_speed * rot[Z_Y_COMP]
		+ player.player_world_z_speed * rot[Z_Z_COMP];
}

static void CalculateGravityAcceleration(GameState& player, const RotationMatrix& rot)
{
	// Gravity acts on the Y axis only. Therefore only Y components are used.

	// Acceleration along car's X axis
	player.gravity_x_acceleration = -GameState::GRAVITY_ACCELERATION * rot[X_Y_COMP];

	// Acceleration along car's Y axis
	player.gravity_y_acceleration = -GameState::GRAVITY_ACCELERATION * rot[Y_Y_COMP];

	// Acceleration along car's Z axis
	player.gravity_z_acceleration = -GameState::GRAVITY_ACCELERATION * rot[Z_Y_COMP];
}

static int32_t damaged_limit = 10; // Actually track/league dependant (could add to track data)

// NOTE: road_cushion_value is 0 for standard league and 1 for super league
//		 fourteen_frames_elapsed has value of 0 or -1 (set)
static int32_t road_cushion_value = 0, fourteen_frames_elapsed = 0;

// following are only global due to use by two functions - could be passed in instead
static double front_left_height_difference,
              front_right_height_difference,
              rear_height_difference;

static double front_difference_below_road,
              overall_difference_below_road;

static void CarCollisionDetection(const TrackState& track, GameState& player, const SoundState& sound)
{
	// local variables

	player.grounded_count = 0;
	player.damage_value = 0;
	player.damaged = 0;

	// Front left wheel collision
	{
		const auto r = CalculateWheelCollision(player, player.front_left_road_height,
		                                       player.front_left_actual_height,
		                                       player.old_front_left_difference,
		                                       player.front_left_amount_below_road,
		                                       player.front_left_damage);
		front_left_height_difference = r.height_difference;
		player.old_front_left_difference = r.old_difference;
		player.front_left_amount_below_road = r.amount_below_road;
		player.front_left_damage = r.damage;
	}

	// Front right wheel collision
	{
		const auto r = CalculateWheelCollision(player, player.front_right_road_height,
		                                       player.front_right_actual_height,
		                                       player.old_front_right_difference,
		                                       player.front_right_amount_below_road,
		                                       player.front_right_damage);
		front_right_height_difference = r.height_difference;
		player.old_front_right_difference = r.old_difference;
		player.front_right_amount_below_road = r.amount_below_road;
		player.front_right_damage = r.damage;
	}

	// Rear wheel collision
	{
		const auto r = CalculateWheelCollision(player, player.rear_road_height,
		                                       player.rear_actual_height,
		                                       player.old_rear_difference,
		                                       player.rear_amount_below_road,
		                                       player.rear_damage);
		rear_height_difference = r.height_difference;
		player.old_rear_difference = r.old_difference;
		player.rear_amount_below_road = r.amount_below_road;
		player.rear_damage = r.damage;
	}

	//

	const double average_front_amount_below_road = (player.front_left_amount_below_road + player.
		front_right_amount_below_road) / 2.0;
	const double average_amount_below_road = (average_front_amount_below_road + player.rear_amount_below_road) / 2.0;

	CalculateCarCollisionAcceleration(player, average_amount_below_road);

	double difference = (player.front_left_amount_below_road - player.front_right_amount_below_road) * 3;
	// limit to maximum
	if (difference > 0x1000) difference = 0x1000;
	if (difference < -0x1000) difference = -0x1000;
	front_difference_below_road = difference;

	//

	difference = average_front_amount_below_road - player.rear_amount_below_road;
	overall_difference_below_road = difference;

	//

	player.touching_road = average_amount_below_road != 0;

	if (!player.touching_road && !player.on_chains)
	{
		// get angle in Amiga StuntCarRacer format (i.e. correct sign)
		const double angle = player.player_x_angle < _180_DEGREES
			                     ? player.player_x_angle
			                     : player.player_x_angle - _360_DEGREES;

		if ((angle < 0 && (track.TrackID == ROLLER_COASTER || track.TrackID == SKI_JUMP))
			||
			angle >= 0)
		{
			difference = -128;

			// check roller coaster - don't need to do anything
			// check ski jump
			if (angle < 0 && track.TrackID == SKI_JUMP)
				difference = -8;

			if (angle >= 0x1000)
				difference = -256;

			difference -= overall_difference_below_road;
			if (difference < 0 && player.player_x_rotation_speed >= -256)
				overall_difference_below_road = difference;
		}
	}

	// following function won't do anything at first
	LiftCarOntoTrack(player);

	player.car_to_road_collision_z_acceleration = player.car_collision_z_acceleration;

	CarToCarCollision(player, sound);

	//

	// Play grounded sound if necessary 

	if (player.grounded_delay > 0) --player.grounded_delay;

	if (player.grounded_count == 0)
		return;

	int32_t amiga_volume = (player.damage_value >> 8) * 4;
	// minimum volume = 28, maximum volume = 64
	if (amiga_volume < 28) amiga_volume = 28;
	if (amiga_volume > 64) amiga_volume = 64;

	PlatformSoundSetVolume(sound.GroundedSoundBuffer, amiga_volume);

	if (player.grounded_delay == 0)
	{
		PlatformSoundPlay(sound.GroundedSoundBuffer, false); // not looping
		player.grounded_delay = 5;
	}
}

static WheelCollisionResult CalculateWheelCollision(GameState& player, const double road_height,
                                                    const double actual_height,
                                                    const double old_difference,
                                                    const double amount_below_road_in,
                                                    const int32_t damage_in)
{
	WheelCollisionResult result = {};
	result.height_difference = road_height - actual_height - player.wreck_wheel_height_reduction;

	double new_difference = result.height_difference;
	if (new_difference > 0x1400)
		new_difference = 0x1400;
	else if (new_difference < -0x300)
		new_difference = -0x300;

	double amount_below_road = new_difference - old_difference;
	amount_below_road = (amount_below_road * INCREASE) / 256.0 + new_difference;

	result.damage = damage_in;

	if (amount_below_road >= 0)
	{
		const double old_amount_below_road = amount_below_road_in;
		result.amount_below_road = amount_below_road;

		if (amount_below_road >= 0x400 && old_amount_below_road < 0x200)
			player.grounded_count++; // wheel grounded - update grounded wheel count

		int32_t damage = static_cast<int32_t>(result.amount_below_road) - road_cushion_value * 256;
		if (damage >= 0x700)
		{
			if (damage > player.damage_value)
				player.damage_value = damage;

			damage -= 0x600;
			if (fourteen_frames_elapsed == 0)
			{
				player.damaged_count++;
				if (player.damaged_count < damaged_limit)
				{
					damage /= 256;
					// NOTE next line may be unnecessary
					damage &= 0xff;
					damage += damage / 2;
					damage += result.damage;
					if (damage > 0xff) damage = 0xff;
					result.damage = damage;
					player.damaged = 0x80;
				}
			}
			if (result.amount_below_road >= 0x1200)
				result.amount_below_road = 0x11ff;
		}
		else
			player.damaged_count = 0;
	}
	else
	{
		result.amount_below_road = 0;
		player.damaged_count = 0;
	}

	result.old_difference = new_difference;
	return result;
}

static void CalculateCarCollisionAcceleration(GameState& player, const double average_amount_below_road)
{
	// average_amount_below_road is the force exerted by the road on the car.
	//
	// Force is directed through the Y axis of the road surface.  Therefore only
	// Y components are used.
	//
	// X acceleration = force * -cosx.sinz
	//
	// Y acceleration = force * cosx.cosz
	//
	// Z acceleration = force * sinx

	constexpr double car_length_factor = 16.0, car_width_factor = 8.0; // Length is twice the width

	// y_inclination_to_road is zero because road exists in X and Z planes only

	// Calculate x_inclination_to_road
	const double front_height_difference = (front_left_height_difference +
		front_right_height_difference) / 2.0;
	const double x_inclination_to_road = (front_height_difference -
		rear_height_difference) / car_length_factor;

	// Calculate sin and cos of X angle between car and road surface
	auto [surface_sinx, surface_cosx] = CalculateInclinationSinCos(x_inclination_to_road);

	// Calculate z_inclination_to_road
	const double z_inclination_to_road = (front_left_height_difference -
		front_right_height_difference) / car_width_factor;

	// Calculate sin and cos of Z angle between car and road surface
	auto [surface_sinz, surface_cosz] = CalculateInclinationSinCos(z_inclination_to_road);

	const double surface_cosx_cosz = surface_cosx * surface_cosz;
	const double surface_cosx_sinz = surface_cosx * surface_sinz;

	// Calculate car collision X acceleration
	const double surface_x = z_inclination_to_road < 0 ? -surface_cosx_sinz : surface_cosx_sinz;
	player.car_collision_x_acceleration = average_amount_below_road * surface_x;

	// Calculate car collision Y acceleration
	player.car_collision_y_acceleration = average_amount_below_road * surface_cosx_cosz;

	// Calculate car collision Z acceleration
	const double surface_z = x_inclination_to_road < 0 ? surface_sinx : -surface_sinx;
	player.car_collision_z_acceleration = average_amount_below_road * surface_z;
}

static InclinationResult CalculateInclinationSinCos(const double inclination_in)
{
	// inclination_in is effectively the sin of the inclination angle, in the
	// same 256-per-unit fixed-point scale as the height differences feeding it
	// (so a magnitude of 256 == sin = 1.0). The sign is handled by the caller
	// when applying the result.

	double s = std::abs(inclination_in) / 256.0;
	if (s > 1.0) s = 1.0;

	InclinationResult result;
	result.sin = s;
	result.cos = std::sqrt(1.0 - s * s);
	return result;
}

static void LiftCarOntoTrack(GameState& player)
{
	if (!player.drop_start_done) return;

	// Prevent the car from sinking too far below the road surface.
	// The spring-damper collision system has an equilibrium slightly below
	// the road; this function acts as a hard floor to limit penetration
	// during transients (jump landings, bumps, game start).
	const double avg_diff = (front_left_height_difference +
		front_right_height_difference +
		rear_height_difference) / 3.0;

	constexpr double MAX_BELOW_ROAD = 0x200;
	if (avg_diff > MAX_BELOW_ROAD)
	{
		// Lift the car (render-space negative-up: lifting = decreasing player_y).
		// avg_diff is in road_height units; /-16 converts to render-space y.
		player.player_y -= (avg_diff - MAX_BELOW_ROAD) / 16.0;

		if (player.player_world_y_speed < 0)
			player.player_world_y_speed /= 2;
	}
}

// Hard floor enforced AFTER the per-frame velocity integration. The spring
// LiftCarOntoTrack uses height differences from the start-of-frame sample,
// which can miss large XZ moves into a rising surface and leave the car
// underneath the track. This function re-samples the road at the new wheel
// positions and lifts the car so it can never end up below the surface.
static void EnforceTrackFloor(TrackState& track, GameState& player)
{
	if (!player.drop_start_done) return;

	// Save the spring-filter state so re-running CalculateRoadWheelHeights
	// here does not corrupt the next frame's averaging input.
	const double saved_fl = player.front_left_road_height;
	const double saved_fr = player.front_right_road_height;
	const double saved_rr = player.rear_road_height;
	const double saved_z_speed = player.player_z_speed;

	// Bypass CalculateRoadWheelHeight's averaging so we get the raw road
	// height at the *current* (post-integration) wheel positions. Otherwise
	// the lagged value can leave us still clipping into a rising ramp.
	player.player_z_speed = 0xA00;

	const auto rot = CalcYXZTrigCoefficients(player.player_x_angle,
	                                         player.player_y_angle,
	                                         player.player_z_angle);
	CalculateWheelXZOffsets(player, rot);
	CalculateRoadWheelHeights(track, player);
	CalculateActualWheelHeights(player);

	player.player_z_speed = saved_z_speed;

	const double fl = player.front_left_road_height - player.front_left_actual_height;
	const double fr = player.front_right_road_height - player.front_right_actual_height;
	const double rr = player.rear_road_height - player.rear_actual_height;

	double max_below = fl;
	if (fr > max_below) max_below = fr;
	if (rr > max_below) max_below = rr;

	// Allow up to MAX_BELOW_ROAD of penetration so we don't fight the
	// spring's equilibrium during normal grounded driving.
	constexpr double MAX_BELOW_ROAD = 0x200;
	if (max_below > MAX_BELOW_ROAD)
	{
		// Lift the car (render-space negative-up: lifting = decreasing player_y).
		// max_below is in road_height units; /-16 converts to render-space y.
		player.player_y -= (max_below - MAX_BELOW_ROAD) / 16.0;

		// Kill any remaining downward velocity so the car doesn't immediately
		// drive itself back through the surface on the next frame.
		if (player.player_world_y_speed < 0)
			player.player_world_y_speed = 0;

		CalculateActualWheelHeights(player);
	}

	// Restore the spring-filter inputs so next frame's averaged sample is
	// computed from the same prev-frame value it would have used without us.
	player.front_left_road_height = saved_fl;
	player.front_right_road_height = saved_fr;
	player.rear_road_height = saved_rr;
}

static void CalculateTotalAcceleration(GameState& player)
{
	player.player_y_acceleration = player.gravity_y_acceleration +
		player.car_collision_y_acceleration;

	// reduce engine_z_acceleration if car is accelerating and not travelling backwards
	// this probably simulates the effect of wind resistance and the
	// car having reduced ability to accelerate as speed increases
	const int32_t reduction = (static_cast<int32_t>(player.engine_z_acceleration) >> 8 |
		static_cast<int32_t>(player.player_z_speed) >> 8) & 0xff;
	if ((reduction & 0x80) != 0x80) // i.e. not negative
	{
		if ((static_cast<int32_t>(player.engine_z_acceleration) & 0xff) != 0)
		{
			player.engine_z_acceleration -= reduction;
		}
	}

	// limit engine_z_acceleration to (2 * car_collision_y_acceleration) ?
	// this possibly prevents the car from accelerating
	// if it is not touching the road sufficiently (not enough grip)
	double twice_y = GetTwiceCollisionYAcceleration(player); // should always be +'ve
	if (abs(player.engine_z_acceleration) >= twice_y)
	{
		if (player.engine_z_acceleration < 0)
			twice_y = -twice_y; // correct sign

		player.engine_z_acceleration = twice_y;
	}

	player.player_z_acceleration = player.engine_z_acceleration +
		player.gravity_z_acceleration +
		player.car_collision_z_acceleration;

	CalculateXAcceleration(player);
}

static double GetTwiceCollisionYAcceleration(const GameState& player)
{
	if (!player.touching_road)
		return 0;

	return player.car_collision_y_acceleration * 2;
}

static void CalculateXAcceleration(GameState& player)
{
	double acceleration = player.gravity_x_acceleration + player.car_collision_x_acceleration;
	const double speed_diff = acceleration - player.player_x_speed; // speed increase minus current speed

	double twice_y = GetTwiceCollisionYAcceleration(player); // should always be +'ve
	if (abs(speed_diff) >= twice_y)
	{
		if (player.player_x_speed < 0)
			twice_y = -twice_y; // correct sign

		acceleration -= twice_y;
		player.player_x_acceleration = acceleration;

		// FOLLOWING VALUE NOT USED AT PRESENT
		////collision_in_air = true;	// not sure if it really signifies collision in air
		// don't think it is used anyway
	}
	else
	{
		// why isn't gravity_x_acceleration added here ?
		player.player_x_acceleration = player.car_collision_x_acceleration - player.player_x_speed;

		// FOLLOWING VALUE NOT USED AT PRESENT
		////collision_in_air = false;
	}
}

static double y_angle_difference, difference_angle, pos_difference_angle;

static void CalculateSteering(const TrackState& track, GameState& player)
{
	// basically affects player_y_angle
	//			     and player_y_rotation_acceleration (which affects player_y_angle)
	//
	// reason why player_y_angle sometimes needs direct adjustment:-
	//	   to give a one-off adjustment - adjusting the acceleration has a continuing effect

	static int32_t piece = 0;
	int32_t scaled_pos_difference_angle;
	int32_t steering_amount;
	int32_t backwards = false;

	// find the piece that the car is currently on
	piece = IdentifyPiece(track, player.player_x, player.player_z, piece);
	player.player_current_piece = piece;

	const auto& t = track.Track[piece];
	// get section steering amount
	const int32_t section_steering_amount = t.steeringAmount;

	// calculate car x/z position relative to the piece (and in same range)
	auto [rx, rz] = CalcXZRelativeToPiece(track, player.player_x,
	                                      player.player_z, piece);

	// calculate y angle of piece at the point where the centre of the car lies
	double section_y_angle = CalcSectionYAngle(track, piece, rx, rz);

	// Reverse section_y_angle for PC StuntCarRacer convention
	section_y_angle = WrapAngle(-section_y_angle);

	// calculate the difference between the section and player's y angle
	// this value should go increasingly -'ve when turning to the right
	// and should go increasingly +'ve when turning to the left
	y_angle_difference = section_y_angle - player.player_y_angle;

	// extra adjustment due to PC StuntCarRacer angles not taking full words
	// should make y_angle_difference range from -180 to 180 degrees
	if (y_angle_difference > _180_DEGREES) y_angle_difference -= _360_DEGREES;
	if (y_angle_difference < -_180_DEGREES) y_angle_difference += _360_DEGREES;
	// this value should go increasingly -'ve when turning to the right
	// and should go increasingly +'ve when turning to the left

	// Extra logic to allow car to drive round track in either direction
	// (backwards flag only applies to curves)
	if (y_angle_difference > _90_DEGREES)
	{
		y_angle_difference -= _180_DEGREES;
		backwards = true;
	}
	if (y_angle_difference < -_90_DEGREES)
	{
		y_angle_difference += _180_DEGREES;
		backwards = true;
	}

	// If player is on a curved section then adjust the difference angle
	int32_t left_hand_bend = false;
	if (t.type == 0x80 || t.type == 0xc0)
	{
		// curve
		// Correctly identify left/right hand bend when driving round backwards
		if ((t.type == 0x80) ^ t.oppositeDirection ^ backwards)
		{
			// right hand bend
			y_angle_difference += 217;
		}
		else
		{
			// left hand bend
			y_angle_difference -= 217;
			left_hand_bend = true;
		}
	}

	difference_angle = y_angle_difference;
	pos_difference_angle = abs(y_angle_difference);

	// Save a scaled positive difference angle ranging from 0 to $7fff
	if (pos_difference_angle < 0x800)
		scaled_pos_difference_angle = static_cast<int32_t>(pos_difference_angle * 16);
	else
		scaled_pos_difference_angle = 0x7fff; // set to maximum

	// If on last segment of road section then get data for next section
	// (perhaps because last co-ords aren't used by StuntCarRacer ?)
	// - not done at present
	if (player.left_right_value != 0)
	{
		// player.is.steering

		// work out if pos_difference_angle is going to increase
		// i.e. car is trying to keep in line with the track or not
		int32_t increasing = (difference_angle < 0) ^ (player.left_right_value < 0);

		if (t.type == 0x80 || t.type == 0xc0)
		{
			// curve
			if ((player.left_right_value >= 0) ^ left_hand_bend)
			{
				// steering into the bend
				steering_amount = section_steering_amount + 45;
			}
			else
			{
				// steering away from bend
				steering_amount = section_steering_amount - 35;

				// NOTE: left_right_value below just used as +'ve/-'ve flag
				if (left_hand_bend)
					player.left_right_value = -1;
				else
					player.left_right_value = 1;

				// ensure steering assistance is not done
				increasing = true;
			}
		}
		else
		{
			// straight
			steering_amount = section_steering_amount;
		}

		if (!increasing)
		{
			// Add current difference (between player and road) onto steering amount
			// to assist steering when car is trying to keep in line with track
			steering_amount += scaled_pos_difference_angle >> 8;
		}

		CalculateSteeringAcceleration(player, steering_amount);
		// end of function
	}
	else
	{
		// player.not.steering
		y_angle_difference = 0; // zero steering acceleration

		if (t.type == 0x00 || t.type == 0x40)
		{
			// straight
			AlignCarWithRoad(player);
			AdjustSteeringAcceleration(player);
		}
		else
		{
			// curve

			// NOTE: left_right_value below just used as +'ve/-'ve flag
			if (left_hand_bend)
				player.left_right_value = -1;
			else
				player.left_right_value = 1;

			steering_amount = section_steering_amount;
			// give effect of centrifugal force ?
			CalculateSteeringAcceleration(player, steering_amount);
		}
	}
}

static void CalculateSteeringAcceleration(GameState& player, const int32_t steering_amount)
{
	// Steering acceleration increases as player's speed increases

	// get y_angle_difference, pos_difference_angle from calling function

	// following value calculated in slightly odd way, to match Amiga StuntCarRacer
	double steering_acceleration = (player.player_z_speed * steering_amount) / 256.0;

	if (player.left_right_value < 0)
	{
		// steering left
		steering_acceleration = -steering_acceleration;
	}

	steering_acceleration = steering_acceleration / 8.0;

	// store steering acceleration
	y_angle_difference = steering_acceleration;

	if (pos_difference_angle >= 30 * 256)
	{
		AlignCarWithRoad(player);
	}
	AdjustSteeringAcceleration(player);
}

static void AlignCarWithRoad(GameState& player)
{
	// Following code used to gradually bring the car back in
	// line with the road - this helps steering considerably

	// eventually get difference_angle, pos_difference_angle from calling function

	double adjust = pos_difference_angle;

	if (adjust >= 256)
	{
		adjust -= 30 * 256;
		if (adjust >= 0)
		{
			// this section makes a large adjustment, e.g. 60 degrees,
			// for when the car is very out of line (e.g. sideways with respect to road)

			// just use remainder to adjust player's y angle
			// needs to correct signs because PC StuntCarRacer rotation is in opposite direction
			if (difference_angle >= 0)
				player.player_y_angle += adjust;
			else
				player.player_y_angle -= adjust;

			return;
		}

		// set adjustment amount to maximum
		adjust = 255;
	}

	// Adjustment of player's Y angle increases as player's speed increases

	int32_t speed = abs(static_cast<int32_t>(player.player_z_speed)) + 0xa00;
	if (speed > 0x7f00)
		speed = 0x7f00; // set speed amount to maximum

	adjust = (adjust * speed) / 32768.0;

	if (adjust < 1.0) adjust = 1.0; // atleast do some adjusting

	// needs to correct signs because PC StuntCarRacer rotation is in opposite direction
	if (difference_angle >= 0)
		player.player_y_angle += adjust;
	else
		player.player_y_angle -= adjust;
}

static void AdjustSteeringAcceleration(GameState& player)
{
	// eventually get y_angle_difference from calling function

	const double acceleration = y_angle_difference - player.player_y_rotation_speed;

	// store steering acceleration
	// needs to correct signs because PC StuntCarRacer rotation is in opposite direction
	if (player.touching_road)
		player.player_y_rotation_acceleration = acceleration;
	else
		player.player_y_rotation_acceleration = 0; // steering disabled
}

// current piece x/z co-ords (i.e. four corners of piece)
static double px1, pz1, px2, pz2, px3, pz3, px4, pz4;

static int32_t IdentifyPiece(const TrackState& track, const double x, const double z, int32_t piece)
{
	// find the piece that the point is located within

	// defaults to the input piece if no piece could be found using the map
	const auto map_result = GetPieceUsingMap(track, x, z);
	if (map_result.found)
		piece = map_result.piece;

	// get the four (x,y,z) corner points of the piece
	GetPieceCoords(track, piece);

	//

	// check point is not before or after piece (z direction)
	double xs, xp, zs, zp;
	int32_t before_piece = true, after_piece = true;

	// 'before piece' loop
	int32_t num_piece_changes = 0;
	while (before_piece)
	{
		const auto rel = CalcXZRelativeToPiece(track, x, z, piece);

		// calculate top dot product => before_piece
		xs = px1 - px4;
		zs = pz1 - pz4; // current segment vector
		xp = rel.rx - px4;
		zp = rel.rz - pz4; // current point vector
		before_piece = xs * zp - xp * zs < 0;

		if (before_piece)
		{
			// future improvement: try a move in one direction,
			// if this is worse then move in other direction

			// DIRECTION DEPENDANT - WHOLE SECTION
			// go to next piece
			piece++;
			if (piece > track.NumTrackPieces - 1) piece = 0;
			num_piece_changes++;

			// get the four (x,y,z) corner points of the new piece
			GetPieceCoords(track, piece);
		}

		// prevent an infinite loop
		if (num_piece_changes >= track.NumTrackPieces)
		{
			break;
		}
	}

	// 'after piece' loop
	num_piece_changes = 0;
	while (after_piece)
	{
		const auto rel = CalcXZRelativeToPiece(track, x, z, piece);

		// calculate bottom dot product => after_piece
		xs = px3 - px2;
		zs = pz3 - pz2; // current segment vector
		xp = rel.rx - px2;
		zp = rel.rz - pz2; // current point vector
		after_piece = xs * zp - xp * zs < 0;

		if (after_piece)
		{
			// future improvement: try a move in one direction,
			// if this is worse then move in other direction

			// DIRECTION DEPENDANT - WHOLE SECTION
			// go to previous piece
			piece--;
			if (piece < 0) piece = track.NumTrackPieces - 1;
			num_piece_changes++;

			// get the four (x,y,z) corner points of the new piece
			GetPieceCoords(track, piece);
		}

		// prevent an infinite loop
		if (num_piece_changes >= track.NumTrackPieces)
		{
			break;
		}
	}

	return piece;
}

static void GetPieceCoords(const TrackState& track, const int32_t piece)
{
	const auto& t = track.Track[piece];
	const int32_t numSegments = t.numSegments;

	px2 = t.coords[0].x;
	pz2 = t.coords[0].z;

	px3 = t.coords[1].x;
	pz3 = t.coords[1].z;

	px1 = t.coords[(numSegments * 4)].x;
	pz1 = t.coords[(numSegments * 4)].z;

	px4 = t.coords[numSegments * 4 + 1].x;
	pz4 = t.coords[numSegments * 4 + 1].z;
}

static void CalculateWorldAcceleration(GameState& player, const RotationMatrix& rot)
{
	// Transform player-local accelerations (X, Y, Z) into world-space accelerations.
	// Same operation as WorldOffset.
	auto transform = [&](const int32_t x_comp, const int32_t y_comp, const int32_t z_comp) -> double
	{
		return player.player_x_acceleration * rot[x_comp]
			+ player.player_y_acceleration * rot[y_comp]
			+ player.player_z_acceleration * rot[z_comp];
	};

	player.total_world_x_acceleration = transform(X_X_COMP, Y_X_COMP, Z_X_COMP);
	player.total_world_y_acceleration = transform(X_Y_COMP, Y_Y_COMP, Z_Y_COMP);
	player.total_world_z_acceleration = transform(X_Z_COMP, Y_Z_COMP, Z_Z_COMP);
}


static void ReduceWorldAcceleration(GameState& game)
{
	double amount = 0;
	int32_t factor = 1; // maximum reduction factor (least drag)
	bool special_case = false;

	if (game.touching_road || game.on_chains)
	{
		amount = std::abs(game.car_to_road_collision_z_acceleration / 256.0);

		if (amount >= 3 || game.off_map_status != 0 || WRECKED || game.on_chains)
		{
			if (WRECKED || game.on_chains)
				factor = 3; // medium reduction

			amount = 0x6000;
			special_case = true;
		}
	}

	if (!special_case)
	{
		// Normal case: reduce accelerations depending on car speed
		amount = abs(game.player_x_speed);
		amount = std::max(amount, abs(game.player_y_speed));
		amount = std::max(amount, abs(game.player_z_speed));

		factor = 5; // minimum reduction factor (most drag)

		// Slipstream: less drag when behind opponent
		if (game.player_close_to_opponent && !game.opponent_behind_player)
		{
			amount -= 20 * 128;
			if (amount < 0) amount = 0;
		}
	}

	// Apply speed-proportional drag to world accelerations
	const double divisor = 65536.0 * (1 << factor);
	game.total_world_x_acceleration -= (game.player_world_x_speed * amount) / divisor;
	game.total_world_y_acceleration -= (game.player_world_y_speed * amount) / divisor;
	game.total_world_z_acceleration -= (game.player_world_z_speed * amount) / divisor;
}

static void CalculateXZRotationAcceleration(GameState& player)
{
	// Calculate values using current car rotation speeds and inclination values
	// between the car and the road, in order to damp the car X and Z angles
	// and keep the car level with the road, on its X and Z axes.  Also give
	// effect of acceleration.

	// Remember that players_y_rotation_acceleration is set by CalculateSteering()

	// overall.difference.below.road is effectively car X inclination.
	//
	// front.difference.below.road is effectively car Z inclination.

	// question: why aren't the x/z inclinations calculated by
	//			 calculate_car_collision_acceleration used here ?
	//
	// - perhaps values used are really accelerations rather than inclinations

	player.player_x_rotation_acceleration = overall_difference_below_road -
		(player.player_x_rotation_speed / 16.0);
	if (player.touching_road)
	{
		// This part lifts the car up at the front during forwards acceleration
		// and, vice versa, dips the front of the car during backwards acceleration.
		player.player_x_rotation_acceleration += player.player_z_acceleration / 4.0;
	}

	player.player_z_rotation_acceleration = front_difference_below_road -
		(player.player_z_rotation_speed / 16.0);
}

static void UpdatePlayersRotationSpeed(GameState& player)
{
	double acceleration = (player.player_x_rotation_acceleration * REDUCTION) / 256.0;
	player.player_x_rotation_speed += acceleration;

	acceleration = (player.player_y_rotation_acceleration * REDUCTION) / 256.0;
	player.player_y_rotation_speed += acceleration;

	acceleration = (player.player_z_rotation_acceleration * REDUCTION) / 256.0;
	player.player_z_rotation_speed += acceleration;
}

static void CalculateFinalRotationSpeed(GameState& player)
{
	const double sin_x = std::sin(player.player_x_angle * ANGLE_TO_RADIANS);
	const double rz = player.player_z_angle * ANGLE_TO_RADIANS;
	const double sin_z = std::sin(rz);
	const double cos_z = std::cos(rz);

	// shouldn't need changing because Amiga StuntCarRacer Z rotation appears to be same as
	// PC StuntCarRacer Z rotation (i.e. RotX = Xcosz - Ysinz, RotY = Xsinz + Ycosz)
	// and so does X rotation

	player.player_final_x_rotation_speed = player.player_x_rotation_speed * cos_z;
	player.player_final_x_rotation_speed += player.player_y_rotation_speed * -sin_z;

	player.player_final_y_rotation_speed = player.player_x_rotation_speed * sin_z;
	player.player_final_y_rotation_speed += player.player_y_rotation_speed * cos_z;

	// Calculate final Z rotation speed by rotating Y rotation speed about
	// the X axis and adding it onto the Z rotation speed.
	player.player_final_z_rotation_speed = player.player_z_rotation_speed;
	player.player_final_z_rotation_speed += player.player_final_y_rotation_speed * sin_x;
}

static void UpdatePlayersWorldSpeed(GameState& player)
{
	double acceleration = (player.total_world_x_acceleration * REDUCTION) / 256.0;
	player.player_world_x_speed += acceleration;

	acceleration = (player.total_world_y_acceleration * REDUCTION) / 256.0;
	player.player_world_y_speed += acceleration;

	acceleration = (player.total_world_z_acceleration * REDUCTION) / 256.0;
	player.player_world_z_speed += acceleration;
}

static void UpdatePlayersPosition(GameState& player)
{
	// Convert each per-frame velocity into a render-space position delta.
	// world_*_speed are in physics-internal acceleration units; the
	// REDUCTION * PC_FACTOR / PRECISION factor (XZ) is the historical Amiga
	// rescale. The Y rescale also folds in LOCAL_Y_FACTOR/2 and a sign flip
	// because render-space Y is negative-up.
	constexpr double XZ_DELTA_RESCALE = static_cast<double>(REDUCTION) * PC_FACTOR / PRECISION;
	constexpr double Y_DELTA_RESCALE = -static_cast<double>(REDUCTION) *
		GameState::LOCAL_Y_FACTOR / 2.0 / PRECISION;
	player.player_x += player.player_world_x_speed * XZ_DELTA_RESCALE;
	player.player_y += player.player_world_y_speed * Y_DELTA_RESCALE;
	player.player_z += player.player_world_z_speed * XZ_DELTA_RESCALE;


	// Hard ceiling: render-space negative-up, so cap at the most-negative
	// extreme. Old `player_y_internal >= 0x10000000` in render-space is
	// player_y <= -65536.
	if (player.player_y < -65536.0)
		player.player_y = -65536.0;

	// Set player's new angles. Now that angles are continuous doubles in
	// MAX_ANGLE units, we no longer need to truncate the per-frame delta.
	double angle_speed = (player.player_final_x_rotation_speed * REDUCTION) / 256.0;
	player.player_x_angle += angle_speed;

	angle_speed = (player.player_final_y_rotation_speed * REDUCTION) / 256.0;
	player.player_y_angle += angle_speed;

	angle_speed = (player.player_final_z_rotation_speed * REDUCTION) / 256.0;
	player.player_z_angle += angle_speed;

	// Limit to valid range (no longer stored as words)
	player.player_x_angle = WrapAngle(player.player_x_angle);
	player.player_y_angle = WrapAngle(player.player_y_angle);
	player.player_z_angle = WrapAngle(player.player_z_angle);

	// Clamp X and Z angles to prevent extreme tilting
	const double limit = player.at_side_byte == 0xe0 && player.smaller_limit_required
		                     ? 11 * 256 // all wheels off road and car on ground
		                     : 45 * 256;

	auto clampAngle = [limit](double& angle_inout, double& rotation_speed)
	{
		// get angle in Amiga StuntCarRacer format (i.e. correct sign)
		double angle = angle_inout < _180_DEGREES ? angle_inout : angle_inout - _360_DEGREES;

		if (std::abs(angle) > limit)
		{
			angle = angle >= 0 ? limit : -limit;

			// convert back to PC StuntCarRacer format (unsigned)
			angle_inout = angle > 0 ? angle : angle + _360_DEGREES;

			// zero rotation speed if it opposes the clamped direction
			if (rotation_speed >= 0 != angle >= 0)
				rotation_speed = 0;
		}
	};

	clampAngle(player.player_x_angle, player.player_x_rotation_speed);
	clampAngle(player.player_z_angle, player.player_z_rotation_speed);
}

static double CalcSectionYAngle(const TrackState& track, const int32_t piece,
                                const double x,
                                const double z)
{
	const auto& t = track.Track[piece];

	// check for and handle straight
	if (t.type == 0x00)
	{
		return WrapAngle(-static_cast<double>(t.roughPieceAngle));
	}
	// check for and handle diagonal straight
	if (t.type == 0x40)
	{
		return WrapAngle(-static_cast<double>(t.roughPieceAngle + MAX_ANGLE / 8));
	}

	// must be a curve (type will be -'ve)
	const auto curve = CalcCurveMeasurements(track, piece, x, z);
	double section_y_angle = curve.y_angle;

	// change sign if right hand curve (i.e. default calculation is for left hand curve)
	if (!t.curveToLeft)
		section_y_angle = -section_y_angle;

	// add on rough piece angle to get final section y angle
	section_y_angle -= t.roughPieceAngle;

	// adjust for normal direction of travel
	if (t.oppositeDirection)
		section_y_angle += MAX_ANGLE / 2; // plus 180 degrees

	// limit to valid range
	return WrapAngle(section_y_angle);
}

static CurveMeasurements CalcCurveMeasurements(const TrackState& track, const int32_t piece,
                                               const double x,
                                               const double z)
{
	double xc, zc;
	double o, a, radians;

	// NOTE: Assumes x/z are relative to (and in same range as) piece co-ordinates

	// start of code - initialise outputs to zero
	CurveMeasurements result = {0, 0, 0.0};

	const auto& t = track.Track[piece];

	// calculate radius of circle that piece is taken from
	// calculates inner or outer edge radius, depending upon co-ordinate layout
	const int32_t numSegments = t.numSegments;

	// get first and last co-ordinates from inner or outer edge
	const int32_t first = 0, last = numSegments * 4;

	double xf = t.coords[first].x;
	double zf = t.coords[first].z;
	double xl = t.coords[last].x;
	double zl = t.coords[last].z;

	// assumes all curved pieces are 45 degree circular arcs
	const double radius = std::abs(xl - xf) + std::abs(zl - zf);

	// Use horizontal/vertical edge when calculating circle centre
	// check first edge is horizontal/vertical, if not then use last edge
	double xo = t.coords[first + 1].x;
	double zo = t.coords[first + 1].z;
	if (xo != xf && zo != zf)
	{
		// use last edge
		// note that variable names are now misleading (i.e. opposite meaning)
		xf = t.coords[last].x;
		zf = t.coords[last].z;
		xl = t.coords[first].x;
		zl = t.coords[first].z;

		xo = t.coords[last + 1].x;
		zo = t.coords[last + 1].z;
	}

	// check resulting edge is horizontal/vertical
	if (xo != xf && zo != zf)
	{
		return result;
	}

	// calculate co-ordinate of circle centre
	// uses first co-ordinate from other edge, for comparison
	if (xo != xf)
	{
		// piece edge is horizontal
		if (xf < xl)
			xc = xf + radius;
		else
			xc = xf - radius;

		zc = zf;

		o = z - zc;
		a = x - xc;
	}
	else if (zo != zf)
	{
		// piece edge is vertical
		xc = xf;

		if (zf < zl)
			zc = zf + radius;
		else
			zc = zf - radius;

		o = x - xc;
		a = z - zc;
	}
	else
	{
		return result;
	}

	// use inverse tan to calculate basic angle in radians
	if (a == 0) // prevent division by zero
		radians = PI / static_cast<double>(2); // 90 degrees
	else
		radians = atan(o / a); // inverse tan

	// convert radians to internal angle
	const double angle = radians * static_cast<double>(MAX_ANGLE) / (static_cast<double>(2) * PI);
	result.y_angle = std::abs(angle);

	// output radius
	result.radius = radius;

	// calculate distance from circle centre to (x,z) point
	result.distance_from_centre = sqrt(o * o + a * a);
	return result;
}


static void PositionCarAbovePiece(TrackState& track, GameState& player, int32_t piece)
{
	// Find section to lower car onto 
	for (;;)
	{
		int32_t t = GetPieceAngleAndTemplate(piece);
		t &= 0xf; // templateNum
		if (track.sections_car_can_be_put_on[t] & 0x80)
		{
			// go to previous piece if already at first surface
			piece--;
			if (piece < 0) piece = track.NumTrackPieces - 1;
		}
		else
			break;
	}

	// Should also reject Track specific pieces (DAT.1c8e8)

	// calculate x/z position of piece's front left corner, within world
	// (render-space, matches player_x/z).
	const double piece_x = track.Track[piece].x * WORLD_CUBE_SIZE;
	const double piece_z = track.Track[piece].z * WORLD_CUBE_SIZE;

	// set car x/z position to middle of piece
	player.player_x = piece_x + WORLD_CUBE_SIZE / 2;
	player.player_z = piece_z + WORLD_CUBE_SIZE / 2;

	// set car y position
	const double height = CalculateWorldRoadHeight(track, player, 0, player.player_x, player.player_z);

	// Place car render-space y at the road plus the chain drop offset, so
	// the chain descent (chain_height_remaining) brings it down onto the
	// road. Render-space y is negative-up; height is in road_height units
	// (256-per-render-Y), so /-16 converts to render-space y, and the chain
	// offset is 48 render-Y units above the road (was 0xc00*256/LOCAL_Y_FACTOR
	// = 0x30000 in the legacy internal-Y units, /4096 = 48 render-Y).
	player.player_y = -height / 16.0 - 48.0;

	// clear car x/z angle
	player.player_x_angle = 0;
	player.player_z_angle = 0;

	// set car y angle
	player.player_y_angle = track.Track[piece].roughPieceAngle;

	if (track.Track[piece].oppositeDirection)
	{
		player.player_y_angle += MAX_ANGLE / 2; // plus 180 degrees
	}

	// check for and handle diagonal straight
	if (track.Track[piece].type == 0x40)
	{
		// Amiga StuntCarRacer always adds 0x2000 on for these pieces (i.e. 45 degrees)
		player.player_y_angle += MAX_ANGLE / 8;
	}

	player.player_y_angle = WrapAngle(player.player_y_angle);

	/*
	 * Then player.to.side.of.road
	 *
	 * Shift player in x direction by 160.
	 *
	 * This is actually x = 160, z = 0 being rotated about the y axis and then added to the player x and z.
	 */
	const double ry = player.player_y_angle * ANGLE_TO_RADIANS;
	const double sin_y = std::sin(ry);
	const double cos_y = std::cos(ry);
	player.player_x += 160.0 * cos_y;
	player.player_z -= 160.0 * sin_y;
}

int32_t CalculateDisplaySpeed(const GameState& player)
{
	int32_t speed = static_cast<int32_t>(player.player_z_speed);
	if (speed < 0) speed = 0;

	speed = (speed * 183) >> 15;

	return speed;
}

static int32_t engineRevs = 0;
static int32_t engineRevsChange = 0;
static int32_t engineFluctuation = 0;

// Tested against Amiga
static void UpdateEngineRevs(const GameState& player)
{
	int c;

	if (!player.touching_road)
	{
		// Not touching road, so test if joystick is held forwards or backwards
		c = 0;
		if (player.accelerate || player.brake)
			c = 0x9000;
	}
	else
	{
		// Touching road
		c = static_cast<int32_t>(player.player_z_speed) & ~0xf; // zero low four bits
		if (c < 0) c = -c;
	}

	c += 0x580;
	c = c >> 3;
	if (engineRevs < 192)
	{
		// If engine revs. are low then increase them slowly (e.g. at race start)
		c = 2;
	}
	else
	{
		// Otherwise calculate revs. change depending on current engine revs.
		c -= engineRevs;
		c = c >> 3;
	}

	engineRevsChange = c;

	/*
	 * Now adjust revs. change
	 */
	if (engineRevsChange >= 0x100)
	{
		// If revs. change is $100 or greater then set to $100
		engineRevsChange = 0x100;
	}
	else if (engineRevsChange < 0)
	{
		if (player.touching_road)
		{
			// Touching road, so set revs. change to $ff00 minimum
			if (engineRevsChange < -0x100)
				engineRevsChange = -0x100;
		}
		else
		{
			// Not touching road, so set revs. change to $ffe0 minimum
			if (engineRevsChange < -0x20)
				engineRevsChange = -0x20;
		}
	}

	engineFluctuation = rand() & 0xf;
}


int enginePeriod = 198;
int engineSoundIndex = -1;

// Ideas to try:
// All sounds playing but mute the ones that aren't required - WORSE
// Start the new sound playing at the same percentage through as the previous sound - SLIGHTLY BETTER

void FramesWheelsEngine(SoundState& sound, PlatformSoundBuffer* engineSoundBuffers[])
{
	int r = engineRevs + engineRevsChange;
	static int lastEngineSoundIndex = -1;
	uint32_t currentPlayCursor;

	if (r < 0)
	{
		r = 0;
	}

	engineRevs = r;

	r += 378;
	int period = 4800000 / r;

	int index = 6;

	if (period >= 0x3fff) period = 0x3ffe;

	period = period | engineFluctuation;
	if (period < 124) period = 124; // lowest possible period

	// Calculate sound index that will give period < 256
	while (period >= 256)
	{
		period >>= 1;
		--index;

		if (index < 0) index = 0;
	}
	const uint32_t freq = AMIGA_PAL_HZ / period;
	// Rearranging formula: period = clock constant (AMIGA_PAL_HZ) / frequency (samples per second)

	// temp store new engine sound index and period
	enginePeriod = period;
	engineSoundIndex = index;

	if (!sound.engineSoundPlaying)
	{
		// Reset last index so that logic below will restart the engine (e.g. after game was paused)
		lastEngineSoundIndex = -1;
	}

	if (engineSoundIndex != lastEngineSoundIndex)
	{
		// Stop the old engine sound
		if (lastEngineSoundIndex >= 0)
		{
			const auto soundPos = PlatformSoundGetPosition(engineSoundBuffers[lastEngineSoundIndex]);
			currentPlayCursor = soundPos.pos;
			PlatformSoundStop(engineSoundBuffers[lastEngineSoundIndex]);
		}
		else
			currentPlayCursor = 0;

		// Start the new engine sound

		// Attempt to start at same position through as previous sound
		if (engineSoundIndex > lastEngineSoundIndex)
			currentPlayCursor = currentPlayCursor / 2;
		else
			currentPlayCursor = currentPlayCursor * 2;

		PlatformSoundSetPosition(engineSoundBuffers[engineSoundIndex], currentPlayCursor);
		PlatformSoundPlay(engineSoundBuffers[engineSoundIndex], true);

		lastEngineSoundIndex = engineSoundIndex;
		sound.engineSoundPlaying = true;
	}

	// Set the frequency of the current engine sound
	PlatformSoundSetFrequency(engineSoundBuffers[engineSoundIndex], freq);
}

void CalculatePlayersRoadPosition(TrackState& track, GameState& player)
{
	// Calculate the position of the car's centre
	CalculateWorldRoadHeight(track, player, CENTRE, player.player_x, player.player_z);
}

void DrawOtherGraphics(GameState& player, const SoundState& sound)
{
	// Draw other graphics that are done as part of 'draw.world'
	if (!player.on_chains && player.off_map_status != 0)
		DrawDustClouds(player, sound);

	DrawSparks(player, sound);

	player.which_side_byte = 0; // Amiga StuntCarRacer cleared this in update.wheel.positions
}

static void DrawDustClouds(const GameState& player, const SoundState& sound)
{
	// currently just plays the sound effect

	int p = rand();
	p &= 0x1c;
	p += 450;

	PlatformSoundSetFrequency(sound.OffRoadSoundBuffer, AMIGA_PAL_HZ / p);

	if (!player.touching_road)
		return;

	PlatformSoundPlay(sound.OffRoadSoundBuffer, false); // not looping
}

static void DrawSparks(const GameState& game, const SoundState& sound)
{
	// currently just plays the sound effect

	if (!game.which_side_byte && NOT_WRECKED) return; // if car is not scraping on road and not on an edge
	if (game.off_map_status != 0) return; // dust clouds will be drawn instead

	int p = abs(static_cast<int32_t>(game.player_z_speed)) / 256;
	if (p < 1) return; // if speed is not large enough

	if (p > 50) p = 50; // set to maximum
	// ferocity.of.sparks.or.clouds = p

	p >>= 1;
	if (p > 31) p = 31;

	p ^= 0x31;
	p &= 0xff;
	p <<= 2;
	p += 170;

	PlatformSoundSetFrequency(sound.WreckSoundBuffer, AMIGA_PAL_HZ / p);

	if (!game.touching_road)
		return;

	PlatformSoundPlay(sound.WreckSoundBuffer, false); // not looping
}

static void PlayCreakSound(const GameState& player, const SoundState& sound)
{
	int32_t amiga_volume = (player.damage_value >> 8) * 4;
	// minimum volume = 28, maximum volume = 64
	if (amiga_volume < 28) amiga_volume = 28;
	if (amiga_volume > 64) amiga_volume = 64;

	PlatformSoundSetVolume(sound.CreakSoundBuffer, amiga_volume);
	PlatformSoundPlay(sound.CreakSoundBuffer, false); // not looping
}

void UpdateDamage(GameState& player, const SoundState& sound)
{
	if (player.damaged)
	{
		const int32_t d = (player.front_left_damage + player.front_right_damage) / 2; // average front damage
		player.new_damage = (d + player.rear_damage) / 2; // total average damage
		// value new_damage must be used to draw damage line
	}

	if (player.smashed_countdown)
	{
		--player.smashed_countdown;
		if (player.smashed_countdown == 69)
		{
			// change smash to hole, by copying 'damage hole' graphic to damage.hole.position
			PlayCreakSound(player, sound);
			return;
		}

		if (player.damaged) PlayCreakSound(player, sound);

		return;
	}

	if (!player.damaged) return;

	if (player.damage_value < 0x1400)
	{
		PlayCreakSound(player, sound);
		return;
	}

	player.smashed_countdown = 69;

	// Play smash sound effect
	PlatformSoundPlay(sound.SmashSoundBuffer, false); // not looping
}

static constexpr int32_t LAP_THAT_FINISHES_RACE = 4;

// Alias for opponent state - moved to top of file with other aliases


static bool carOnFirstHalfOfLap[NUM_CARS] = {false, false};

void ResetLapData(GameState& game, const int32_t car)
{
	game.raceFinished = game.raceWon = false;
	game.lapNumber[car] = 0;
	carOnFirstHalfOfLap[car] = false;
}

void UpdateLapData(GameState& game, const TrackState& track)
{
	int32_t car, start_finish_piece =
		        track.StartLinePiece + 1 < track.NumTrackPieces ? track.StartLinePiece + 1 : 0;

	for (car = OPPONENT; car < NUM_CARS; car++)
	{
		const int32_t current_piece = car == PLAYER
			                              ? game.player_current_piece
			                              : game.opponents_current_piece;

		if (carOnFirstHalfOfLap[car])
		{
			if (current_piece == track.HalfALapPiece)
				carOnFirstHalfOfLap[car] = false;
		}
		else if (current_piece == start_finish_piece)
		{
			carOnFirstHalfOfLap[car] = true;
			++game.lapNumber[car];
		}
	}

	for (car = OPPONENT; car < NUM_CARS; car++)
	{
		if (!game.raceFinished)
		{
			if (game.lapNumber[car] == LAP_THAT_FINISHES_RACE)
			{
				game.raceFinished = true;

				// frames to show message for = 44; about 5.64 seconds

				if (CalculateIfWinning(track, game, start_finish_piece) < 0)
					game.raceWon = true;
				else
					game.raceWon = false;
			}
		}
	}
}
