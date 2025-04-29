#ifndef PATMOS_COMBINED_CACHE_H
#define PATMOS_COMBINED_CACHE_H

#include "basic-types.h"
#include "instr-cache.h"
#include "data-cache.h"
#include "simulation-core.h"
#include "symbol.h"
#include "method-cache.h"
#include "stack-cache.h"
#include "memory.h"

#include <map>
#include <limits>
#include <vector>
#include <deque>
#include <ostream>

namespace patmos
{

    /// Cache statistics for a particular method and return offset. Map offsets
    /// to number of cache hits/misses.
    typedef std::map<word_t, std::pair<unsigned int, unsigned int>>
        offset_stats_t;

    // Cache statistics to keep track how often a given method was evicted by
    // other methods due to the limited cache capacity (first) or limited number
    // of tags (second).
    typedef std::map<word_t, std::pair<unsigned int, unsigned int>>
        eviction_stats_t;

    class combined_cache_t : public fifo_method_cache_t, public block_aligned_stack_cache_t
    {

    private:
        unsigned int Num_total_blocks; ///< Number of blocks in the cache.
        unsigned int Num_stack_blocks; ///< Number of blocks in the stack cache.
        unsigned int Num_shared_blocks; ///< Number of blocks in the stack cache.

    public:
        /// Construct a block-based stack method cache.
        /// @param memory The memory to spill/fill.
        /// @param num_blocks Size of the stack cache in blocks.
        combined_cache_t(memory_t &memory, unsigned int num_blocks, unsigned int num_block_bytes,
                         unsigned int stack_blocks, unsigned int method_blocks, unsigned int max_active_methods = 0);

        virtual ~combined_cache_t();

        /// Assert that the method is in the method cache.
        virtual bool load_method(simulator_t &s, uword_t address, word_t offset);

        virtual bool reserve(simulator_t &s, uword_t size, word_t delta,
                             uword_t new_spill, uword_t new_top);

        virtual bool free(simulator_t &s, uword_t size, word_t delta,
                            uword_t new_spill, uword_t new_top);

        virtual bool ensure(simulator_t &s, uword_t size, word_t delta,
                            uword_t new_spill, uword_t new_top);

        void free_method(simulator_t &s, uword_t address);

        virtual void flush_cache();

        virtual void reset_stats();
    };
};
#endif // PATMOS_COMBINED_CACHE_H
