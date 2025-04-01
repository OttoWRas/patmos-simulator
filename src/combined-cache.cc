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

void initialize(simulator_t &s, uword_t address) {

};
// Method cache interface
bool fetch(simulator_t &s, uword_t base, uword_t address, word_t iw[NUM_SLOTS]) {

};
bool load_method(simulator_t &s, uword_t address, word_t offset) {

};

bool is_available(simulator_t &s, uword_t address) {

};

// Data cache interface
bool read(simulator_t &s, uword_t address, byte_t *value, uword_t size, bool is_fetch) {

};
bool write(simulator_t &s, uword_t address, byte_t *value, uword_t size) {

};

void tick(simulator_t &s) {

};
void print(const simulator_t &s, std::ostream &os) {
    
};
void print_stats(const simulator_t &s, std::ostream &os,
                 const stats_options_t& options) {
                    
                 };
void reset_stats() {
    
};
void flush_cache() {
    
};
