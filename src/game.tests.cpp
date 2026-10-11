//
// game.tests.cpp - Headless self-tests run via the /test command-line option.
//
// Drives the car physics directly (no window, no rendering, no sound) to
// verify behaviour such as the chain-drop start: the car should be lowered
// onto the track surface and come to rest there, not fall through. The menu
// bar and the window reactor are checked the same way, against a fake frame.
//
// All output is written to test_results.log in the working directory and to
// stderr, and the process exits with code 0 on success or 1 on failure.
//

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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
		car.x_angle = 0x1800;
		car.z_angle = 0x1000;

		// Frame 1 triggers the new-game branch in CarBehaviour, which calls
		// PositionCarAbovePiece and sets up the chain drop.
		StepFrame(car);

		const double start_y = car.y;
		const double chain_x = car.x;
		const double chain_z = car.z;
		const int32_t start_chain = static_cast<int32_t>(g_gameState.chain_height_remaining);
		const bool start_on_chains = g_gameState.on_chains != 0;
		bool chain_position_stable = true;
		bool chain_angles_level =
			g_gameState.player_x_angle == 0 && g_gameState.player_z_angle == 0;

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
			if (g_gameState.on_chains)
			{
				chain_position_stable = chain_position_stable &&
					car.x == chain_x && car.z == chain_z;
				chain_angles_level = chain_angles_level &&
					g_gameState.player_x_angle == 0 && g_gameState.player_z_angle == 0;
			}

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
		log.check(chain_position_stable, "chain placement does not drift in XZ");
		log.check(chain_angles_level, "chain placement clears stale crash pitch and roll");
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
			const bool grounded = g_gameState.touching_road != 0;

			if (g_gameState.player_current_piece != prevPiece)
			{
				++piecesAdvanced;
				prevPiece = g_gameState.player_current_piece;
			}
			if (g_gameState.player_current_piece == g_trackState.HalfALapPiece)
				visitedHalfway = true;

			// Jump detection: edge-trigger on grounded->airborne and airborne->grounded.
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
			if (g_gameState.off_track_count > GameState::OFF_TRACK_LIMIT)
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

	void TestStraightSegmentSelection(Logger& log, const int trackId)
	{
		if (!ConvertAmigaTrack(g_trackState, trackId))
		{
			log.check(false, "track loaded for segment selection");
			return;
		}

		bool all_segments_selected = true;
		for (int piece = 0; piece < g_trackState.NumTrackPieces; ++piece)
		{
			const auto& track_piece = g_trackState.Track[piece];
			if (track_piece.type & 0x80)
				continue;

			const double piece_x = track_piece.x * WORLD_CUBE_SIZE;
			const double piece_z = track_piece.z * WORLD_CUBE_SIZE;
			for (int segment = 0; segment < track_piece.numSegments; ++segment)
			{
				const int offset = segment * 4;
				const double x = piece_x +
					(track_piece.coords[offset].x + track_piece.coords[offset + 1].x +
					 track_piece.coords[offset + 4].x + track_piece.coords[offset + 5].x) / 4.0;
				const double z = piece_z +
					(track_piece.coords[offset].z + track_piece.coords[offset + 1].z +
					 track_piece.coords[offset + 4].z + track_piece.coords[offset + 5].z) / 4.0;
				g_gameState.player_x = x;
				g_gameState.player_z = z;
				g_gameState.player_current_piece = piece;
				CalculatePlayersRoadPosition(g_trackState, g_gameState);
				all_segments_selected = all_segments_selected &&
					g_gameState.player_current_piece == piece &&
					g_gameState.player_current_segment == segment;
			}
		}

		log.check(all_segments_selected,
		          "straight road samples use the containing segment");
	}

	// Every shortcut the menu advertises has to reach the game. The move onto
	// the shared platform dropped the old accelerator table, the labels kept
	// promising the keys, and nothing noticed.
	void TestMenuShortcuts(Logger& log)
	{
		log.log("\n--- TestMenuShortcuts ---\n");

		const auto menu = BuildGameMenu();
		std::vector<const pf::menu_command*> items;
		std::function<void(const std::vector<pf::menu_command>&)> collect =
			[&](const std::vector<pf::menu_command>& level)
			{
				for (const auto& item : level)
				{
					items.push_back(&item);
					collect(item.children);
				}
			};
		collect(menu);

		struct Expected
		{
			int id;
			unsigned int key;
			const char* what;
		};
		static constexpr Expected bindings[] = {
			{IDM_BACK_TO_MENU, pf::platform_key::Escape, "Esc returns to the track menu"},
			{IDM_PAUSE, 'P', "P pauses"},
			{IDM_RESUME, 'O', "O resumes"},
			{IDM_REVERSE_CAR, 'R', "R reverses the car"},
			{IDM_SHOW_STATS, pf::platform_key::F5, "F5 shows statistics"},
			{IDM_PAUSE_PLAYER, pf::platform_key::F6, "F6 pauses the player"},
			{IDM_PAUSE_OPPONENT, pf::platform_key::F7, "F7 pauses the opponent"},
			{IDM_SPEED_INCREASE, pf::platform_key::F9, "F9 increases game speed"},
			{IDM_SPEED_DECREASE, pf::platform_key::F10, "F10 decreases game speed"},
			{IDM_RESTART_RACE, 'Z', "Z restarts the race"},
		};
		for (const auto& expected : bindings)
		{
			const auto found = std::ranges::find_if(items, [&](const pf::menu_command* item)
			{
				return item->id == expected.id;
			});
			log.check(found != items.end() && (*found)->accel.key == expected.key &&
			          (*found)->accel.modifiers == pf::key_mod::none, expected.what);
		}

		// The platform writes a bound key into the label itself, so a label that
		// spells one out as well shows it twice. Start Race is the one written
		// hint left: S is read by AppHandleKeyDown, not by an accelerator.
		bool labels_ok = true;
		for (const auto* item : items)
		{
			const bool hint = item->text.find('\t') != std::string::npos;
			if (hint && (!item->accel.empty() || item->id != IDM_START_RACE))
			{
				log.log("  label spells out a key: %s\n", item->text.c_str());
				labels_ok = false;
			}
		}
		log.check(labels_ok, "only the keys AppHandleKeyDown reads are written into labels");

		// Restart Race used to re-drop the cars and nothing else, so a finished
		// race came back with its RACE LOST banner and lap count still showing.
		const auto restart = std::ranges::find_if(items, [](const pf::menu_command* item)
		{
			return item->id == IDM_RESTART_RACE;
		});
		if (restart == items.end() || !(*restart)->action)
		{
			log.check(false, "Restart Race exists");
			return;
		}
		g_gameMode = GAME_IN_PROGRESS;
		g_gameState.raceFinished = true;
		g_gameState.raceWon = false;
		g_gameState.lapNumber[PLAYER] = 2;
		g_gameState.lapNumber[OPPONENT] = 4;
		g_gameState.boostReserve = 0;
		g_gameState.bNewGame = false;
		(*restart)->action();
		log.check(g_gameMode == GAME_IN_PROGRESS && g_gameState.bNewGame, "Restart Race drops both cars again");
		log.check(!g_gameState.raceFinished && g_gameState.lapNumber[PLAYER] == 0 &&
		          g_gameState.lapNumber[OPPONENT] == 0, "Restart Race clears the finish and the lap count");
		log.check(g_gameState.boostReserve == g_trackState.StandardBoost, "Restart Race refills the boost");
	}

	// A pause belongs to the race it was taken in. P then Esc used to leave the
	// track menu frozen with Resume lit, and the next race came up frozen on the
	// old race's last frame with nothing on screen to say why.
	void TestPauseEndsWithRace(Logger& log)
	{
		log.log("\n--- TestPauseEndsWithRace ---\n");

		const auto menu = BuildGameMenu();
		std::function<const pf::menu_command*(const std::vector<pf::menu_command>&, int)> find =
			[&](const std::vector<pf::menu_command>& level, const int id) -> const pf::menu_command*
			{
				for (const auto& item : level)
				{
					if (item.id == id) return &item;
					if (const auto* child = find(item.children, id)) return child;
				}
				return nullptr;
			};
		const auto* pause = find(menu, IDM_PAUSE);
		const auto* resume = find(menu, IDM_RESUME);
		const auto* back = find(menu, IDM_BACK_TO_MENU);
		const auto* start = find(menu, IDM_START_RACE);
		const auto* restart = find(menu, IDM_RESTART_RACE);
		if (!pause || !resume || !back || !start || !restart || !resume->is_enabled ||
		    g_trackState.TrackID == NO_TRACK)
		{
			log.check(false, "the pause and race items exist and a track is loaded");
			return;
		}

		// The platform runs an item only while it is enabled, and so do the tests.
		const auto run = [](const pf::menu_command* item)
		{
			const bool enabled = !item->is_enabled || item->is_enabled();
			if (enabled) item->action();
			return enabled;
		};
		const auto paused = [&] { return resume->is_enabled(); };

		g_gameMode = GAME_IN_PROGRESS;
		log.check(run(pause) && paused(), "P pauses a race");
		log.check(run(back) && g_gameMode == TRACK_MENU && !paused(),
		          "Esc from a paused race leaves the track menu running");

		log.check(run(start) && g_gameMode == TRACK_PREVIEW, "Start Race opens the preview");
		log.check(run(pause) && run(start) && g_gameMode == GAME_IN_PROGRESS && !paused(),
		          "a race started from a paused preview runs");

		log.check(run(pause) && run(restart) && g_gameMode == GAME_IN_PROGRESS && !paused(),
		          "a race restarted while paused runs");

		run(back);
	}

	// Minimising stands the race still but used to leave its engine droning:
	// the engine is a looping sound, and only OnFrameMove, which the frame loop
	// skips while minimised, stopped it.
	void TestMinimizeSilencesEngine(Logger& log)
	{
		log.log("\n--- TestMinimizeSilencesEngine ---\n");

		g_soundState.engineSoundPlaying = true;
		AppIdleMinimized();
		log.check(!g_soundState.engineSoundPlaying, "a minimised race stops its engine");
	}

	// A window that records what the reactor asks of it; everything else is inert.
	struct RecordingFrame final : pf::window_frame
	{
		std::string text;
		pf::irect bounds{};
		bool closed = false;

		void set_reactor(pf::frame_reactor_ptr) override {}
		void notify_size() override {}
		pf::irect get_client_rect() const override { return bounds; }
		void invalidate() override {}
		void invalidate_rect(const pf::irect&) override {}
		void set_focus() override {}
		bool has_focus() const override { return true; }
		void set_capture() override {}
		void release_capture() override {}
		uint32_t set_timer(uint32_t, uint32_t) override { return 0; }
		void kill_timer(uint32_t) override {}
		pf::ipoint screen_to_client(const pf::ipoint pt) const override { return pt; }
		void set_cursor_shape(pf::cursor_shape) override {}
		void move_window(const pf::irect& r) override { bounds = r; }
		void show(bool) override {}
		bool is_visible() const override { return true; }
		void set_text(const std::string_view t) override { text = t; }
		std::string text_from_clipboard() override { return {}; }
		bool text_to_clipboard(std::string_view) override { return false; }
		placement get_placement() const override { return {}; }
		void set_placement(const placement&) override {}
		void track_mouse_leave() override {}
		bool is_key_down(unsigned int) const override { return false; }
		bool is_key_down_async(unsigned int) const override { return false; }
		pf::window_frame_ptr create_child(std::string_view, uint32_t, pf::color_t) const & override { return nullptr; }
		void close() override { closed = true; }
		int message_box(std::string_view, std::string_view, uint32_t) override { return 0; }
		void present_pixels(const uint32_t*, int, int) override {}
		void set_menu(std::vector<pf::menu_command>) override {}
		std::unique_ptr<pf::measure_context> create_measure_context() const override { return nullptr; }
		void show_popup_menu(const std::vector<pf::menu_command>&, const pf::ipoint&) override {}
		double get_dpi_scale() const override { return 1.5; }
		void accept_drop_files(bool) override {}
		pf::toolbar_frame_ptr create_address_bar(const pf::address_bar_config&) override { return nullptr; }
	};

	// The platform calls app_init before the window exists, and leaves WM_CLOSE
	// to the reactor. Both lost the game something it once had: a title and a
	// 640x480 window, and a close box that worked.
	void TestWindowFrame(Logger& log)
	{
		log.log("\n--- TestWindowFrame ---\n");

		const auto reactor = CreateGameReactor();
		const auto recorder = std::make_shared<RecordingFrame>();
		pf::window_frame_ptr frame = recorder;

		reactor->handle_message(frame, pf::message_type::create, {});
		log.check(recorder->text == "Stunt Car Racer", "the window is titled once it exists");

		const auto screen = pf::platform_screen_size();
		const int cx = std::min(WINDOW_WIDTH * 3 / 2, screen.cx);
		const int cy = std::min(WINDOW_HEIGHT * 3 / 2, screen.cy);
		log.log("  bounds %d,%d %dx%d on a %dx%d screen\n", recorder->bounds.left, recorder->bounds.top,
		        recorder->bounds.width(), recorder->bounds.height(), screen.cx, screen.cy);
		log.check(recorder->bounds.width() == cx && recorder->bounds.height() == cy,
		          "the window is 640x480 scaled by the monitor's DPI");
		log.check(recorder->bounds.left == std::max(0, (screen.cx - cx) / 2) &&
		          recorder->bounds.top == std::max(0, (screen.cy - cy) / 2),
		          "the window is centred on the screen");

		reactor->handle_message(frame, pf::message_type::close, {});
		log.check(recorder->closed, "the close box closes the window");
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

	for (int t = 0; t < NUM_TRACKS; ++t)
		TestStraightSegmentSelection(log, t);

	// Drive a lap on every track.
	for (int t = 0; t < NUM_TRACKS; ++t)
		TestDriveLapAroundTrack(log, t);

	TestMenuShortcuts(log);
	TestPauseEndsWithRace(log);
	TestMinimizeSilencesEngine(log);
	TestWindowFrame(log);

	log.log("\n==========================\n");
	log.log("Total failures: %d\n", log.failures);
	const int rc = log.failures == 0 ? 0 : 1;
	log.close();
	return rc;
}
