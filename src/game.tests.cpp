//
// game.tests.cpp - Headless self-tests run via the /test command-line option.
//
// Drives the car physics directly (no window, no rendering, no sound) to
// verify behaviour such as the chain-drop start: the car should be lowered
// onto the track surface and come to rest there, not fall through.
//
// All output is written to test_results.log in the working directory and to
// stderr, and the process exits with code 0 on success or 1 on failure.
//

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

#include "platform.h"
#include "game.h"


// State owned by game.cpp / game.drive.cpp that the tests need to drive or
// inspect. Declared extern here to keep the test isolated to this file.
extern GameState g_gameState;
extern TrackState g_trackState;
extern SoundState g_soundState;
extern GameModeType g_gameMode;

namespace
{
	// Simple tee logger: writes to file and stderr.
	struct Logger
	{
		FILE* fp = nullptr;
		int failures = 0;

		void open(const char* path)
		{
			fopen_s(&fp, path, "w");
		}

		void close()
		{
			if (fp) std::fclose(fp);
			fp = nullptr;
		}

		void log(const char* fmt, ...)
		{
			va_list ap;
			va_start(ap, fmt);
			if (fp)
			{
				va_list ap2;
				va_copy(ap2, ap);
				std::vfprintf(fp, fmt, ap2);
				va_end(ap2);
				std::fflush(fp);
			}
			std::vfprintf(stderr, fmt, ap);
			va_end(ap);
		}

		void check(const bool ok, const char* what)
		{
			log("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
			if (!ok) ++failures;
		}
	};

	// Reset the global player position vars to a known state before each test.
	// game.cpp keeps its own copy of player1_x/y/z separate from g_gameState; we
	// reach them via CarBehaviour's input/output API, so we just track them here.
	struct CarVars
	{
		double x = 0.0, y = 0.0, z = 0.0;
		double x_angle = 0.0, y_angle = 0.0, z_angle = 0.0;
	};

	// Run a single physics frame.
	void StepFrame(CarVars& car)
	{
		const auto pose = CarBehaviour(g_trackState, g_gameState, g_soundState,
		                               0,
		                               car.x, car.y, car.z,
		                               car.x_angle, car.y_angle, car.z_angle);
		car.x = pose.x;
		car.y = pose.y;
		car.z = pose.z;
		car.x_angle = pose.x_angle;
		car.y_angle = pose.y_angle;
		car.z_angle = pose.z_angle;

		// OpponentBehaviour clears bNewGame; without it the new-game branch in
		// CarBehaviour fires every frame and the chain drop never progresses.
		(void)OpponentBehaviour(g_trackState, g_gameState, /*bOpponentPaused=*/true);

		// Match what OnFrameMove does after CarBehaviour.
		car.y = LimitViewpointY(g_trackState, g_gameState, car.y);
	}

	// Test: starting a new race lowers the car onto the track and it rests there.
	void TestChainDropOntoTrack(Logger& log, const int trackId)
	{
		log.log("\n--- TestChainDropOntoTrack: track %d ---\n", trackId);

		if (!ConvertAmigaTrack(g_trackState, trackId))
		{
			log.log("  ConvertAmigaTrack failed\n");
			log.check(false, "track loaded");
			return;
		}

		g_gameMode = GAME_IN_PROGRESS;
		g_gameState.opponentsID = 0;
		g_gameState.bNewGame = true;
		g_gameState.INITIALISE_PLAYER = true;

		CarVars car;

		// Frame 1 triggers the new-game branch in CarBehaviour, which calls
		// PositionCarAbovePiece and sets up the chain drop.
		StepFrame(car);

		const double start_y = car.y;
		const int32_t start_chain = static_cast<int32_t>(g_gameState.chain_height_remaining);
		const bool start_on_chains = g_gameState.on_chains != 0;

		log.log("  After first frame: player_y=%.1f, chain_remaining=%d, on_chains=%d, touching_road=%d\n",
		        start_y, start_chain, start_on_chains ? 1 : 0,
		        g_gameState.touching_road ? 1 : 0);

		log.check(start_on_chains, "car is on chains after new-game frame");
		log.check(start_chain > 0, "chain_height_remaining initialised positive");

		// Run enough frames for the chain to fully descend and the car to settle.
		// chain_height_remaining starts at 48 (render-space Y units, was
		// 0xc00 * 256 / LOCAL_Y_FACTOR = 0x30000 internal), CHAIN_DESCENT_RATE
		// per frame is 1, so 48 frames to finish chains. Add a generous settling margin.
		constexpr int kMaxFrames = 400;
		int chainsDoneFrame = -1;
		int restingFrame = -1;
		double settled_y = 0.0;
		int32_t settled_road_height = 0;

		for (int i = 2; i <= kMaxFrames; ++i)
		{
			StepFrame(car);

			if (chainsDoneFrame < 0 && !g_gameState.on_chains)
				chainsDoneFrame = i;

			// Car is "resting" when chains are done and it's touching the road.
			if (chainsDoneFrame > 0 && g_gameState.touching_road && restingFrame < 0)
			{
				restingFrame = i;
			}

			// Sample at a few interesting frames.
			if (i <= 5 || i == 50 || i == 100 || i == 200 || i == kMaxFrames)
			{
				log.log("  frame %3d: player_y=%.1f, chain_remaining=%.1f, on_chains=%d, "
				        "touching_road=%d, fl_road=%.1f, fl_actual=%.1f\n",
				        i, car.y, g_gameState.chain_height_remaining,
				        g_gameState.on_chains ? 1 : 0,
				        g_gameState.touching_road ? 1 : 0,
				        g_gameState.front_left_road_height,
				        g_gameState.front_left_actual_height);
			}
		}

		settled_y = car.y;
		settled_road_height = static_cast<int32_t>(g_gameState.front_left_road_height);

		log.log("  Final: player_y=%.1f, chain_remaining=%.1f, on_chains=%d, "
		        "touching_road=%d, fl_road_height=%.1f, fl_actual_height=%.1f\n",
		        settled_y, g_gameState.chain_height_remaining,
		        g_gameState.on_chains ? 1 : 0,
		        g_gameState.touching_road ? 1 : 0,
		        g_gameState.front_left_road_height, g_gameState.front_left_actual_height);
		log.log("  chains finished at frame: %d, first touched at frame: %d\n",
		        chainsDoneFrame, restingFrame);

		log.check(chainsDoneFrame > 0, "chains finished before timeout");
		log.check(g_gameState.touching_road, "car is touching road at end");
		log.check(restingFrame > 0, "car came into contact with road during sequence");

		// Sanity: the car should be near the road, not far below it.
		const double below = g_gameState.front_left_road_height -
			g_gameState.front_left_actual_height;
		log.log("  road - actual = %.1f (positive = car under road)\n", below);
		log.check(below < 0x400,
		          "car not deep below road surface (front_left road - actual < 0x400)");
	}

	// Reset all per-test global state to a clean baseline. Without this, lap
	// counters and other static-life state leak between tests on different tracks.
	void ResetForNewTest()
	{
		ResetPlayer(g_gameState);
		ResetLapData(g_gameState, PLAYER);
		ResetLapData(g_gameState, OPPONENT);
		g_gameMode = GAME_IN_PROGRESS;
		g_gameState.opponentsID = 0;
		g_gameState.bNewGame = true;
		g_gameState.INITIALISE_PLAYER = true;
	}

	// Drive forward (accel + boost) and verify the car does not fall through
	// the road or develop other physics breakage. Without an autopilot the car
	// will eventually drift off curved tracks - that is expected and we do not
	// fail on it; we just log how far it got. The hard assertions are the
	// physics-correctness ones (penetration depth, sustained off-map).
	void TestDriveLapAroundTrack(Logger& log, const int trackId)
	{
		log.log("\n--- TestDriveLapAroundTrack: track %d ---\n", trackId);

		if (!ConvertAmigaTrack(g_trackState, trackId))
		{
			log.check(false, "track loaded");
			return;
		}

		ResetForNewTest();

		CarVars car;

		// Phase 1: chain drop and settle (no driver input).
		int settleFrame = -1;
		constexpr int kSettleMaxFrames = 300;
		for (int i = 1; i <= kSettleMaxFrames && settleFrame < 0; ++i)
		{
			StepFrame(car);
			if (!g_gameState.on_chains && g_gameState.touching_road)
				settleFrame = i;
		}
		log.check(settleFrame > 0, "car settled on track before driving");

		// Phase 2: hold accelerate + boost, watch physics invariants every frame.
		constexpr int kMaxDriveFrames = 4000;
		constexpr int32_t kBelowRoadFailLimit = 0x800; // sustained penetration is a fall-through

		int32_t maxBelowRoad = 0;
		int32_t maxOffMap = 0;
		int worstBelowFrame = -1;
		int lapFrame = -1;
		int piecesAdvanced = 0;
		int32_t prevPiece = g_gameState.player_current_piece;
		bool visitedHalfway = false;
		int framesDriven = 0;

		// Jump tracking: each takeoff/landing pair records how long the car was
		// airborne and the worst penetration sampled in the first few frames
		// after re-grounding (to catch "car lands inside the ramp" bugs).
		bool wasGrounded = g_gameState.touching_road;
		int takeoffFrame = -1;
		int jumpsCompleted = 0;
		int longestAirFrames = 0;
		int32_t worstLandingPenetration = 0;
		int landingSampleFramesLeft = 0;

		for (int i = 1; i <= kMaxDriveFrames; ++i)
		{
			constexpr uint32_t input = KEY_P1_ACCEL | KEY_P1_BOOST;

			const auto pose = CarBehaviour(g_trackState, g_gameState, g_soundState,
			                               input,
			                               car.x, car.y, car.z,
			                               car.x_angle, car.y_angle, car.z_angle);
			car.x = pose.x;
			car.y = pose.y;
			car.z = pose.z;
			car.x_angle = pose.x_angle;
			car.y_angle = pose.y_angle;
			car.z_angle = pose.z_angle;

			(void)OpponentBehaviour(g_trackState, g_gameState, /*bOpponentPaused=*/true);
			car.y = LimitViewpointY(g_trackState, g_gameState, car.y);
			UpdateLapData(g_gameState, g_trackState);

			framesDriven = i;

			// Only count physics samples while the car is genuinely on the track
			// surface. Once the car has drifted off the track and is in free-fall,
			// the road-height samples become meaningless and we just stop scoring.
			if (g_gameState.touching_road && g_gameState.off_map_status == 0)
			{
				const int32_t below = static_cast<int32_t>(g_gameState.front_left_road_height -
					g_gameState.front_left_actual_height);
				if (below > maxBelowRoad)
				{
					maxBelowRoad = below;
					worstBelowFrame = i;
				}
			}
			if (g_gameState.off_map_status > maxOffMap)
				maxOffMap = g_gameState.off_map_status;

			if (g_gameState.player_current_piece != prevPiece)
			{
				++piecesAdvanced;
				prevPiece = g_gameState.player_current_piece;
			}
			if (g_gameState.player_current_piece == g_trackState.HalfALapPiece)
				visitedHalfway = true;

			// Jump detection: edge-trigger on grounded->airborne and airborne->grounded.
			const bool grounded = g_gameState.touching_road != 0;
			if (wasGrounded && !grounded)
			{
				takeoffFrame = i;
			}
			else if (!wasGrounded && grounded && takeoffFrame > 0)
			{
				const int air = i - takeoffFrame;
				// Only count flights of at least a few frames as a real jump (filters
				// out spring-bounce wobble that briefly lifts a wheel).
				if (air >= 3)
				{
					++jumpsCompleted;
					if (air > longestAirFrames) longestAirFrames = air;
					landingSampleFramesLeft = 4; // sample the next few frames
				}
				takeoffFrame = -1;
			}
			wasGrounded = grounded;

			// Sample landing penetration just after a real jump.
			if (landingSampleFramesLeft > 0 && grounded && g_gameState.off_map_status == 0)
			{
				const int32_t below = static_cast<int32_t>(g_gameState.front_left_road_height -
					g_gameState.front_left_actual_height);
				if (below > worstLandingPenetration)
					worstLandingPenetration = below;
				--landingSampleFramesLeft;
			}

			if (g_gameState.lapNumber[PLAYER] >= 2 && visitedHalfway && lapFrame < 0)
				lapFrame = i;

			// Stop the test once the car has clearly escaped the track - no point
			// running thousands more frames of free-fall.
			if (g_gameState.off_map_status > GameState::OFF_TRACK_LIMIT)
				break;
		}

		log.log("  result: framesDriven=%d, piecesAdvanced=%d, lapNumber=%d, lapFrame=%d, "
		        "visitedHalfway=%d, maxBelowRoad=0x%x@frame %d, maxOffMap=%d, "
		        "finalPiece=%d/%d, halfwayPiece=%d, startLinePiece=%d, "
		        "jumps=%d, longestAirFrames=%d, worstLandingPenetration=0x%x\n",
		        framesDriven, piecesAdvanced, g_gameState.lapNumber[PLAYER], lapFrame,
		        visitedHalfway ? 1 : 0, maxBelowRoad, worstBelowFrame, maxOffMap,
		        g_gameState.player_current_piece, g_trackState.NumTrackPieces,
		        g_trackState.HalfALapPiece, g_trackState.StartLinePiece,
		        jumpsCompleted, longestAirFrames, worstLandingPenetration);

		// Hard physics assertions: these must always hold.
		log.check(piecesAdvanced > 0, "car moved forward through track pieces");
		log.check(maxBelowRoad < kBelowRoadFailLimit,
		          "car never sustained deep penetration into road while on it");
		// Landing assertion: when the car has actually jumped, the landing should
		// not place it deep inside the road. With road-height averaging removed,
		// landings should settle near the spring equilibrium (~0x200) rather than
		// the previous lagged 0x800 ceiling. Allow a small margin above 0x200
		// for the unavoidable single-frame integration step.
		if (jumpsCompleted > 0)
		{
			log.check(worstLandingPenetration <= 0x400,
			          "jump landings settle near spring equilibrium (<=0x400)");
		}
	}

	// Dump piece/cube layout for a single track to the test log so we can
	// inspect track geometry from the headless run. Used to sanity-check
	// stepping-stones-style tracks where pieces sit in non-adjacent cubes.
	void DumpTrackLayout(Logger& log, const int trackId)
	{
		if (!ConvertAmigaTrack(g_trackState, trackId)) return;

		const auto& t = g_trackState;
		log.log("\n--- DumpTrackLayout: track %d (%d pieces) ---\n",
		        trackId, t.NumTrackPieces);
		log.log("  PlayersStart=%d  StartLine=%d  HalfALap=%d\n",
		        t.PlayersStartPiece, t.StartLinePiece, t.HalfALapPiece);

		for (int p = 0; p < t.NumTrackPieces; ++p)
		{
			const auto& pc = t.Track[p];
			const char* typeName =
				pc.type == 0x00
					? "STRAIGHT"
					: pc.type == 0x40
					? "DIAGONAL"
					: pc.type == 0x80
					? "CURVE_R "
					: pc.type == 0xC0
					? "CURVE_L "
					: "????    ";
			log.log("  piece %2d: cube=(%2d,%2d) type=%s segs=%2d ang=%5d "
			        "%s%s\n",
			        p, pc.x, pc.z, typeName, pc.numSegments, pc.roughPieceAngle,
			        pc.oppositeDirection ? "REV " : "    ",
			        pc.curveToLeft ? "L" : " ");
		}

		// Also dump the 16x16 Track_Map so gaps between pieces are visible.
		log.log("  Track_Map (cube grid, '.'=empty, hex=piece%%16):\n");
		for (int z = 0; z < NUM_TRACK_CUBES; ++z)
		{
			char row[NUM_TRACK_CUBES + 1] = {};
			for (int x = 0; x < NUM_TRACK_CUBES; ++x)
			{
				const int32_t pIdx = t.Track_Map[x][z];
				row[x] = pIdx < 0 ? '.' : "0123456789ABCDEF"[pIdx & 0xF];
			}
			log.log("    z=%2d  %s\n", z, row);
		}
	}
} // namespace


// Headless test entry point. Returns process exit code.
int AppRunTests()
{
	Logger log;
	log.open("test_results.log");
	log.log("Stunt Car Racer self-tests\n");
	log.log("==========================\n");

	// Minimal init: sin/cos table + initial track load.
	InitialiseData(g_trackState);

	// Dump track 1 layout (STEPPING_STONES) for inspection.
	DumpTrackLayout(log, 1);

	// Run the chain-drop test on every track. Some tracks have elevated
	// starting pieces; the bug we are chasing only manifests on those.
	for (int t = 0; t < NUM_TRACKS; ++t)
		TestChainDropOntoTrack(log, t);

	// Drive a lap on every track.
	for (int t = 0; t < NUM_TRACKS; ++t)
		TestDriveLapAroundTrack(log, t);

	log.log("\n==========================\n");
	log.log("Total failures: %d\n", log.failures);
	const int rc = log.failures == 0 ? 0 : 1;
	log.close();
	return rc;
}
