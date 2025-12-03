/*
  Slopfish - Opening Cache for Forced Capture Chess Variant
  
  This cache stores position -> best move mappings to speed up opening play.
  Moves are only cached after deep searches, ensuring quality.
*/

#ifndef OPENING_CACHE_H_INCLUDED
#define OPENING_CACHE_H_INCLUDED

#include <fstream>
#include <string>
#include <unordered_map>

#include "types.h"

namespace Stockfish {

// Cache entry storing best move with quality metrics
struct CacheEntry {
    Move  bestMove;
    Depth depth;      // Search depth used to find this move
    Value score;      // Evaluation score
    
    CacheEntry() : bestMove(Move::none()), depth(0), score(VALUE_NONE) {}
    CacheEntry(Move m, Depth d, Value s) : bestMove(m), depth(d), score(s) {}
};

class OpeningCache {
public:
    // Maximum ply to cache (only cache opening positions)
    static constexpr int MAX_PLY_TO_CACHE = 20;
    
    // Minimum search depth required to trust a cached move
    static constexpr Depth MIN_CACHE_DEPTH = 8;
    
    // Singleton access
    static OpeningCache& instance() {
        static OpeningCache cache;
        return cache;
    }
    
    // Store a move in the cache (only if deeper than existing entry)
    void store(Key posKey, Move bestMove, Depth depth, Value score, int ply) {
        // Only cache opening positions
        if (ply > MAX_PLY_TO_CACHE)
            return;
        
        // Only cache if search was deep enough
        if (depth < MIN_CACHE_DEPTH)
            return;
        
        auto it = cache.find(posKey);
        if (it == cache.end() || depth > it->second.depth) {
            cache[posKey] = CacheEntry(bestMove, depth, score);
        }
    }
    
    // Lookup a move from cache
    // Returns Move::none() if not found or not deep enough
    Move lookup(Key posKey, Depth minDepth = MIN_CACHE_DEPTH) const {
        auto it = cache.find(posKey);
        if (it != cache.end() && it->second.depth >= minDepth) {
            return it->second.bestMove;
        }
        return Move::none();
    }
    
    // Check if position is in cache
    bool contains(Key posKey) const {
        return cache.find(posKey) != cache.end();
    }
    
    // Get cache size
    size_t size() const { return cache.size(); }
    
    // Clear the cache
    void clear() { cache.clear(); }
    
    // Save cache to file
    bool save(const std::string& filename) const {
        std::ofstream file(filename, std::ios::binary);
        if (!file)
            return false;
        
        size_t count = cache.size();
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        
        for (const auto& [key, entry] : cache) {
            file.write(reinterpret_cast<const char*>(&key), sizeof(key));
            file.write(reinterpret_cast<const char*>(&entry.bestMove), sizeof(entry.bestMove));
            file.write(reinterpret_cast<const char*>(&entry.depth), sizeof(entry.depth));
            file.write(reinterpret_cast<const char*>(&entry.score), sizeof(entry.score));
        }
        
        return true;
    }
    
    // Load cache from file
    bool load(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file)
            return false;
        
        size_t count;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        
        cache.clear();
        for (size_t i = 0; i < count; ++i) {
            Key key;
            CacheEntry entry;
            
            file.read(reinterpret_cast<char*>(&key), sizeof(key));
            file.read(reinterpret_cast<char*>(&entry.bestMove), sizeof(entry.bestMove));
            file.read(reinterpret_cast<char*>(&entry.depth), sizeof(entry.depth));
            file.read(reinterpret_cast<char*>(&entry.score), sizeof(entry.score));
            
            if (file)
                cache[key] = entry;
        }
        
        return true;
    }

private:
    OpeningCache() = default;
    std::unordered_map<Key, CacheEntry> cache;
};

}  // namespace Stockfish

#endif  // #ifndef OPENING_CACHE_H_INCLUDED

