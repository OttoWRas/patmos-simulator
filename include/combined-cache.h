#ifndef PATMOS_COMBINED_CACHE_H
#define PATMOS_COMBINED_CACHE_H

#include "basic-types.h"
#include "instr-cache.h"
#include "data-cache.h"
#include "simulation-core.h"
#include "symbol.h"

#include <map>
#include <limits>


namespace patmos
{
    class combined_cache_t : public instr_cache_t, public data_cache_t
    {
    private:
        /// The backing memory to fetch instructions from.
        memory_t &CombinedMemory;
    
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
                         const stats_options_t& options) override;
        void reset_stats() override;
        void flush_cache() override;
    };
};

#endif // PATMOS_COMBINED_CACHE_Hmemory_t &memory