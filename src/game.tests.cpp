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
	int32_t x = 0, y = 0, z = 0;
	int32_t x_angle = 0, y_angle = 0, z_angle = 0;
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
	g_gameState.Replay = false;
	g_gameState.ReplayRequested = false;

	CarVars car;

	// Frame 1 triggers the new-game branch in CarBehaviour, which calls
	// PositionCarAbovePiece and sets up the chain drop.
	StepFrame(car);

	const int32_t start_y = car.y;
	const int32_t start_chain = g_gameState.chain_height_remaining;
	const bool start_on_chains = g_gameState.on_chains != 0;

	log.log("  After first frame: player_y=%d, chain_remaining=%d, on_chains=%d, touching_road=%d\n",
	        start_y, start_chain, start_on_chains ? 1 : 0,
	        g_gameState.touching_road ? 1 : 0);

	log.check(start_on_chains, "car is on chains after new-game frame");
	log.check(start_chain > 0, "chain_height_remaining initialised positive");

	// Run enough frames for the chain to fully descend and the car to settle.
	// chain_height_remaining starts at 0xc00 * 256 / 4 = 0xC0000 = 786432
	// CHAIN_DESCENT_RATE per frame is 4096, so ~192 frames to finish chains.
	// Add a generous settling margin.
	constexpr int kMaxFrames = 400;
	int chainsDoneFrame = -1;
	int restingFrame = -1;
	int32_t settled_y = 0;
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
			log.log("  frame %3d: player_y=%d, chain_remaining=%d, on_chains=%d, "
			        "touching_road=%d, fl_road=%d, fl_actual=%d\n",
			        i, car.y, g_gameState.chain_height_remaining,
			        g_gameState.on_chains ? 1 : 0,
			        g_gameState.touching_road ? 1 : 0,
			        g_gameState.front_left_road_height,
			        g_gameState.front_left_actual_height);
		}
	}

	settled_y = car.y;
	settled_road_height = g_gameState.front_left_road_height;

	log.log("  Final: player_y=%d, chain_remaining=%d, on_chains=%d, "
	        "touching_road=%d, fl_road_height=%d, fl_actual_height=%d\n",
	        settled_y, g_gameState.chain_height_remaining,
	        g_gameState.on_chains ? 1 : 0,
	        g_gameState.touching_road ? 1 : 0,
	        settled_road_height, g_gameState.front_left_actual_height);
	log.log("  chains finished at frame: %d, first touched at frame: %d\n",
	        chainsDoneFrame, restingFrame);

	log.check(chainsDoneFrame > 0, "chains finished before timeout");
	log.check(g_gameState.touching_road, "car is touching road at end");
	log.check(restingFrame > 0, "car came into contact with road during sequence");

	// Sanity: the car should be near the road, not far below it.
	// front_left_actual_height ~= player_y >> 8 in internal units; if it has
	// fallen way below the road the difference will be very negative.
	const int32_t below = g_gameState.front_left_road_height -
	                      g_gameState.front_left_actual_height;
	log.log("  road - actual = %d (positive = car under road)\n", below);
	log.check(below < 0x400,
	          "car not deep below road surface (front_left road - actual < 0x400)");
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

	// Run the chain-drop test on every track. Some tracks have elevated
	// starting pieces; the bug we are chasing only manifests on those.
	for (int t = 0; t < NUM_TRACKS; ++t)
		TestChainDropOntoTrack(log, t);

	log.log("\n==========================\n");
	log.log("Total failures: %d\n", log.failures);
	const int rc = log.failures == 0 ? 0 : 1;
	log.close();
	return rc;
}
