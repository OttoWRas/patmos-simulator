#include "combined-cache.h"
#include "method-cache.h"
#include "stack-cache.h"
#include "memory.h"

#include <map>
#include <limits>
#include <vector>

namespace patmos
{
  combined_cache_t::combined_cache_t(memory_t &memory, unsigned int num_blocks, unsigned int num_block_bytes,
                                     unsigned int stack_blocks, unsigned int method_blocks, unsigned int max_active_methods)
      : fifo_method_cache_t(memory, method_blocks, num_block_bytes, max_active_methods),
        block_aligned_stack_cache_t(memory, stack_blocks, num_block_bytes),
        Num_total_blocks(num_blocks),
        Num_shared_blocks(num_blocks)
  {
    // Initialize any additional members or logic specific to combined_cache_t here
  }
  combined_cache_t::~combined_cache_t()
  {
    // Clean up any resources if necessary
  }
  bool combined_cache_t::load_method(simulator_t &s, uword_t address, word_t offset)
  {
    //fifo_method_cache_t::Num_blocks = Num_total_blocks - Num_stack_blocks;
    // Implement the logic to load a method into the cache
    return fifo_method_cache_t::load_method(s, address, offset);
  }

  bool combined_cache_t::reserve(simulator_t &s, uword_t size, word_t delta,
                                 uword_t new_spill, uword_t new_top)
  {
    // Implement the logic to reserve space in the cache
    if (delta > 0 && (delta + fifo_method_cache_t::Num_active_blocks + Num_stack_blocks) > Num_total_blocks)
    {
      free_method(s, delta + fifo_method_cache_t::Num_active_blocks + Num_stack_blocks);
      Num_stack_blocks = size ? (size - 1) / block_aligned_stack_cache_t::Num_block_bytes + 1 : 0;
    }

    return block_stack_cache_t::reserve(s, size, delta, new_spill, new_top);
  }

  bool combined_cache_t::free(simulator_t &s, uword_t size, word_t delta,
                              uword_t new_spill, uword_t new_top)
  {
    Num_stack_blocks = size ? (size - 1) / block_aligned_stack_cache_t::Num_block_bytes + 1 : 0;
    return block_stack_cache_t::ensure(s, size, delta, new_spill, new_top);
  }

  bool combined_cache_t::ensure(simulator_t &s, uword_t size, word_t delta,
                                uword_t new_spill, uword_t new_top)
  {
    // Implement the logic to ensure space in the cache
    if (delta > 0 && (fifo_method_cache_t::Num_active_blocks + block_aligned_stack_cache_t::Num_blocks_reserved) > Num_total_blocks)
    {
      free_method(s, delta + fifo_method_cache_t::Num_active_blocks + Num_stack_blocks);
      Num_stack_blocks = size ? (size - 1) / block_aligned_stack_cache_t::Num_block_bytes + 1 : 0;
    }
    return block_stack_cache_t::ensure(s, size, delta, new_spill, new_top);
  }

  void combined_cache_t::free_method(simulator_t &s, uword_t size)
  {
    uword_t free_blocks = get_num_blocks_for_bytes(size);

    uword_t evicted_blocks = 0;
    uword_t evicted_methods = 0;
    // throw other entries out of the cache if needed
    while (Num_active_blocks - free_blocks > fifo_method_cache_t::Num_blocks ||
           Num_active_methods >= Num_max_methods)
    {
      assert(Num_active_methods > 0);
      method_info_t &method(Methods[fifo_method_cache_t::Num_blocks - Num_active_methods]);

      // update eviction statistics
      evicted_blocks += method.Num_blocks;
      evicted_methods++;

      update_evict_stats(method, 0, EVICT_CAPACITY);

      // evict the method from the cache
      Num_active_blocks -= method.Num_blocks;
      Num_active_methods--;
    }

    uword_t blocks_freed = evicted_blocks > Num_allocate_blocks ? evicted_blocks - Num_allocate_blocks : 0;

    Num_blocks_freed += blocks_freed;
    Max_blocks_freed = std::max(Max_blocks_freed, blocks_freed);

    // shift the remaining blocks
    for (unsigned int j = fifo_method_cache_t::Num_blocks - Num_active_methods;
         j < fifo_method_cache_t::Num_blocks - 1; j++)
    {
      Methods[j] = Methods[j + 1];
    }
    // Implement the logic to free a method from the cache
    printf("Freeing method at address %u\n", size);
  }
  void combined_cache_t::flush_cache()
  {
    // Implement the logic to flush the cache
    fifo_method_cache_t::flush_cache();
  }

  void combined_cache_t::reset_stats()
  {
    fifo_method_cache_t::reset_stats();
    block_stack_cache_t::reset_stats();
  }
}
