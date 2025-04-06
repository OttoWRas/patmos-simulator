//
// combined-cache implementations.
//

#include "combined-cache.h"

#include "basic-types.h"
#include "endian-conversion.h"
#include "instr-cache.h"
#include "simulation-core.h"
#include "symbol.h"

#include <cassert>
#include <cmath>
#include <map>
#include <ostream>
#include <limits>

#include <boost/format.hpp>
#include "exception.h"

using namespace patmos;

void combined_cache_t::initialize(simulator_t &s, uword_t address) {
    current_base = address;
};
// Method cache interface
bool combined_cache_t::fetch(simulator_t &s, uword_t base, uword_t address, word_t iw[NUM_SLOTS]) {
    combined_memory.read_peek(s, address, reinterpret_cast<byte_t*>(&iw[0]),
                     sizeof(word_t)*NUM_SLOTS, true);
    return true;
};
bool combined_cache_t::load_method(simulator_t &s, uword_t address, word_t offset) {
    current_base = address;
    return true;
};

bool combined_cache_t::is_available(simulator_t &s, uword_t address) {
    return true;
};

// Data cache interface
bool combined_cache_t::read(simulator_t &s, uword_t address, byte_t *value, uword_t size, bool is_fetch) {

};
bool combined_cache_t::write(simulator_t &s, uword_t address, byte_t *value, uword_t size) {

};

void combined_cache_t::tick(simulator_t &s) {
    if (Phase != IDLE) Num_stall_cycles++;
};
void combined_cache_t::print(const simulator_t &s, std::ostream &os) {
    os << boost::format(" #M: %1$02d #B: %2$02d\n") % Num_active_methods % Num_active_blocks;

    for(int i = Num_blocks-1; i >= (int)(Num_blocks-Num_active_methods); i--){
        os << boost::format("   M%1$02d: 0x%2$08x (%3$8d Blk %4$8d b)\n") % (Num_blocks-i) % Methods[i].Address % Methods[i].Num_blocks % Methods[i].Num_bytes;
    }
    os << '\n';
}
void combined_cache_t::print_stats(const simulator_t &s, std::ostream &os, const stats_options_t& options) {
                        
};
void combined_cache_t::reset_stats() {
    
};
void combined_cache_t::flush_cache() {

};
