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
#include <map>
#include <ostream>

namespace patmos
{
    // Pure virtual class to help with the implementation of combined cache
    class fifo_method_helper_t : public fifo_method_cache_t {
        
    public:
        /// Construct a FIFO-based method cache.
        /// @param memory The memory to fetch instructions from on a cache miss.
        /// @param num_blocks The size of the cache in blocks.
        /// @param num_block_bytes The size of a single block in bytes
        /// @param max_active_methods The max number of active methods
        fifo_method_helper_t(memory_t &memory, unsigned int num_blocks,
            unsigned int num_block_bytes, unsigned int max_active_methods = 0)
            : fifo_method_cache_t(memory, num_blocks, num_block_bytes, max_active_methods) {}

        virtual void print_stats_method(const simulator_t &s, std::ostream &os,
            const stats_options_t& options) = 0; 

        void print_stats(const simulator_t &s, std::ostream &os,
            const stats_options_t& options) override {
                print_stats_method(s, os, options);
            }
    };

    /// Cache statistics for a particular method and return offset. Map offsets
    /// to number of cache hits/misses.
    typedef std::map<word_t, std::pair<unsigned int, unsigned int>>
        offset_stats_t;

    class combined_cache_t : public fifo_method_helper_t, public block_aligned_stack_cache_t
    {

    private:
        unsigned int Num_total_blocks;  ///< Number of blocks in the cache.
        unsigned int Num_stack_blocks;  ///< Number of blocks in the stack cache.
        unsigned int Num_shared_blocks; ///< Number of blocks in the stack cache.

        unsigned int Num_stack_evicts;

        typedef std::map<word_t, unsigned int> stack_evict_stats_t;

    public:
        stack_evict_stats_t Stack_evict_stats; ///< Map addresses to cache statistics of individual methods.

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

        virtual void free_method(simulator_t &s, uword_t address);

        virtual void flush_cache();

        virtual void reset_stats();

        void print_stats_method(const simulator_t &s, std::ostream &os,
            const stats_options_t& options);
    };
};
#endif // PATMOS_COMBINED_CACHE_H
