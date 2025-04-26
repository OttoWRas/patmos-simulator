//
// combined-cache implementations.
//

#include "combined-cache.h"

#include "basic-types.h"
#include "endian-conversion.h"
#include "instr-cache.h"
#include "stack-cache.h"
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

/// Construct a block-based stack method cache.
/// @param memory The memory to spill/fill.
/// @param num_blocks Size of the stack cache in blocks.
combined_cache_t::combined_cache_t(memory_t &memory,
                                   unsigned int num_blocks,
                                   unsigned int num_block_bytes, unsigned int stack_blocks, unsigned int method_blocks,
                                   unsigned int max_active_methods) : ideal_stack_cache_t(memory), Num_blocks(num_blocks),
                                                                      Num_block_bytes(num_block_bytes),
                                                                      s_phase(stack_phase_e::SP_IDLE),
                                                                      m_phase(method_phase_e::MP_IDLE),
                                                                      Memory(memory), Num_blocks_reserved(0),
                                                                      Max_blocks_reserved(0), Num_blocks_spilled(0), Max_blocks_spilled(0),
                                                                      Num_blocks_filled(0), Max_blocks_filled(0), Num_free_empty(0),
                                                                      Num_read_accesses(0), Num_bytes_read(0), Num_write_accesses(0),
                                                                      Num_bytes_written(0), Num_stall_cycles(0), m_allocate_blocks(0), m_method_size(0), m_active_methods(0),
                                                                      m_active_blocks(0), m_blocks_allocated(0),
                                                                      Num_max_blocks_allocated(0), Num_bytes_transferred(0),
                                                                      Num_max_bytes_transferred(0), Num_bytes_fetched(0),
                                                                      Num_max_active_methods(0), Num_transfer_block_bytes(num_block_bytes),
                                                                      Num_hits(0), Num_misses(0), Num_misses_ret(0), Num_evictions_capacity(0),
                                                                      Num_evictions_tag(0), Num_bytes_utilized(0), Num_blocks_freed(0), Max_blocks_freed(0)
{
  Num_max_methods = max_active_methods ? max_active_methods : num_blocks;
  Cache = new byte_t[Num_block_bytes * Num_blocks + 4];
  Buffer = new byte_t[num_blocks * Num_block_bytes];
}

/// free dynamically allocated cache memory.
combined_cache_t::~combined_cache_t()
{
  delete[] Cache;
  delete[] Buffer;
}

bool combined_cache_t::peek_function_size(simulator_t &s,
                                          word_t function_base,
                                          uword_t *result_size)
{
  uword_t num_bytes_big_endian;
  Memory.read_peek(s, function_base - sizeof(uword_t),
                   reinterpret_cast<byte_t *>(&num_bytes_big_endian),
                   sizeof(uword_t), true);
  // convert method size to native endianess and compute size in
  // blocks
  *result_size = from_big_endian<big_uword_t>(num_bytes_big_endian);
  return true;
}

uword_t combined_cache_t::get_num_blocks_for_bytes(uword_t num_bytes)
{
  return ((num_bytes - 1) / Num_block_bytes) + 1;
}

uword_t combined_cache_t::get_transfer_size()
{
  // Memory controller aligns to burst size
  // But we need to transfer the size word as well.
  return m_method_size + 4;
}

uword_t combined_cache_t::get_transfer_start(uword_t address)
{
  return address - 4;
}

/// Initialize the cache before executing the first instruction.
/// @param address Address to fetch initial instructions.
void combined_cache_t::initialize(simulator_t &s, uword_t address)
{
  assert(m_active_blocks == 0 && m_active_methods == 0);

  // get 'most-recent' method of the cache
  method_info_t &current_method = Methods.back();

  // we assume it is an ordinary function entry with size specification
  // (the word before) and copy it in the cache.
  uword_t num_bytes, num_blocks;
  peek_function_size(s, address, &num_bytes);
  num_blocks = get_num_blocks_for_bytes(num_bytes);

  current_method.update(address, num_blocks, num_bytes);
  m_active_blocks = num_blocks;

  m_active_methods = 1;
  Num_max_active_methods = std::max(Num_max_active_methods, 1U);
}

bool combined_cache_t::fetch(simulator_t &s, uword_t base, uword_t address, word_t iw[2])
{
  // fetch from 'most-recent' method of the cache
  return do_fetch(s, Methods[Num_blocks - 1], address, iw);
}

bool combined_cache_t::do_fetch(simulator_t &s, method_info_t &current_method,
                                uword_t address, word_t iw[2])
{
  if (m_phase != MP_IDLE ||
      address < current_method.Address ||
      current_method.Address + current_method.Num_bytes + sizeof(word_t) * NUM_SLOTS * 3 <= address)
  {
    simulation_exception_t::illegal_pc(current_method.Address);
  }

  // get instruction word from the method's instructions
  byte_t *iwp = reinterpret_cast<byte_t *>(&iw[0]);

  // TODO read from Cache buffer, get read position(s) from method_info.

  Memory.read_peek(s, address, iwp, sizeof(word_t) * NUM_SLOTS, true);

  for (unsigned int i = 0; i < NUM_SLOTS; i++)
  {
    unsigned int word = (address - current_method.Address) / sizeof(word_t) + i;
    if (word >= current_method.Num_bytes / sizeof(word_t))
    {
      break;
    }
    current_method.Utilization[word] = true;
  }

  Num_bytes_fetched += sizeof(word_t) * NUM_SLOTS;

  return true;
}

bool combined_cache_t::lookup(simulator_t &s, uword_t address)
{
  return is_available(s, address);
}

bool combined_cache_t::load_method(simulator_t &s, uword_t address, word_t offset)
{
  // check status of the method cache
  switch (m_phase)
  {
  // a new request has to be started.
  case MP_IDLE:
  {
    assert(m_allocate_blocks == 0 && m_method_size == 0);

    if (lookup(s, address))
    {
      // method is in the cache ... done!
      Num_hits++;
      Method_stats[address].Accesses[offset].first++;

      if (s.Dbg_stack.get_stats_options().debug_cache == patmos::DC_ALL &&
          s.Dbg_stack.is_printing())
      {
        // print_hit(s, *s.Dbg_stack.get_stats_options().debug_out, address);
      }
      return true;
    }
    else
    {
      // proceed to next phase ... fetch the size from memory.
      // NOTE: the next phase starts immediately.
      m_phase = MP_SIZE;
      Num_misses++;
      if (offset != 0)
        Num_misses_ret++;
      Method_stats[address].Accesses[offset].second++;
    }
  }

  // the size of the method has to be fetched from memory.
  case MP_SIZE:
  {
    assert(m_allocate_blocks == 0 && m_method_size == 0);

    // get the size of the method that should be loaded
    if (peek_function_size(s, address, &m_method_size))
    {

      m_allocate_blocks = get_num_blocks_for_bytes(m_method_size);

      // TODO should we also store how many bytes are actually transferred
      // by the memory? Ask the Memory for the actual transfer size.
      Method_stats[address].Num_method_bytes = m_method_size;
      Method_stats[address].Num_blocks_allocated = m_allocate_blocks;

      // check method size against cache size.
      if (m_allocate_blocks == 0 || m_allocate_blocks > m_blocks)
      {
        simulation_exception_t::code_exceeded(address);
      }

      uword_t evicted_blocks = 0;
      uword_t evicted_methods = 0;

      assert(m_active_methods > 0);

      // We have to evict a method
      while ((Num_blocks - (m_active_blocks + s_active_blocks)) < m_allocate_blocks)
      {
        update_evict_stats(Methods.front(), address, EVICT_CAPACITY);
        update_evict_stats(Methods.front(), address, EVICT_CAPACITY);
        m_active_blocks -= Methods.front().Num_blocks;
        evicted_blocks += Methods.front().Num_blocks;
        Methods.pop_front();
        m_active_methods--;
        evicted_methods++;
      }

      uword_t blocks_freed = evicted_blocks > m_allocate_blocks ? evicted_blocks - m_allocate_blocks : 0;

      Num_blocks_freed += blocks_freed;
      Max_blocks_freed = std::max(Max_blocks_freed, blocks_freed);

      // update counters
      m_active_methods++;
      Num_max_active_methods = std::max(Num_max_active_methods,
                                        m_active_methods);
      m_active_blocks += m_allocate_blocks;
      m_blocks_allocated += m_allocate_blocks;
      Num_max_blocks_allocated = std::max(Num_max_blocks_allocated,
                                          m_allocate_blocks);
      Num_bytes_transferred += get_transfer_size();
      Num_max_bytes_transferred = std::max(Num_max_bytes_transferred,
                                           get_transfer_size());

      // insert the new entry at the head of the table
      Methods.push_back(method_info_t(address, m_allocate_blocks, m_method_size));

      if (s.Dbg_stack.get_stats_options().debug_cache != patmos::DC_NONE &&
          s.Dbg_stack.is_printing())
      {
        // print_miss(s, *s.Dbg_stack.get_stats_options().debug_out, address,
        //            evicted_methods, evicted_blocks, blocks_freed,
        //            false);
      }

      // proceed to next phase ... the size of the method has been fetched
      // from memory, now transfer the method's instructions.
      // NOTE: the next phase starts immediately.
      m_phase = MP_TRANSFER;
    }
    else
    {
      // keep waiting until the size has been loaded.
      return false;
    }
  }

  // begin transfer from main memory to the method cache.
  case MP_TRANSFER:
  {
    assert(m_allocate_blocks != 0 && m_method_size != 0);

    // TODO implement as actual cache, keep track of where to store
    // methods to in the cache buffer, and keep pointers into the cache in
    // the method_infos.

    if (Memory.read(s, get_transfer_start(address), Cache,
                    get_transfer_size(), true))
    {
      // the transfer is done, go back to IDLE phase
      m_allocate_blocks = m_method_size = 0;
      m_phase = MP_IDLE;
      return true;
    }
    else
    {
      // keep waiting until the transfer is completed.
      return false;
    }
  }
  }

  assert(false);
  abort();
}

/// Check whether a method is in the method cache.
/// @param address The base address of the method.
/// @return True when the method is available in the cache, false otherwise.
bool combined_cache_t::is_available(simulator_t &s, uword_t address)
{
  // check if the address is in the cache
  for (auto &method : Methods)
  {
    if (method.Address == address)
    {
      return true;
    }
  }
  return false;
}

uword_t combined_cache_t::get_active_method_base()
{
  return Methods[Num_blocks - 1].Address;
}

size_t combined_cache_t::get_active_method() const
{
  return Num_blocks - 1;
}

void combined_cache_t::print_stats(patmos::simulator_t const &, std::ostream &, patmos::stats_options_t const &)
{
  return;
}

/// Notify the cache that a cycle passed -- i.e., if there is an ongoing
/// transfer of a method to the cache, advance this transfer by one cycle.
void combined_cache_t::tick(simulator_t &s)
{
  return;
};

word_t combined_cache_t::prepare_reserve(simulator_t &s, uword_t size,
                                         uword_t &stack_spill, uword_t &stack_top)
{
  // convert byte-level size to block size.
  unsigned int size_blocks = size ? (size - 1) / Num_block_bytes + 1 : 0;

  // ensure that the stack cache size is not exceeded
  if (size_blocks > Num_blocks)
  {
    simulation_exception_t::stack_exceeded("Reserving more blocks than"
                                           "the number of blocks in the stack cache");
  }
  if (size_blocks * Num_block_bytes != size)
  {
    simulation_exception_t::stack_exceeded("Reserving a frame size that is not "
                                           "a multiple of the stack block size.");
  }

  if (stack_top < size_blocks * Num_block_bytes)
  {
    simulation_exception_t::stack_exceeded("Stack top pointer decreased beyond "
                                           "lowest possible address.");
  }

  // update stack_top first
  stack_top -= size_blocks * Num_block_bytes;

  uword_t transfer_blocks = 0;

  uword_t reserved_blocks = get_num_reserved_blocks(stack_spill, stack_top);

  // need to spill some blocks?
  if ((s_active_blocks + m_active_blocks) - Num_blocks > reserved_blocks - s_active_blocks)
  {
    // yes? spill some blocks ...
    transfer_blocks = reserved_blocks - ((s_active_blocks + m_active_blocks) - Num_blocks);
  }

  // update the stack top pointer of the processor
  stack_spill -= transfer_blocks * Num_block_bytes;

  // update statistics
  Num_blocks_reserved += size_blocks;
  Max_blocks_reserved = std::max(Max_blocks_reserved, size_blocks);
  Num_blocks_spilled += transfer_blocks;
  Max_blocks_spilled = std::max(Max_blocks_spilled, transfer_blocks);

  return transfer_blocks * Num_block_bytes;
}

word_t combined_cache_t::prepare_free(simulator_t &s, uword_t size,
                                      uword_t &stack_spill, uword_t &stack_top)
{

  // convert byte-level size to block size.
  unsigned int size_blocks = size ? (size - 1) / Num_block_bytes + 1 : 0;
  unsigned int reserved_blocks = get_num_reserved_blocks(stack_spill, stack_top);

  unsigned int freed_spilled_blocks = (size_blocks <= reserved_blocks) ? 0 : size_blocks - reserved_blocks;

  // ensure that the stack cache size is not exceeded
  if (size_blocks > Num_blocks)
  {
    simulation_exception_t::stack_exceeded("Freeing more blocks than"
                                           " the number of blocks in the stack cache");
  }
  if (size_blocks * Num_block_bytes != size)
  {
    simulation_exception_t::stack_exceeded("Freeing a frame size that is not "
                                           "a multiple of the stack block size.");
  }

  // also free space in memory?
  if (freed_spilled_blocks)
  {
    // update the stack top pointer of the processor
    stack_spill += freed_spilled_blocks * Num_block_bytes;
  }

  stack_top += size_blocks * Num_block_bytes;

  // update statistics
  if (stack_top == stack_spill)
  {
    Num_free_empty++;
  }

  // was the transfer alligned?
  uword_t alignment_fixup = stack_spill % Num_transfer_block_bytes;
  if (alignment_fixup != 0)
  {
    // compute actual fixup
    alignment_fixup = Num_transfer_block_bytes - alignment_fixup;

    // round stack spill up to transfer block size
    stack_spill += alignment_fixup;

    // update stats
    Num_words_free_filled += Num_transfer_block_bytes / 4;
    Max_words_free_filled = std::max(Max_words_filled,
                                     Num_transfer_block_bytes / 4);

    return Num_transfer_block_bytes;
  }
  else
  {
    return 0;
  }
}

word_t combined_cache_t::prepare_ensure(simulator_t &s, uword_t size,
                                        uword_t &stack_spill, uword_t &stack_top)
{
  // convert byte-level size to block size.
  unsigned int size_blocks = size ? (size - 1) / Num_block_bytes + 1 : 0;

  uword_t transfer_blocks = 0;

  uword_t reserved_blocks = get_num_reserved_blocks(stack_spill, stack_top);

  // TODO: FIX TO CHECK ACTUAL CACHE
  if (reserved_blocks < size_blocks)
  {
    transfer_blocks = size_blocks - reserved_blocks;
  }

  // update the stack top pointer of the processor
  stack_spill += transfer_blocks * Num_block_bytes;

  // update statistics
  Num_blocks_filled += transfer_blocks;
  Max_blocks_filled = std::max(Max_blocks_filled, transfer_blocks);

  // was the transfer alligned?
  uword_t alignment_fixup = stack_spill % Num_transfer_block_bytes;
  if (alignment_fixup != 0)
  {
    // compute actual fixup
    alignment_fixup = Num_transfer_block_bytes - alignment_fixup;

    // round stack spill up to transfer block size
    stack_spill += alignment_fixup;

    // update stats
    Num_words_filled += alignment_fixup / 4;
    Max_words_filled = std::max(Max_words_filled, alignment_fixup / 4);
  }

  // increment number of blocks to be actually filled
  return transfer_blocks * Num_block_bytes + alignment_fixup;
}

word_t combined_cache_t::prepare_spill(simulator_t &s, uword_t size,
                                       uword_t &stack_spill, uword_t &stack_top)
{
  // convert byte-level size to block size.
  unsigned int size_blocks = size ? (size - 1) / Num_block_bytes + 1 : 0;

  uword_t transfer_blocks = size_blocks;

  if (size_blocks * Num_block_bytes != size)
  {
    simulation_exception_t::stack_exceeded("Spilling a frame size that is not "
                                           "a multiple of the stack block size.");
  }

  // update the stack top pointer of the processor
  stack_spill -= transfer_blocks * Num_block_bytes;

  // update statistics
  Num_blocks_spilled += transfer_blocks;
  Max_blocks_spilled = std::max(Max_blocks_spilled, transfer_blocks);

  return transfer_blocks * Num_block_bytes;
}

bool combined_cache_t::reserve(simulator_t &s, uword_t size, word_t delta,
                               uword_t new_spill, uword_t new_top)
{
  switch (s_phase)
  {
  case SP_IDLE:
  {
    // convert byte-level size to block size.
    unsigned int size_blocks = size ? (size - 1) / Num_block_bytes + 1 : 0;

    // reserve stack space
    Content.resize(Content.size() + size_blocks * Num_block_bytes);

    // need to spill some blocks?
    if (delta > 0)
    {
      unsigned int freed_m_blocks = 0;
      while (s_active_blocks != s_blocks)
      {
        freed_m_blocks += Methods.front().Num_blocks;
        s_active_blocks = s_active_blocks + Methods.front().Num_blocks > Num_blocks
                              ? s_active_blocks + Methods.front().Num_blocks
                              : Num_blocks;

        update_evict_stats(Methods.front(), 0, EVICT_CAPACITY);
        Methods.pop_front();

        m_active_blocks -= Methods.front().Num_blocks;
        m_active_methods--;
      }

      Num_blocks_freed += freed_m_blocks;
      Max_blocks_freed = std::max(Max_blocks_freed, freed_m_blocks);

      for (int i = 0; i < (delta - Num_block_bytes * freed_m_blocks); i++)
      {
        Buffer[delta - i - 1] = Content.front();
        Content.erase(Content.begin());
      }

      // proceed to spill phase ...
      // NOTE: the spill commences immediately
      s_phase = SP_SPILL;
    }
    else
    {
      // Nothing to spill, we are done
      return true;
    }
  }
  case SP_SPILL:
  {
    assert(delta);

    // spill the content of the stack buffer to the memory.
    if (Memory.write(s, new_spill, &Buffer[0], delta))
    {
      // the transfer is done, go back to IDLE phase
      s_phase = SP_IDLE;
      return true;
    }
    else
    {
      // keep waiting until the transfer is completed.
      Num_stall_cycles++;
      return false;
    }
  }
  case SP_FILL:
    // should never be reached
    break;
  }
  // we should not get here.
  assert(false);
  abort();
}

bool combined_cache_t::ensure(simulator_t &s, uword_t size, word_t delta,
                              uword_t new_spill, uword_t new_top)
{
  // do we need to fill?
  if (!delta)
  {
    // no, done.
    return true;
  }

  s_phase = SP_FILL;

  // copy the data from memory into a temporary buffer
  if (Memory.read(s, new_spill - delta, Buffer, delta, false))
  {
    // Ensure the size of the stack cache is larger than the block that needs to
    // be loaded
    uword_t new_size = new_spill > new_top ? new_spill - new_top : 0;
    Content.insert(Content.begin(),
                   new_size > Content.size() ? new_size - Content.size() : 0,
                   0);

    // copy the data back into the stack cache
    for (int i = 0; i < delta; i++)
    {
      assert(delta - i - 1 >= 0);
      assert(delta - i - 1 < (int)Content.size());
      Content[delta - i - 1] = Buffer[i];
    }

    // terminate transfer -- goto IDLE state
    s_phase = SP_IDLE;
    return true;
  }
  else
  {
    // wait until the transfer from the memory is completed.
    Num_stall_cycles++;
    return false;
  }
}

bool combined_cache_t::spill(simulator_t &s, uword_t size, word_t delta,
                             uword_t new_spill, uword_t new_top)
{
  switch (s_phase)
  {
  case SP_IDLE:
  {
    // do we need to spill?
    if (!delta)
    {
      // no, done.
      return true;
    }

    if ((int)Content.size() < delta)
    {
      simulation_exception_t::stack_exceeded("Trying to spill more than the current size of the stack.");
    }

    // copy data to a buffer to allow contiguous transfer to the memory.
    for (int i = 0; i < delta; i++)
    {
      Buffer[delta - i - 1] = Content.front();
      Content.erase(Content.begin());
      s_active_blocks--;
    }

    // proceed to spill phase ...
    // NOTE: the spill commences immediately
    s_phase = SP_SPILL;
  }
  case SP_SPILL:
  {
    assert(delta);

    // spill the content of the stack buffer to the memory.
    if (Memory.write(s, new_spill, &Buffer[0], delta))
    {
      // the transfer is done, go back to IDLE phase
      s_phase = SP_IDLE;
      return true;
    }
    else
    {
      // keep waiting until the transfer is completed.
      Num_stall_cycles++;
      return false;
    }
  }
  case SP_FILL:
    // should never be reached
    break;
  };

  // we should not get here.
  assert(false);
  abort();
}

bool combined_cache_t::free(simulator_t &s, uword_t size,
                            word_t delta, uword_t new_spill,
                            uword_t new_top)
{
  // ensure that a single block is to be filled
  assert(delta == (word_t)Num_transfer_block_bytes);

  // only perform this the first time the function gets called
  if (s_phase == SP_IDLE)
  {
    // first, clear the stack cache content ...
    Content.clear();
  }

  //  ... then execute a one-block fill, if needed ...
  bool retval = ensure(s, delta, delta, new_spill, new_spill - delta);

  // ... then make sure that the content matches the actual number of blocks
  // reserved in the cache.
  if (retval)
  {
    Content.resize(get_num_reserved_blocks(new_spill, new_top) * 4);
    s_active_blocks = get_num_reserved_blocks(new_spill, new_top);
  }

  return retval;
}

bool combined_cache_t::read(simulator_t &s, uword_t address, byte_t *value, uword_t size, bool is_fetch)
{
  // read data
  bool result = ideal_stack_cache_t::read(s, address, value, size, is_fetch);
  assert(result);

  // update statistics
  Num_read_accesses++;
  Num_bytes_read += size;

  return true;
}

bool combined_cache_t::write(simulator_t &s, uword_t address, byte_t *value, uword_t size)
{
  // read data
  bool result = ideal_stack_cache_t::write(s, address, value, size);
  assert(result);

  // update statistics
  Num_write_accesses++;
  Num_bytes_written += size;

  return true;
}

/// Print debug information to an output stream.
/// @param os The output stream to print to.
void combined_cache_t::print(const simulator_t &s, std::ostream &os) {};

/// Print statistics to an output stream.
/// @param os The output stream to print to.
/// @param symbols A mapping of addresses to symbols.
void print_stats(const simulator_t &s, std::ostream &os,
                 const stats_options_t &options);

void combined_cache_t::flush_cache()
{
  if (m_active_methods < 2)
    return;

  uword_t current_base = Methods[Num_blocks - 1].Address;

  for (unsigned int j = Num_blocks - m_active_methods; j < Num_blocks - 1; j++)
  {
    // update_evict_stats(Methods[j], current_base, EVICT_FLUSH);
  }

  m_active_methods = 1;
  m_active_blocks = Methods[Num_blocks - 1].Num_blocks;
}

void combined_cache_t::update_utilization_stats(method_info_t &method,
                                                  uword_t utilized_bytes)
{

  float utilization = (float)utilized_bytes / (float)method.Num_bytes;

  Method_stats[method.Address].Max_utilization = std::max(utilization,
                                                          Method_stats[method.Address].Max_utilization);
  Method_stats[method.Address].Min_utilization = std::min(utilization,
                                                          Method_stats[method.Address].Min_utilization);
}

void combined_cache_t::update_evict_stats(method_info_t &method,
                                            uword_t new_method,
                                            eviction_type_e type)
{
  if (type != EVICT_FLUSH)
  {
    std::pair<unsigned int, unsigned int> &eviction_stats(
        Method_stats[method.Address].Evictions[new_method]);
    if (type == EVICT_TAG)
    {
      eviction_stats.second++;
      Num_evictions_tag++;
    }
    else if (type == EVICT_CAPACITY)
    {
      eviction_stats.first++;
      Num_evictions_capacity++;
    }
  }

  unsigned int utilized_bytes = method.get_utilized_bytes();

  Num_bytes_utilized += utilized_bytes;

  update_utilization_stats(method, utilized_bytes);
}

unsigned int combined_cache_t::method_info_t::get_utilized_bytes() {
  uword_t utilized_bytes = 0;
  for (unsigned int i = 0; i < Utilization.size(); i++) {
    if (Utilization[i]) {
      utilized_bytes += sizeof(uword_t);
    }
  }
  return utilized_bytes;
}

void combined_cache_t::reset_stats() {};