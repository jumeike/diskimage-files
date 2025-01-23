#include <cstdint>
//#include <libmemcached/memcached.h>
#include <memory>
#include <iostream>
#include <cstdint>     
#include <utility>     
#include <cstdio>      
#include <mutex>

// External dependencies
extern "C" {
#include "memcached.h"
#include "slabs.h"
#include "storage.h"
#include "hash.h"
#include "assoc.h"
#include "stats_prefix.h"
#include <event2/event.h>
#include <event2/thread.h>
}

#include <time.h>

#define ENABLE_GEM5 1
#define ENABLE_CEREBELLUM 1

#if ENABLE_GEM5==1
#pragma message("Compiling with gem5 instructions")
#include <gem5/m5ops.h>
#include "m5_mmap.h"
#endif // ENABLE_GEM5


#if ENABLE_CEREBELLUM==1
#pragma message("Compiling with cerebellum")
#include "cerebellum_job.h"
#include "cerebellum_manager.h"
CerebellumManagerFactory factory = CerebellumManagerFactory();
auto cerebellum_manager = factory.getManager();
uint64_t* sendAddress =nullptr;
uint64_t* readAddress = nullptr;
#define cmd_send_dpdk_buf    0
#define cmd_send_dpdk_len    1
#define cmd_set_app_flag     2
#define cmd_send_app_resp    3
#define cmd_send_app_buf     4
#define cmd_set_dpdk_flag    5

// TODO: move this to manager
//  an engine command is 12 bits to leave room for 52 bits physical address.
//  cmd 0 is reserved. All 0's commands is used to get back to engine commands.
#define cmd_tb_addr_beg   0x001
#define cmd_tb_addr_end   0x002

#endif // ENABLE_CEREBELLUM

enum OpType {
    SET,
    GET
};

class MemcachedBusinessLogic {
private:
   LIBEVENT_THREAD thread;
   std::mutex mutex_;
   struct event_base *main_base;
   struct event clockevent;
   std::thread event_thread;

   // Original allocated pointers
   uint8_t* raw_recv_buf_;
   uint8_t* raw_resp_buf_;
   // Aligned pointers for use
   uint8_t* recv_buf_;    // Receive Buffer
   uint8_t* resp_buf_;    // Response Buffer 

   static constexpr size_t BUFFER_SIZE = 1024 * 1024;
   volatile uint64_t* sendAddress;
   volatile uint64_t* readAddress;

   static void clock_handler(evutil_socket_t fd, short which, void *arg) {
      struct timeval t = {.tv_sec = 1, .tv_usec = 0};
      // struct timespec ts;

      // // Update current_time
      // if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
      //     struct timeval tv;
      //     gettimeofday(&tv, NULL);
      //     current_time = (rel_time_t)(tv.tv_sec - process_started);

      // } else {
      //     current_time = (rel_time_t)(ts.tv_sec - process_started);
      // }

      // Reschedule timer
      MemcachedBusinessLogic *handler = (MemcachedBusinessLogic *)arg;
      evtimer_del(&handler->clockevent);
      evtimer_set(&handler->clockevent, clock_handler, handler);
      event_base_set(handler->main_base, &handler->clockevent);
      evtimer_add(&handler->clockevent, &t);
   }

public:
   MemcachedBusinessLogic(volatile uint64_t* sendAddr, volatile uint64_t* readAddr)
       : sendAddress(sendAddr),
         readAddress(readAddr) {
        
        // Initialize settings
      settings.use_cas = true;
      settings.maxbytes = static_cast<size_t>(1ULL * 1024 * 1024 * 1024); // 4GB
      settings.maxconns = 1024;
      settings.factor = 1.25;
      settings.chunk_size = 256;
      settings.num_threads = 1;
      settings.item_size_max = 1024 * 1024;
      settings.slab_page_size = 1024 * 1024;
      settings.slab_chunk_size_max = settings.slab_page_size / 2;
      settings.hashpower_init = 0;
      settings.oldest_live = 0;
      current_time = std::numeric_limits<rel_time_t>::max(); // Max time avoids flush

      enum hashfunc_type hash_type = MURMUR3_HASH;

      if (hash_init(hash_type) != 0) {
        throw std::runtime_error("Failed to initialize hash_algorithm!\n");
      }
      
      // Initialize stats
      memset(&stats, 0, sizeof(struct stats));
      memset(&stats_state, 0, sizeof(struct stats_state));
      stats_state.accepting_conns = true;
      process_started = time(0) - ITEM_UPDATE_INTERVAL - 2;
      stats_prefix_init(settings.prefix_delimiter);

      // Initialize subsystems
      slabs_init(settings.maxbytes, settings.factor, true, nullptr, nullptr, false);
      assoc_init(settings.hashpower_init);
      memcached_thread_init(settings.num_threads, nullptr);
      
      // Initialize thread stats
      threadlocal_stats_reset();
      void *result = slabs_alloc(48, 1, 0);
      std::cerr << "Initial slab allocation: " << result << std::endl;
      // if (!slabs_alloc(settings.chunk_size, 1, 0)) {
      //     throw std::runtime_error("Failed to initialize storage");
      // }

      // Initialize thread
      memset(&thread, 0, sizeof(LIBEVENT_THREAD));

      // Initialize event base
      main_base = event_base_new();
      if (!main_base) {
        throw std::runtime_error("Failed to create event base");
      }

      // Setup timer
      struct timeval t = {.tv_sec = 1, .tv_usec = 0};
      evtimer_set(&clockevent, clock_handler, this);
      event_base_set(main_base, &clockevent);
      evtimer_add(&clockevent, &t);

      // Start event loop
      event_thread = std::thread([this]() {
          event_base_dispatch(main_base);
      });
      event_thread.detach();

      // Allocate aligned buffers
      raw_recv_buf_ = new uint8_t[BUFFER_SIZE + 0x10];  // Extra space for alignment
      raw_resp_buf_ = new uint8_t[BUFFER_SIZE + 0x10];

      recv_buf_ = allocateAlignedBuffer(raw_recv_buf_);
      resp_buf_ = allocateAlignedBuffer(raw_resp_buf_);
   }
   
   uint8_t* allocateAlignedBuffer(uint8_t* raw_buf) {
       // Find next address ending in 0
       uintptr_t addr = reinterpret_cast<uintptr_t>(raw_buf);
       uintptr_t aligned_addr = (addr + 0xF) & ~0xF;
       while((aligned_addr & 0xF) != 0x0) {
           aligned_addr += 0x10;
       }

       printf("Original address: 0x%lx\n", addr);
       printf("Aligned address: 0x%lx\n", aligned_addr);
       printf("===============================\n");

       return reinterpret_cast<uint8_t*>(aligned_addr);
   }

   // Get pointer to receive buffer
   uint8_t* getRecvBuffer() {
       printf("Using recv buffer at: 0x%lx\n",
              reinterpret_cast<uintptr_t>(recv_buf_));
       printf("====================================\n");
       return recv_buf_;
   }

   void processRequests() {
        int runs = 0;
        while(runs < 10000) {
            // Send buffer to get next request
            uint64_t cmd = reinterpret_cast<uint64_t>(recv_buf_) | cmd_set_app_flag;
            *sendAddress = cmd;
            
            // Read request info
            volatile uint64_t request = *readAddress;
            bool isGet = request & 0x1;
            size_t keyLen = (request >> 1) & 0x7FF;
            size_t valueLen = (request >> 12) & 0x7FF;
            
            std::vector<int8_t> key(recv_buf_, recv_buf_ + keyLen);
            
            if (isGet) { // GET
                std::vector<int8_t> value;
                bool success = handleGet(key, value);
                
                if (success) {
                    // Copy value to response buffer
                    memcpy(resp_buf_, value.data(), value.size());
                    sendResponse(true, true, value.size());
                    sendBuffer();
                } else {
                    sendResponse(true, false, 0);
                }
            } else { // SET
                std::vector<int8_t> value(recv_buf_ + keyLen, recv_buf_ + keyLen + valueLen);
                bool success = handleSet(key, value);
                sendResponse(false, success, 0);
            }
            runs++;
        }
    }

    bool handleGet(const std::vector<int8_t>& key, std::vector<int8_t>& value) {
      // Convert key to string for memcached API
      std::string key_str(reinterpret_cast<const char*>(key.data()), key.size());
      // printf("GET attempt with key size: %zu, key content: ", key.size());
      // for(size_t i = 0; i < key.size(); i++) {
      //     printf("%02x ", key[i]);
      // }
      // printf("\n");

      // Get the item directly from cache
      item* it = item_get(key_str.c_str(), key_str.length(), &thread, true);
      
      if (it != nullptr) {
          // Extract value from item
          const char* value_ptr = ITEM_data(it);
          size_t value_len = it->nbytes;
          
          // Copy value to return buffer
          value.assign(reinterpret_cast<const int8_t*>(value_ptr),
                      reinterpret_cast<const int8_t*>(value_ptr + value_len));
          
          // printf("GET attempt with value size: %zu, Value content: ", value.size());
          // for(size_t i = 0; i < value.size(); i++) {
          //     printf("%02x ", value[i]);
          // }
          // printf("\n"); 

          // Release our reference
          item_remove(it);
      } else {
        std::cout << "Item not found for key: " << key_str << std::endl;
        value.clear();
        return false;
      }
      return true;
   }

   bool handleSet(const std::vector<int8_t>& key, const std::vector<int8_t>& value) {
      // Convert key and value to strings for memcached API
      std::string key_str(reinterpret_cast<const char*>(key.data()), key.size());
      std::string value_str(reinterpret_cast<const char*>(value.data()), value.size());
      // printf("SET attempt with key size: %zu, key content: ", key.size());
      // for(size_t i = 0; i < key.size(); i++) {
      //     printf("%02x ", key[i]);
      // }
      // printf("\n");

      // printf("SET attempt with value size: %zu, value content: ", value.size());
      // for(size_t i = 0; i < value.size(); i++) {
      //     printf("%02x ", value[i]);
      // }
      // printf("\n");
      
      // Allocate new item
      item* it = item_alloc(key_str.c_str(), key_str.length(), 0, 0, value.size());
      if (it == nullptr) {
        std::cerr << "item_alloc failed" << std::endl;
          return false;
      }
      
      // Copy value into item
      memcpy(ITEM_data(it), value.data(), value.size());
      
      // Store the item
      enum store_item_type status = store_item(it, NREAD_SET, &thread, nullptr, nullptr, 0, false);
      // std::cerr << "store_item returned:" << status << std::endl;
      return (status == STORED);
   }   

   void sendResponse(bool isGet, bool success, size_t valueLen) {
        uint64_t response = 0;
        response |= (isGet ? 1ULL : 0ULL) << 4;
        response |= ((success ? 1ULL : 0ULL) << 5);
        response |= (valueLen & 0x7FF) << 6;
        
        uint64_t cmd = response | cmd_send_app_resp;
        *sendAddress = cmd;
        volatile uint64_t ack = *readAddress;
    }

    void sendBuffer() {
        uint64_t cmd = reinterpret_cast<uint64_t>(resp_buf_) | cmd_send_app_buf;
        *sendAddress = cmd;
        volatile uint64_t ack = *readAddress;
    }

   ~MemcachedBusinessLogic() {
       // Delete original allocated memory
       delete[] raw_recv_buf_;
       delete[] raw_resp_buf_;
       
       if (main_base) {
          event_base_loopbreak(main_base);
          event_base_free(main_base);
       }
   }
};

int main() {

    #if ENABLE_GEM5==1

      map_m5_mem();
      m5_work_begin_addr(0, 0); // switch cpu type needed before working with the engine

      #if ENABLE_CEREBELLUM==1

        CerebellumManagerFactory::waitingTillMSRReady();
        std::cout<<"MSR ready \n";
        cerebellum_manager->sendJobMSR(CerebellumJob());
        
        uint64_t cpuid = 0;
        // std::pair<uint8_t*,uint64_t> tb_mem = cerebellum_manager->get_physical_memory_cpuid(cpuid);
        // uint8_t* vadd = tb_mem.first;
        // uint64_t padd = tb_mem.second;
        // printf("Allocating physical memory for translation buffer 0x%lx.\n", padd);
        // vadd[0] = 0xf; // Force OS page mapping
        
        printf("Allocating uncacheable page to comminucate with the engine.\n");
        auto add = cerebellum_manager->getAddress(cpuid);
                
        sendAddress = add.first;
        readAddress = add.second;
        
        // Initialize engine commands
        uint64_t num_init_commands = 0;
        *sendAddress = num_init_commands;
        volatile uint64_t temp0 = *readAddress;
        
        // // Configure translation buffer addresses
        // uint64_t work = padd << 12 | cmd_tb_addr_beg;
        // *sendAddress = work;
        // volatile uint64_t temp1 = *readAddress;
        // printf("tb cmd 0x%lx\n", work);
        
        // work = ((padd + 4096) << 12) | cmd_tb_addr_end; // 4K page size
        // *sendAddress = work;
        // volatile uint64_t temp2 = *readAddress;
        // printf("tb cmd 0x%lx\n", work);

    // Set up service on port 11211 (default memcached port)
    int port = 11211;
    std::cout << "Starting memcached service on port " << port << std::endl;

    MemcachedBusinessLogic logic(sendAddress, readAddress);

    // Start of measurement
    m5_exit_addr(0);

    logic.processRequests();

    #endif // ENABLE_CEREBELLUM

    m5_work_end_addr(0, 0); // switch back to cpu type
    unmap_m5_mem();

    #endif // ENABLE_GEM5  
    return 0;
}
