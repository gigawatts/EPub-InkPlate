#pragma once

#include <forward_list>
#include <inttypes.h>
#include <stdlib.h>

class CharPool
{
  private:
    static const uint16_t POOL_SIZE = 4096;
    typedef char Pool[POOL_SIZE];

    std::forward_list<Pool *> pool_list;

    Pool *   current;
    uint16_t current_idx;
    uint32_t total_allocated;

  public:
    CharPool() : 
              current(nullptr), 
          current_idx(0),
      total_allocated(0) {}

   ~CharPool() { for (auto * pool : pool_list) free(pool); }

    uint32_t get_total_allocated() { return total_allocated; }
    char * allocate(uint16_t size) {
      if ((current == nullptr) || ((current_idx + size) >= POOL_SIZE)) {
        if ((current = (Pool *) malloc(POOL_SIZE)) != nullptr) {
          pool_list.push_front(current);
          current_idx = 0;
        }
        else {
          return nullptr;
        }
      }

      char * tmp = &(((char *)current)[current_idx]);
      
      current_idx     += size;
      total_allocated += size;

      return tmp;
    }
};