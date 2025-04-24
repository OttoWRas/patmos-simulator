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

    class combined_cache_t : public ideal_stack_cache_t, public instr_cache_t
    {
    protected:
        /// Possible transfers to/from the stack cache.
        enum stack_phase_e
        {
            /// No transfer ongoing.
            SP_IDLE,
            /// Data is transferred from the stack cache to the memory.
            SP_SPILL,
            /// Data is transferred from the memory to the stack cache.
            SP_FILL
        };

        enum method_phase_e
        {
          /// The method cache is idle and available to handle requests.
          MP_IDLE,
          /// The method cache is on the way of fetching the size of the method
          /// from memory.
          MP_SIZE,
          /// The instructions of the method are being transferred from memory.
          MP_TRANSFER
        };

        typedef unsigned int method_tag_t; 

        class combined_ptr_t
        {
        private:
            unsigned int ptr;
            unsigned int max;

        public:
            
            combined_ptr_t(unsigned int ptr, unsigned int max) 
                : ptr(ptr), max(0) {};

            combined_ptr_t& operator--(int) {
                ptr = (ptr == 0) ? max - 1 : ptr - 1;
                return *this;
            }

            combined_ptr_t operator-(const int &rhs) const {
                int diff = ptr - rhs;
                if (diff < 0) {
                    diff += max;
                }
                return combined_ptr_t(diff, max);
            }

            int operator*() const { return ptr; }
            operator int() const { return ptr; }

            combined_ptr_t& operator++(int) {
                ptr = (ptr + 1) % max;
                return *this;
            };

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

            unsigned int method_tag; 

            bool Fully_in_cache;

            std::vector<bool> Utilization;

            /// Construct a method lru info object. All data is initialized to zero.
            /// @param instructions Pointer to the method's instructions.
            method_info_t()
                : Address(0), Num_blocks(0), Num_bytes(0)
            {
            }

            method_info_t(uword_t address, uword_t blocks, uword_t bytes, method_tag_t tag)
            : Address(address), Num_blocks(blocks), Num_bytes(bytes), 
              Fully_in_cache(true), method_tag(tag)
            {}

            /// Update the internal data of the method lru info entry.
            /// @param address The new address of the entry.
            /// @param num_blocks The number of blocks occupied in the method cache.
            /// @param num_bytes The number of valid instruction bytes of the method.
            void update(uword_t address, uword_t num_blocks,
                        uword_t num_bytes) 
                        {
                            Address = address;
                            Num_blocks = num_blocks;
                            Num_bytes = num_bytes;
                        }

            void reset_utilization();

            unsigned int get_utilized_bytes();
        };

        class combined_block_info 
        {
        public:
            bool         Is_empty;
            bool         Is_method;
            uword_t      Address;
            method_tag_t method_tag;

            combined_block_info()
                : Is_empty(true), Is_method(false), Address(0), method_tag(0)
            {
            }

            void add_method_tag (method_tag_t &tag)
            {
                method_tag  = tag;
                Is_method   = true;
                Is_empty    = false;
            }

            void add_address (uword_t address) 
            {
                Address = address;
                Is_method = false;
                Is_empty  = false;
            }

            void empty() {
                Address   = 0;
                Is_method = false;
                Is_empty  = true;
            }
        };

        /// Map addresses to cache statistics of individual methods.
        typedef std::map<word_t, method_stats_info_t> method_stats_t;

        /// Size of the stack cache in blocks.
        unsigned int Num_blocks;

        /// Size of blocks in bytes.
        unsigned int Num_block_bytes;
        
        /// Maximum number of active functions allowed in the cache.
        unsigned int Num_max_methods;

        /// The stack cache pointer.
        combined_ptr_t stack_ptr;

        unsigned int  stack_top_ptr;
        unsigned int  stack_spill_ptr;

        unsigned int Num_method_blocks;

        combined_ptr_t method_ptr;

        unsigned int Num_stack_blocks;

        unsigned int Method_tag_head;

        /// Store currently ongoing transfer.
        stack_phase_e stack_phase;

        /// Store currently ongoing transfer.
        method_phase_e method_phase;

        /// The memory to spill/fill.
        memory_t &Memory;

        /// Temporary buffer used during spill/fill.
        byte_t *Buffer;

        /// Number of blocks of the currently pending transfer, if any.
        uword_t Num_allocate_blocks;

        /// Number of bytes of the currently pending transfer, if any.
        uword_t Num_method_size;

        /// The methods in the cache sorted by age.
        std::vector<method_info_t> Methods;

        /// Cache buffer.
        combined_block_info *Cache;

        /// Fake cache
        byte_t *Fake_cache;

        // *************************************************************************
        // statistics

        /// Total number of blocks reserved.
        unsigned int Num_blocks_reserved;
        /// Maximal number of blocks reserved at once.
        unsigned int Max_blocks_reserved;
        /// Total number of blocks transferred to main (spill) memory.
        unsigned int Num_blocks_spilled;
        /// Maximal number of blocks transferred to main at once (spill) memory.
        unsigned int Max_blocks_spilled;
        /// Total number of blocks transferred from main (fill) memory.
        unsigned int Num_blocks_filled;
        /// Maximal number of blocks transferred from main at once (fill) memory.
        unsigned int Max_blocks_filled;
        /// Number of executed free instructions resulting in an entirely empty
        /// stack cache.
        unsigned int Num_free_empty;
        /// Number of read accesses to the stack cache.
        unsigned int Num_read_accesses;
        /// Number of bytes read from the stack cache.
        unsigned int Num_bytes_read;

        /// Number of write accesses to the stack cache.
        unsigned int Num_write_accesses;

        /// Number of bytes written to the stack cache.
        unsigned int Num_bytes_written;

        /// Number of stall cycles caused by method cache misses.
        unsigned int Num_stall_cycles;

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

        /// Number of bytes used in evicted methods.
        unsigned int Num_bytes_utilized;

        /// Total Number of blocks evicted but not immediately allocated.
        unsigned int Num_blocks_freed;

        /// Maximum number of blocks evicted but not immediately allocated.
        unsigned int Max_blocks_freed;

        /// Cache statistics of individual method.
        method_stats_t Method_stats;

        /// A simulated instruction fetch from the method cache.
        /// @param current_method The currently active method.
        /// @param address The memory address to fetch from.
        /// @param iw A pointer to store the fetched instruction word.
        /// @return True when the instruction word is available from the read port.
        bool do_fetch(simulator_t &s, method_info_t &current_method, uword_t address, word_t iw[2]);

        /// Check whether the method at the given address is in the method cache.
        /// @param address The method address.
        /// @return True in case the method is in the cache, false otherwise.
        virtual bool lookup(simulator_t &s, uword_t address);

        method_tag_t  get_next_method_tag() {
            auto n = Method_tag_head;
            Method_tag_head++;
            return n;
        }

        void update_utilization_stats(method_info_t &method, uword_t utilized_bytes);

        virtual size_t get_active_method() const;

        void print_cache_state(simulator_t &s, std::ostream &dout,
                               size_t active_method) const;

        void print_hit(simulator_t &s, std::ostream &dout, uword_t address) const;

        void print_miss(simulator_t &s, std::ostream &dout, uword_t address,
                        uword_t evicted_methods, uword_t evicted_blocks,
                        uword_t blocks_freed, bool capacity_miss) const;

        enum eviction_type_e
        {
            EVICT_CAPACITY,
            EVICT_TAG,
            EVICT_FLUSH
        };

        /// Evict a given method, updating the cache state, and various statics.
        /// Also updates the utilization stats.
        /// @param method The method to be evicted.
        /// @param new_method the address of the new method causing the eviction
        /// @param capacity_miss true if the method is evicted due to a capacity miss
        void update_evict_stats(method_info_t &method, uword_t new_method,
                                eviction_type_e type);

        bool read_function_size(simulator_t &s, word_t function_base, uword_t *result_size);

        bool peek_function_size(simulator_t &s, word_t function_base, uword_t *result_size);

        uword_t get_num_blocks_for_bytes(uword_t num_bytes);

        uword_t get_transfer_start(uword_t address);

        uword_t get_transfer_size();

        /// Return the number of blocks currently reserved.
        inline unsigned int get_num_reserved_blocks(uword_t spill, uword_t top) const
        {
            return (spill - top) / Num_block_bytes;
        }

    public:
        /// Construct a block-based stack method cache.
        /// @param memory The memory to spill/fill.
        /// @param num_blocks Size of the stack cache in blocks.
        combined_cache_t(memory_t &memory, unsigned int num_blocks, unsigned int num_block_bytes, 
            unsigned int stack_blocks, unsigned int method_blocks, unsigned int max_active_methods = 0);

        virtual ~combined_cache_t();

        /// Initialize the cache before executing the first instruction.
        /// @param address Address to fetch initial instructions.
        virtual void initialize(simulator_t &s, uword_t address);

        /// A simulated instruction fetch from the method cache.
        /// @param base The current method's base address.
        /// @param address The memory address to fetch from.
        /// @param iw A pointer to store the fetched instruction word.
        /// @return True when the instruction word is available from the read port.
        virtual bool fetch(simulator_t &s, uword_t base, uword_t address, word_t iw[2]);

        /// Assert that the method is in the method cache.
        /// If it is not available yet, initiate a transfer,
        /// evicting other methods if needed.
        /// @param address The base address of the method.
        /// @param offset Offset within the method where execution should continue.
        /// @return True when the method is available in the cache, false otherwise.
        virtual bool load_method(simulator_t &s, uword_t address, word_t offset);

        /// Check whether a method is in the method cache.
        /// @param address The base address of the method.
        /// @return True when the method is available in the cache, false otherwise.
        virtual bool is_available(simulator_t &s, uword_t address);

        virtual uword_t get_active_method_base();

        /// Notify the cache that a cycle passed -- i.e., if there is an ongoing
        /// transfer of a method to the cache, advance this transfer by one cycle.
        virtual void tick(simulator_t &s);

        virtual word_t prepare_reserve(simulator_t &s, uword_t size,
                                       uword_t &stack_spill, uword_t &stack_top);

        virtual word_t prepare_free(simulator_t &s, uword_t size,
                                    uword_t &stack_spill, uword_t &stack_top);

        virtual word_t prepare_ensure(simulator_t &s, uword_t size,
                                      uword_t &stack_spill, uword_t &stack_top);

        virtual word_t prepare_spill(simulator_t &s, uword_t size,
                                     uword_t &stack_spill, uword_t &stack_top);

        virtual bool reserve(simulator_t &s, uword_t size, word_t delta,
                             uword_t new_spill, uword_t new_top);

        virtual bool free(simulator_t &s, uword_t size, word_t delta,
                          uword_t new_spill, uword_t new_top);

        virtual bool ensure(simulator_t &s, uword_t size, word_t delta,
                            uword_t new_spill, uword_t new_top);

        virtual bool spill(simulator_t &s, uword_t size, word_t delta,
                           uword_t new_spill, uword_t new_top);

        virtual bool read(simulator_t &s, uword_t address, byte_t *value, uword_t size, bool is_fetch);

        virtual bool write(simulator_t &s, uword_t address, byte_t *value, uword_t size);

        /// Print debug information to an output stream.
        /// @param os The output stream to print to.
        virtual void print(const simulator_t &s, std::ostream &os);

        /// Print statistics to an output stream.
        /// @param os The output stream to print to.
        /// @param symbols A mapping of addresses to symbols.
        virtual void print_stats(const simulator_t &s, std::ostream &os,
                                 const stats_options_t &options);

        virtual void flush_cache();

        virtual void reset_stats();
    };
};
#endif // PATMOS_COMBINED_CACHE_H