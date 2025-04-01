#ifndef PATMOS_COMBINED_CACHE_H
#define PATMOS_COMBINED_CACHE_H

#include "basic-types.h"
#include "instr-cache.h"
#include "data-cache.h"
#include "simulation-core.h"
#include "symbol.h"
#include "method-cache.h"

#include <map>
#include <limits>

namespace patmos
{
    class combined_cache_t : public instr_cache_t, public data_cache_t
    {
    protected:
        /// Phases of fetching a method from memory.
        enum phase_e
        {
            /// The method cache is idle and available to handle requests.
            IDLE,
            /// The method cache is on the way of fetching the size of the method
            /// from memory.
            SIZE,
            /// The instructions of the method are being transferred from memory.
            TRANSFER
        };

        /// Bookkeeping information on methods in the cache.
        class method_info_t
        {
        public:
            /// The address of the method.
            uword_t Address;

            /// The number of blocks occupied by the method.
            uword_t Num_blocks;

            /// The size of the method in bytes.
            uword_t Num_bytes;

            std::vector<bool> Utilization;

            /// Construct a method lru info object. All data is initialized to zero.
            /// @param instructions Pointer to the method's instructions.
            method_info_t()
                : Address(0), Num_blocks(0), Num_bytes(0)
            {
            }

            /// Update the internal data of the method lru info entry.
            /// @param address The new address of the entry.
            /// @param num_blocks The number of blocks occupied in the method cache.
            /// @param num_bytes The number of valid instruction bytes of the method.
            void update(uword_t address, uword_t num_blocks,
                        uword_t num_bytes);

            void reset_utilization();

            unsigned int get_utilized_bytes();
        };

        /// Map addresses to cache statistics of individual methods.
        typedef std::map<word_t, method_stats_info_t> method_stats_t;

        /// The backing memory to fetch instructions from.
        memory_t &Memory;

        /// Number of blocks in the method cache.
        unsigned int Num_blocks;

        /// Number of bytes in a block.
        unsigned int Num_block_bytes;

        /// Maximum number of active functions allowed in the cache.
        unsigned int Num_max_methods;

        /// Currently active phase to fetch a method from memory.
        phase_e Phase;

        /// Number of blocks of the currently pending transfer, if any.
        uword_t Num_allocate_blocks;

        /// Number of bytes of the currently pending transfer, if any.
        uword_t Num_method_size;

        /// The methods in the cache sorted by age.
        method_info_t *Methods;

        /// Cache buffer.
        byte_t *Cache;

        /// The number of methods currently in the cache.
        unsigned int Num_active_methods;

        /// The sum of sizes of all method entries currently active in the cache.
        unsigned int Num_active_blocks;

        /// Number of blocks transferred from the main memory.
        unsigned int Num_blocks_allocated;

        /// Largest number of blocks transferred from the main memory for a single
        /// method.
        unsigned int Num_max_blocks_allocated;

        /// Number of bytes transferred from the main memory.
        unsigned int Num_bytes_transferred;

        /// Largest number of bytes transferred from the main memory for a single
        /// method.
        unsigned int Num_max_bytes_transferred;

        /// Number of bytes fetched from the cache.
        unsigned int Num_bytes_fetched;

        /// Maximum number of methods allocated in the cache.
        unsigned int Num_max_active_methods;

        /// Number of cache hits.
        unsigned int Num_hits;

        /// Number of cache misses.
        unsigned int Num_misses;

        /// Number of cache misses on returns.
        unsigned int Num_misses_ret;

        /// Number of cache evictions due to limited cache capacity.
        unsigned int Num_evictions_capacity;

        /// Number of cache evictions due to the limited number of tags.
        unsigned int Num_evictions_tag;

        /// Number of stall cycles caused by method cache misses.
        unsigned int Num_stall_cycles;

        /// Number of bytes used in evicted methods.
        unsigned int Num_bytes_utilized;

        /// Total Number of blocks evicted but not immediately allocated.
        unsigned int Num_blocks_freed;

        /// Maximum number of blocks evicted but not immediately allocated.
        unsigned int Max_blocks_freed;

        /// Cache statistics of individual method.
        method_stats_t Method_stats;

        /// Check whether the method at the given address is in the method cache.
        /// @param current_method The currently active method.
        /// @param address The method address.
        /// @param iw A pointer to store the fetched instruction word.
        /// @return True in case the method is in the cache, false otherwise.
        bool do_fetch(simulator_t &s, method_info_t &current_method,
                     uword_t address, word_t iw[NUM_SLOTS]);

        

    public:
        void initialize(simulator_t &s, uword_t address) override;
        // Method cache interface
        bool fetch(simulator_t &s, uword_t base, uword_t address, word_t iw[NUM_SLOTS]) override;
        bool load_method(simulator_t &s, uword_t address, word_t offset) override;

        bool is_available(simulator_t &s, uword_t address) override;

        // Data cache interface
        bool read(simulator_t &s, uword_t address, byte_t *value, uword_t size, bool is_fetch) override;
        bool write(simulator_t &s, uword_t address, byte_t *value, uword_t size) override;

        void tick(simulator_t &s) override;
        void print(const simulator_t &s, std::ostream &os) override;
        void print_stats(const simulator_t &s, std::ostream &os,
                         const stats_options_t &options) override;
        void reset_stats() override;
        void flush_cache() override;
    };
};

#endif // PATMOS_COMBINED_CACHE_Hmemory_t &memory