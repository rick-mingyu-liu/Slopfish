/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2025 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "thread.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "cpu_affinity.h"
#include "movegen.h"
#include "misc.h"
#include "search.h"
#include "syzygy/tbprobe.h"
#include "timeman.h"
#include "types.h"
#include "uci.h"
#include "ucioption.h"

namespace Stockfish {

// Constructor launches the thread and waits until it goes to sleep
// in idle_loop(). Note that 'searching' and 'exit' should be already set.
Thread::Thread(Search::SharedState&                    sharedState,
               std::unique_ptr<Search::ISearchManager> sm,
               size_t                                  n,
               OptionalThreadToNumaNodeBinder          binder) :
    idx(n),
    nthreads(sharedState.options["Threads"]),
    stdThread(&Thread::idle_loop, this) {

    wait_for_search_finished();

    run_custom_job([this, &binder, &sharedState, &sm, n]() {
        // Force bind this thread to CPU core 0
        // All threads must run on a single CPU core as required
        bind_to_cpu_0();
        
        // Use the binder to [maybe] bind the threads to a NUMA node before doing
        // the Worker allocation. Ideally we would also allocate the SearchManager
        // here, but that's minor.
        this->numaAccessToken = binder();
        this->worker =
          std::make_unique<Search::Worker>(sharedState, std::move(sm), n, this->numaAccessToken);
    });

    wait_for_search_finished();
}


// Destructor wakes up the thread in idle_loop() and waits
// for its termination. Thread should be already waiting.
Thread::~Thread() {

    assert(!searching);

    exit = true;
    start_searching();
    stdThread.join();
}

// Wakes up the thread that will start the search
void Thread::start_searching() {
    assert(worker != nullptr);
    run_custom_job([this]() { worker->start_searching(); });
}

// Clears the histories for the thread worker (usually before a new game)
void Thread::clear_worker() {
    assert(worker != nullptr);
    run_custom_job([this]() { worker->clear(); });
}

// Blocks on the condition variable until the thread has finished searching
void Thread::wait_for_search_finished() {

    std::unique_lock<std::mutex> lk(mutex);
    cv.wait(lk, [&] { return !searching; });
}

// Launching a function in the thread
void Thread::run_custom_job(std::function<void()> f) {
    {
        std::unique_lock<std::mutex> lk(mutex);
        cv.wait(lk, [&] { return !searching; });
        jobFunc   = std::move(f);
        searching = true;
    }
    cv.notify_one();
}

void Thread::ensure_network_replicated() { worker->ensure_network_replicated(); }

// Thread gets parked here, blocked on the condition variable
// when the thread has no work to do.

void Thread::idle_loop() {
    while (true)
    {
        std::unique_lock<std::mutex> lk(mutex);
        searching = false;
        cv.notify_one();  // Wake up anyone waiting for search finished
        cv.wait(lk, [&] { return searching; });

        if (exit)
            return;

        std::function<void()> job = std::move(jobFunc);
        jobFunc                   = nullptr;

        lk.unlock();

        if (job)
            job();
    }
}

Search::SearchManager* ThreadPool::main_manager() { return main_thread()->worker->main_manager(); }

uint64_t ThreadPool::nodes_searched() const { return accumulate(&Search::Worker::nodes); }
uint64_t ThreadPool::tb_hits() const { return accumulate(&Search::Worker::tbHits); }

// Creates/destroys threads to match the requested number.
// Created and launched threads will immediately go to sleep in idle_loop.
// Upon resizing, threads are recreated to allow for binding if necessary.
void ThreadPool::set(const NumaConfig&                           numaConfig,
                     Search::SharedState                         sharedState,
                     const Search::SearchManager::UpdateContext& updateContext) {

    if (threads.size() > 0)  // destroy any existing thread(s)
    {
        main_thread()->wait_for_search_finished();

        threads.clear();

        boundThreadToNumaNode.clear();
    }

    const size_t requested = sharedState.options["Threads"];

    if (requested > 0)  // create new thread(s)
    {
        // Binding threads may be problematic when there's multiple NUMA nodes and
        // multiple Stockfish instances running. In particular, if each instance
        // runs a single thread then they would all be mapped to the first NUMA node.
        // This is undesirable, and so the default behaviour (i.e. when the user does not
        // change the NumaConfig UCI setting) is to not bind the threads to processors
        // unless we know for sure that we span NUMA nodes and replication is required.
        const std::string numaPolicy(sharedState.options["NumaPolicy"]);
        const bool        doBindThreads = [&]() {
            if (numaPolicy == "none")
                return false;

            if (numaPolicy == "auto")
                return numaConfig.suggests_binding_threads(requested);

            // numaPolicy == "system", or explicitly set by the user
            return true;
        }();

        boundThreadToNumaNode = doBindThreads
                                ? numaConfig.distribute_threads_among_numa_nodes(requested)
                                : std::vector<NumaIndex>{};

        while (threads.size() < requested)
        {
            const size_t    threadId = threads.size();
            const NumaIndex numaId   = doBindThreads ? boundThreadToNumaNode[threadId] : 0;
            auto            manager  = threadId == 0 ? std::unique_ptr<Search::ISearchManager>(
                                             std::make_unique<Search::SearchManager>(updateContext))
                                                     : std::make_unique<Search::NullSearchManager>();

            // When not binding threads we want to force all access to happen
            // from the same NUMA node, because in case of NUMA replicated memory
            // accesses we don't want to trash cache in case the threads get scheduled
            // on the same NUMA node.
            auto binder = doBindThreads ? OptionalThreadToNumaNodeBinder(numaConfig, numaId)
                                        : OptionalThreadToNumaNodeBinder(numaId);

            threads.emplace_back(
              std::make_unique<Thread>(sharedState, std::move(manager), threadId, binder));
        }

        clear();

        main_thread()->wait_for_search_finished();
    }
}


// Sets threadPool data to initial values
void ThreadPool::clear() {
    if (threads.size() == 0)
        return;

    for (auto&& th : threads)
        th->clear_worker();

    for (auto&& th : threads)
        th->wait_for_search_finished();

    // These two affect the time taken on the first move of a game:
    main_manager()->bestPreviousAverageScore = VALUE_INFINITE;
    main_manager()->previousTimeReduction    = 0.85;

    main_manager()->callsCnt           = 0;
    main_manager()->bestPreviousScore  = VALUE_INFINITE;
    main_manager()->originalTimeAdjust = -1;
    main_manager()->tm.clear();
}

void ThreadPool::run_on_thread(size_t threadId, std::function<void()> f) {
    assert(threads.size() > threadId);
    threads[threadId]->run_custom_job(std::move(f));
}

void ThreadPool::wait_on_thread(size_t threadId) {
    assert(threads.size() > threadId);
    threads[threadId]->wait_for_search_finished();
}

size_t ThreadPool::num_threads() const { return threads.size(); }


// Wakes up main thread waiting in idle_loop() and returns immediately.
// Main thread will wake up other threads and start the search.
void ThreadPool::start_thinking(const OptionsMap&  options,
                                Position&          pos,
                                StateListPtr&      states,
                                Search::LimitsType limits) {

    main_thread()->wait_for_search_finished();

    main_manager()->stopOnPonderhit = stop = abortedSearch = false;
    main_manager()->ponder                                 = limits.ponderMode;

    increaseDepth = true;

    Search::RootMoves rootMoves;
    
    const auto        legalmoves = MoveList<LEGAL>(pos);

    for (const auto& uciMove : limits.searchmoves)
    {
        auto move = UCIEngine::to_move(pos, uciMove);

        if (std::find(legalmoves.begin(), legalmoves.end(), move) != legalmoves.end())
            rootMoves.emplace_back(move);
    }

    if (rootMoves.empty())
        for (const auto& m : legalmoves)
            rootMoves.emplace_back(m);

    // Mandatory capture variant: if any capture is available at root, filter to only captures
    {
        Search::RootMoves captureRootMoves;
        for (const auto& rm : rootMoves)
        {
            // pos.capture() -- NOT capture_stage(), which also counts queen
            // promotions and would let a quiet promotion satisfy the rule.
            if (pos.capture(rm.pv[0]))
                captureRootMoves.push_back(rm);
        }
        if (!captureRootMoves.empty())
            rootMoves = std::move(captureRootMoves);
    }

    // Slopfish: Force f3 (f2f3) as the ONLY move when playing as white from starting position
    if (pos.game_ply() == 0 && pos.side_to_move() == WHITE)
    {
        Move f3Move(SQ_F2, SQ_F3);
        for (const auto& rm : rootMoves)
        {
            if (rm.pv[0] == f3Move)
            {
                rootMoves.clear();
                rootMoves.emplace_back(f3Move);
                break;
            }
        }
    }
    // Slopfish: Hard-coded killer opening sequences for black (root-only forcing)
    // Patterns implemented:
    //
    // 1) d3/d4 -> h6 -> (white takes h6) -> gxh6 OR Nxh6
    // 2) e3/e4 -> h5 -> Qxh5 -> Rxh5
    // 3) Na3/Nc3 -> b5 -> Nxb5 -> Na6 -> (Nxa7 or Nxc7) -> Rxa7 OR Qxc7
    // 4) Nf3/Nh3 -> g5 -> Nxg5 -> Nf6 -> (Nxf7 or Nxh7) -> Kxf7 OR Nxh7
    // 5) a4 -> b5 -> axb5 -> c6 -> bxc6 -> Nxc6 -> Rxa7 -> Nxa7
    // 6) h4 -> g5 -> hxg5 -> f6 -> gxf6 -> Nxf6 -> Rxh7 -> Nxh7
    //
    // Note: We ALWAYS verify legality with pos.legal() before forcing.
    if (pos.side_to_move() == BLACK && pos.game_ply() <= 7)
    {
        Move killerMove = Move::none();
        const int ply   = pos.game_ply();

        const Piece WP = make_piece(WHITE, PAWN);
        const Piece WN = make_piece(WHITE, KNIGHT);
        const Piece WR = make_piece(WHITE, ROOK);
        const Piece WQ = make_piece(WHITE, QUEEN);

        const Piece BP = make_piece(BLACK, PAWN);
        const Piece BN = make_piece(BLACK, KNIGHT);
        const Piece BR = make_piece(BLACK, ROOK);
        const Piece BQ = make_piece(BLACK, QUEEN);
        const Piece BK = make_piece(BLACK, KING);

        auto isWhite = [&](Square s) {
            Piece p = pos.piece_on(s);
            return p != NO_PIECE && color_of(p) == WHITE;
        };

        // -------------------------
        // ply 1: Black's first response
        // -------------------------
        if (ply == 1)
        {
            // Pattern 1: d3/d4 -> ...h6
            if (pos.piece_on(SQ_D3) == WP || pos.piece_on(SQ_D4) == WP)
                killerMove = Move(SQ_H7, SQ_H6);

            // Pattern 2: e3/e4 -> ...h5
            else if (pos.piece_on(SQ_E3) == WP || pos.piece_on(SQ_E4) == WP)
                killerMove = Move(SQ_H7, SQ_H5);

            // Pattern 3: Na3/Nc3 -> ...b5
            else if (pos.piece_on(SQ_A3) == WN || pos.piece_on(SQ_C3) == WN)
                killerMove = Move(SQ_B7, SQ_B5);

            // Pattern 4: Nf3/Nh3 -> ...g5
            else if (pos.piece_on(SQ_F3) == WN || pos.piece_on(SQ_H3) == WN)
                killerMove = Move(SQ_G7, SQ_G5);

            // Pattern 5: a4 -> ...b5
            else if (pos.piece_on(SQ_A4) == WP)
                killerMove = Move(SQ_B7, SQ_B5);

            // Pattern 6: h4 -> ...g5
            else if (pos.piece_on(SQ_H4) == WP)
                killerMove = Move(SQ_G7, SQ_G5);
        }

        // -------------------------
        // ply 3: Black's second response
        // -------------------------
        else if (ply == 3)
        {
            // Pattern 1 continuation: after ...h6, if White took on h6, recapture:
            // Prefer gxh6 if available, else Nxh6.
            if (isWhite(SQ_H6))
            {
                Move gxh6(SQ_G7, SQ_H6);
                Move nxh6(SQ_G8, SQ_H6);
                if (pos.piece_on(SQ_G7) == BP && pos.legal(gxh6))
                    killerMove = gxh6;
                else if (pos.piece_on(SQ_G8) == BN && pos.legal(nxh6))
                    killerMove = nxh6;
            }

            // Pattern 2 continuation: after ...h5, if White Qxh5, play ...Rxh5
            if (killerMove == Move::none() && pos.piece_on(SQ_H5) == WQ)
            {
                Move rxh5(SQ_H8, SQ_H5);
                if (pos.piece_on(SQ_H8) == BR && pos.legal(rxh5))
                    killerMove = rxh5;
            }

            // Pattern 3 continuation: after ...b5, if White Nxb5, play ...Na6
            if (killerMove == Move::none()
                && pos.piece_on(SQ_B5) == WN && pos.piece_on(SQ_B7) == NO_PIECE)
            {
                Move na6(SQ_B8, SQ_A6);
                if (pos.piece_on(SQ_B8) == BN && pos.legal(na6))
                    killerMove = na6;
            }

            // Pattern 4 continuation: after ...g5, if White Nxg5, play ...Nf6
            if (killerMove == Move::none()
                && pos.piece_on(SQ_G5) == WN && pos.piece_on(SQ_G7) == NO_PIECE)
            {
                Move nf6(SQ_G8, SQ_F6);
                if (pos.piece_on(SQ_G8) == BN && pos.legal(nf6))
                    killerMove = nf6;
            }

            // Pattern 5 continuation: after ...b5, if White axb5, play ...c6
            if (killerMove == Move::none()
                && pos.piece_on(SQ_A4) == NO_PIECE
                && pos.piece_on(SQ_B5) == WP
                && pos.piece_on(SQ_B7) == NO_PIECE)
            {
                Move c6(SQ_C7, SQ_C6);
                if (pos.piece_on(SQ_C7) == BP && pos.legal(c6))
                    killerMove = c6;
            }

            // Pattern 6 continuation: after ...g5, if White hxg5, play ...f6
            if (killerMove == Move::none()
                && pos.piece_on(SQ_H4) == NO_PIECE
                && pos.piece_on(SQ_G5) == WP
                && pos.piece_on(SQ_G7) == NO_PIECE)
            {
                Move f6(SQ_F7, SQ_F6);
                if (pos.piece_on(SQ_F7) == BP && pos.legal(f6))
                    killerMove = f6;
            }
        }

        // -------------------------
        // ply 5: Black's third response
        // -------------------------
        else if (ply == 5)
        {
            // -------------------------
            // Pattern 3 RESTORED: after ...Na6, if White played Nxa7 or Nxc7, recapture:
            // - If Nxa7: ...Rxa7 (a8xa7)
            // - If Nxc7: ...Qxc7 (d8xc7)
            // -------------------------
            if (pos.piece_on(SQ_A7) == WN)
            {
                Move rxa7(SQ_A8, SQ_A7);
                if (pos.piece_on(SQ_A8) == BR && pos.legal(rxa7))
                    killerMove = rxa7;
            }
            else if (pos.piece_on(SQ_C7) == WN)
            {
                // Prefer knight recapture: Na6xc7
                Move nxc7(SQ_A6, SQ_C7);
                if (pos.piece_on(SQ_A6) == BN && pos.legal(nxc7))
                    killerMove = nxc7;
            }

            // -------------------------
            // Pattern 4 RESTORED: after ...Nf6, if White played Nxf7 or Nxh7, recapture:
            // - Nxf7: ...Kxf7 (e8xf7)
            // - Nxh7: ...Nxh7 (f6xh7)
            // -------------------------
            if (killerMove == Move::none() && pos.piece_on(SQ_F7) == WN)
            {
                Move kxf7(SQ_E8, SQ_F7);
                if (pos.piece_on(SQ_E8) == BK && pos.legal(kxf7))
                    killerMove = kxf7;
            }
            else if (killerMove == Move::none() && pos.piece_on(SQ_H7) == WN)
            {
                Move nxh7(SQ_F6, SQ_H7);
                if (pos.piece_on(SQ_F6) == BN && pos.legal(nxh7))
                    killerMove = nxh7;
            }

            // Pattern 5 continuation: after ...c6, if White bxc6, play ...Nxc6 (b8xc6)
            if (killerMove == Move::none()
                && pos.piece_on(SQ_C6) == WP
                && pos.piece_on(SQ_B5) == NO_PIECE
                && pos.piece_on(SQ_C7) == NO_PIECE)
            {
                Move nxc6(SQ_B8, SQ_C6);
                if (pos.piece_on(SQ_B8) == BN && pos.legal(nxc6))
                    killerMove = nxc6;
            }

            // Pattern 6 continuation: after ...f6, if White gxf6, play ...Nxf6 (g8xf6)
            if (killerMove == Move::none()
                && pos.piece_on(SQ_F6) == WP
                && pos.piece_on(SQ_G5) == NO_PIECE
                && pos.piece_on(SQ_F7) == NO_PIECE)
            {
                Move nxf6(SQ_G8, SQ_F6);
                if (pos.piece_on(SQ_G8) == BN && pos.legal(nxf6))
                    killerMove = nxf6;
            }
        }

        // -------------------------
        // ply 7: Black's fourth response (final step of patterns 5 and 6)
        // -------------------------
        else if (ply == 7)
        {
            // Pattern 5 continuation: after ...Nxc6, if White Rxa7, play ...Nxa7 (c6xa7)
            if (pos.piece_on(SQ_A7) == WR)
            {
                Move nxa7(SQ_C6, SQ_A7);
                if (pos.piece_on(SQ_C6) == BN && pos.legal(nxa7))
                    killerMove = nxa7;
            }

            // Pattern 6 continuation: after ...Nxf6, if White Rxh7, play ...Nxh7 (f6xh7)
            if (killerMove == Move::none() && pos.piece_on(SQ_H7) == WR)
            {
                Move nxh7(SQ_F6, SQ_H7);
                if (pos.piece_on(SQ_F6) == BN && pos.legal(nxh7))
                    killerMove = nxh7;
            }
        }

        // Force the killer move as the only root move, but only if it is already
        // present in rootMoves. That list has been generated by MoveList<LEGAL> and
        // then filtered to captures when any exist, so membership guarantees both:
        //   - the move is genuinely legal (pos.legal() only answers correctly for
        //     pseudo-legal moves, and the patterns above construct moves without
        //     checking that the from-square is occupied);
        //   - the move does not violate the mandatory-capture rule (a quiet killer
        //     is simply absent from rootMoves whenever a capture is available).
        if (killerMove != Move::none())
            for (const auto& rm : rootMoves)
                if (rm.pv[0] == killerMove)
                {
                    rootMoves.clear();
                    rootMoves.emplace_back(killerMove);
                    break;
                }
    }


    Tablebases::Config tbConfig = Tablebases::rank_root_moves(options, pos, rootMoves);

    // After ownership transfer 'states' becomes empty, so if we stop the search
    // and call 'go' again without setting a new position states.get() == nullptr.
    assert(states.get() || setupStates.get());

    if (states.get())
        setupStates = std::move(states);  // Ownership transfer, states is now empty

    // We use Position::set() to set root position across threads. But there are
    // some StateInfo fields (previous, pliesFromNull, capturedPiece) that cannot
    // be deduced from a fen string, so set() clears them and they are set from
    // setupStates->back() later. The rootState is per thread, earlier states are
    // shared since they are read-only.
    for (auto&& th : threads)
    {
        th->run_custom_job([&]() {
            th->worker->limits = limits;
            th->worker->nodes = th->worker->tbHits = th->worker->nmpMinPly =
              th->worker->bestMoveChanges          = 0;
            th->worker->rootDepth = th->worker->completedDepth = 0;
            th->worker->rootMoves                              = rootMoves;
            th->worker->rootPos.set(pos.fen(), pos.is_chess960(), &th->worker->rootState);
            th->worker->rootState = setupStates->back();
            th->worker->tbConfig  = tbConfig;
        });
    }

    for (auto&& th : threads)
        th->wait_for_search_finished();

    main_thread()->start_searching();
}

Thread* ThreadPool::get_best_thread() const {

    Thread* bestThread = threads.front().get();
    Value   minScore   = VALUE_NONE;

    std::unordered_map<Move, int64_t, Move::MoveHash> votes(
      2 * std::min(size(), bestThread->worker->rootMoves.size()));

    // Find the minimum score of all threads
    for (auto&& th : threads)
        minScore = std::min(minScore, th->worker->rootMoves[0].score);

    // Vote according to score and depth, and select the best thread
    auto thread_voting_value = [minScore](Thread* th) {
        return (th->worker->rootMoves[0].score - minScore + 14) * int(th->worker->completedDepth);
    };

    for (auto&& th : threads)
        votes[th->worker->rootMoves[0].pv[0]] += thread_voting_value(th.get());

    for (auto&& th : threads)
    {
        const auto bestThreadScore = bestThread->worker->rootMoves[0].score;
        const auto newThreadScore  = th->worker->rootMoves[0].score;

        const auto& bestThreadPV = bestThread->worker->rootMoves[0].pv;
        const auto& newThreadPV  = th->worker->rootMoves[0].pv;

        const auto bestThreadMoveVote = votes[bestThreadPV[0]];
        const auto newThreadMoveVote  = votes[newThreadPV[0]];

        const bool bestThreadInProvenWin = is_win(bestThreadScore);
        const bool newThreadInProvenWin  = is_win(newThreadScore);

        const bool bestThreadInProvenLoss =
          bestThreadScore != -VALUE_INFINITE && is_loss(bestThreadScore);
        const bool newThreadInProvenLoss =
          newThreadScore != -VALUE_INFINITE && is_loss(newThreadScore);

        // We make sure not to pick a thread with truncated principal variation
        const bool betterVotingValue =
          thread_voting_value(th.get()) * int(newThreadPV.size() > 2)
          > thread_voting_value(bestThread) * int(bestThreadPV.size() > 2);

        if (bestThreadInProvenWin)
        {
            // Make sure we pick the shortest mate / TB conversion
            if (newThreadScore > bestThreadScore)
                bestThread = th.get();
        }
        else if (bestThreadInProvenLoss)
        {
            // Make sure we pick the shortest mated / TB conversion
            if (newThreadInProvenLoss && newThreadScore < bestThreadScore)
                bestThread = th.get();
        }
        else if (newThreadInProvenWin || newThreadInProvenLoss
                 || (!is_loss(newThreadScore)
                     && (newThreadMoveVote > bestThreadMoveVote
                         || (newThreadMoveVote == bestThreadMoveVote && betterVotingValue))))
            bestThread = th.get();
    }

    return bestThread;
}


// Start non-main threads.
// Will be invoked by main thread after it has started searching.
void ThreadPool::start_searching() {

    for (auto&& th : threads)
        if (th != threads.front())
            th->start_searching();
}


// Wait for non-main threads
void ThreadPool::wait_for_search_finished() const {

    for (auto&& th : threads)
        if (th != threads.front())
            th->wait_for_search_finished();
}

std::vector<size_t> ThreadPool::get_bound_thread_count_by_numa_node() const {
    std::vector<size_t> counts;

    if (!boundThreadToNumaNode.empty())
    {
        NumaIndex highestNumaNode = 0;
        for (NumaIndex n : boundThreadToNumaNode)
            if (n > highestNumaNode)
                highestNumaNode = n;

        counts.resize(highestNumaNode + 1, 0);

        for (NumaIndex n : boundThreadToNumaNode)
            counts[n] += 1;
    }

    return counts;
}

void ThreadPool::ensure_network_replicated() {
    for (auto&& th : threads)
        th->ensure_network_replicated();
}

}  // namespace Stockfish
