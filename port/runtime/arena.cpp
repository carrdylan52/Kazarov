#include "arena.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct Block {
    const void *owner;
    ArenaMemory memory;
};

std::vector<Block> g_blocks;
// Blocks an owner outgrew stay mapped: retail's memory never went away, and a
// pointer the game kept into an old arena reads zeroes rather than faulting.
std::vector<Block> g_retired;

Block MapBlock(const void *owner, std::size_t bytes, std::size_t lead) {
    return {owner, ArenaMemoryMap(bytes, lead)};
}

// Every 32-bit truncation the game makes of an arena pointer is widened; one left behind only shows
// when the pointer has bits above 32, so a debug build refuses to run where none would.
void CheckSomeBlockHigh() {
#ifndef NDEBUG
    for (const Block &block : g_blocks) {
        if (IsAbove4GiB(block.memory.base)) {
            return;
        }
    }
    std::fprintf(stderr, "arena: none lies above 4 GiB, where a truncated pointer would show\n");
    std::abort();
#endif
}

} // namespace

unsigned char *ArenaBlock(const void *owner, std::size_t bytes, std::size_t lead) {
    for (Block &block : g_blocks) {
        if (block.owner != owner) {
            continue;
        }
        if (block.memory.capacity >= bytes && block.memory.lead >= lead) {
            return block.memory.base;
        }
        ArenaMemoryZero(block.memory);
        g_retired.push_back(block);
        block = MapBlock(owner, bytes, lead);
        CheckSomeBlockHigh();
        return block.memory.base;
    }
    g_blocks.push_back(MapBlock(owner, bytes, lead));
    CheckSomeBlockHigh();
    return g_blocks.back().memory.base;
}

void ArenaClearAll() {
    for (const Block &block : g_blocks) {
        ArenaMemoryZero(block.memory);
    }
}

void *PortImagePointer(std::int32_t truncated) {
    static const char anchor = 0;
    if (truncated == 0) {
        return nullptr;
    }
    return reinterpret_cast<void *>(WidenNear(reinterpret_cast<std::uintptr_t>(&anchor), static_cast<std::uint32_t>(truncated)));
}

void ArenaOverflow(const void *arena, int used, int limit) {
    std::fprintf(stderr, "arena %p: allocation overflow, %d of %d quadwords\n", arena, used, limit);
    std::abort();
}
