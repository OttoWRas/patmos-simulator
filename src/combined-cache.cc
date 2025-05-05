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
      : fifo_method_helper_t(memory, method_blocks, num_block_bytes, max_active_methods),
        block_aligned_stack_cache_t(memory, stack_blocks, num_block_bytes),
        Num_total_blocks(num_blocks), Num_stack_blocks(0),
        Num_shared_blocks(num_blocks - (method_blocks + stack_blocks))
  {
    // Initialize any additional members or logic specific to combined_cache_t here
  }
  combined_cache_t::~combined_cache_t()
  {
    // Clean up any resources if necessary
  }
  bool combined_cache_t::load_method(simulator_t &s, uword_t address, word_t offset)
  {
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
      Num_active_blocks += delta ? (delta - 1) / block_aligned_stack_cache_t::Num_block_bytes + 1 : 0;
    }

    return block_stack_cache_t::reserve(s, size, delta, new_spill, new_top);
  }

  bool combined_cache_t::free(simulator_t &s, uword_t size, word_t delta,
                              uword_t new_spill, uword_t new_top)
  {
    Num_active_blocks += delta ? (delta - 1) / block_aligned_stack_cache_t::Num_block_bytes + 1 : 0;
    return block_stack_cache_t::free(s, size, delta, new_spill, new_top);
  }

  bool combined_cache_t::ensure(simulator_t &s, uword_t size, word_t delta,
                                uword_t new_spill, uword_t new_top)
  {
    // Implement the logic to ensure space in the cache
    if (delta > 0 && (fifo_method_cache_t::Num_active_blocks + block_aligned_stack_cache_t::Num_blocks_reserved) > Num_total_blocks)
    {
      free_method(s, delta + fifo_method_cache_t::Num_active_blocks + Num_stack_blocks);
      Num_active_blocks += delta ? (delta - 1) / block_aligned_stack_cache_t::Num_block_bytes + 1 : 0;
    }
    return block_stack_cache_t::ensure(s, size, delta, new_spill, new_top);
  }

  void combined_cache_t::free_method(simulator_t &s, uword_t size)
  {
    // printf("Freeing method at address %u\n", size);
    //  Implement the logic to free a method from the cache
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

      Stack_evict_stats[method.Address] += method.Num_blocks;

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

  void combined_cache_t::print_stats_method(const simulator_t &s, std::ostream &os,
                                       const stats_options_t &options)
  {
    uword_t bytes_utilized = Num_bytes_utilized;
    for (unsigned int j = fifo_method_cache_t::Num_blocks - Num_active_methods; j < fifo_method_cache_t::Num_blocks; j++)
    {
      uword_t ub = Methods[j].get_utilized_bytes();

      bytes_utilized += ub;

      update_utilization_stats(Methods[j], ub);
    }

    // per cache miss, we load the size word, but do not store it in the cache
    uword_t bytes_allocated = Num_bytes_transferred - Num_misses * 4;
    // Utilization = Bytes used / bytes allocated in cache
    float utilization = (float)bytes_utilized /
                        (float)(Num_blocks_allocated * fifo_method_cache_t::Num_block_bytes);
    // Internal fragmentation = Bytes loaded to cache / Bytes allocated in cache
    float fragmentation = 1.0 - (float)bytes_allocated /
                                    (float)(Num_blocks_allocated * fifo_method_cache_t::Num_block_bytes);

    // External fragmentation = Blocks evicted but not allocated / Blocks allocated
    float ext_fragmentation = (float)Num_blocks_freed /
                              (float)Num_blocks_allocated;

    // Ratio of bytes loaded from main memory to bytes fetched from the cache.
    float transfer_ratio = (float)Num_bytes_transferred / (float)Num_bytes_fetched;

    // instruction statistics
    os << boost::format("                              total        max.\n"
                        "   Blocks Allocated    : %1$10d  %2$10d\n"
                        "   Bytes Transferred   : %3$10d  %4$10d\n"
                        "   Bytes Allocated     : %5$10d  %6$10d\n"
                        "   Bytes Used          : %7$10d\n"
                        "   Block Utilization   : %8$10.2f%%\n"
                        "   Int. Fragmentation  : %9$10.2f%%\n"
                        "   Bytes Freed         : %10$10d  %11$10d\n"
                        "   Ext. Fragmentation  : %12$10.2f%%\n"
                        "   Max Methods in Cache: %13$10d\n"
                        "   Cache Hits          : %14$10d  %15$10.2f%%\n"
                        "   Cache Misses        : %16$10d  %17$10.2f%%\n"
                        "   Cache Misses Returns: %18$10d  %19$10.2f%%\n"
                        "   Evictions Capacity  : %20$10d  %21$10.2f%%\n"
                        "   Evictions Tag       : %22$10d  %23$10.2f%%\n"
                        "   Evictions Stack     : %24$10d  %25$10.2f%%\n"
                        "   Transfer Ratio      : %26$10.3f\n"
                        "   Miss Stall Cycles   : %27$10d  %28$10.2f%%\n\n")
      % Num_blocks_allocated % Num_max_blocks_allocated
      % Num_bytes_transferred % Num_max_bytes_transferred
      % bytes_allocated % (Num_max_bytes_transferred - 4)
      % bytes_utilized % (utilization * 100.0) % (fragmentation * 100.0)
      % (Num_blocks_freed * fifo_method_cache_t::Num_block_bytes)
      % (Max_blocks_freed * fifo_method_cache_t::Num_block_bytes)
      % (ext_fragmentation * 100.0)
      % Num_max_active_methods
      % Num_hits % (100.0 * Num_hits / (Num_hits + Num_misses))
      % Num_misses % (100.0 * Num_misses / (Num_hits + Num_misses))
      % Num_misses_ret % (100.0 * Num_misses_ret / Num_misses)
      % Num_evictions_capacity
      % (100.0*Num_evictions_capacity / (Num_evictions_capacity + Num_evictions_tag + Num_stack_evicts))
      % Num_evictions_tag
      % (100.0 * Num_evictions_tag / (Num_evictions_capacity + Num_evictions_tag + Num_stack_evicts))
      % Num_stack_evicts
      % (100.0 * Num_stack_evicts / (Num_evictions_capacity + Num_evictions_tag + Num_stack_evicts))
      % transfer_ratio
      % fifo_method_cache_t::Num_stall_cycles % (100.0 * fifo_method_cache_t::Num_stall_cycles / (float)s.Cycle);

    if (options.short_stats)
      return;

    // print stats per method
    os << "       Method:        #hits     #misses  methodsize      blocks    "
          "min-util    max-util\n";
    for (method_stats_t::iterator i(Method_stats.begin()),
         ie(Method_stats.end());
         i != ie; i++)
    {
      unsigned int hits = 0;
      unsigned int misses = 0;
      for (offset_stats_t::iterator j(i->second.Accesses.begin()),
           je(i->second.Accesses.end());
           j != je; j++)
      {
        hits += j->second.first;
        misses += j->second.second;
      }

      // Skip all stats entries that are never accessed since the last stats reset
      if (hits + misses == 0)
      {
        continue;
      }

      os << boost::format("   0x%1$08x:   %2$10d  %3$10d  %4$10d  %5$10d "
                          "%6$10.2f%% %7$10.2f%%    %8%\n") %
                i->first % hits % misses % i->second.Num_method_bytes % i->second.Num_blocks_allocated % (i->second.Min_utilization * 100.0) % (i->second.Max_utilization * 100.0) % s.Symbols.find(i->first);

      // print hit/miss statistics per offset
      if (options.hitmiss_stats)
      {
        for (offset_stats_t::iterator j(i->second.Accesses.begin()),
             je(i->second.Accesses.end());
             j != je; j++)
        {
          os << boost::format("     0x%1$08x: %2$10d  %3$10d %4%\n") % (i->first + j->first) % j->second.first % j->second.second % s.Symbols.find(i->first + j->first);
        }
      }
    }

    // print Eviction statistics
    if (options.hitmiss_stats)
    {
      os << "\n       Method:    #capacity        #tag        #stack          by\n";
      for (method_stats_t::iterator i(Method_stats.begin()), ie(Method_stats.end());
           i != ie; i++)
      {
        // count number of evictions
        unsigned int num_capacity_evictions = 0;
        unsigned int num_tag_evictions = 0;
        unsigned int num_stack_evictions = 0;
        for (eviction_stats_t::iterator j(i->second.Evictions.begin()), je(i->second.Evictions.end()); j != je; j++)
        {
          num_capacity_evictions += j->second.first;
          num_tag_evictions += j->second.second;
        }
        num_stack_evictions = Stack_evict_stats[i->first];

        // print address and name of current method
        os << boost::format("   0x%1$08x:   %2$10d  %3$10d  %4$10d         %5%\n") % i->first % num_capacity_evictions % num_tag_evictions % num_stack_evictions % s.Symbols.find(i->first);

        // print other methods who evicted this method
        for (eviction_stats_t::iterator j(i->second.Evictions.begin()),
             je(i->second.Evictions.end());
             j != je; j++)
        {
          os << boost::format("                 %1$10d  %2$10d  0x%3$08x  %4%\n") % j->second.first % j->second.second % j->first % s.Symbols.find(j->first);
        }
      }
    }
  }
}
