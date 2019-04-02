#define DEGREE 1
#include <stdio.h>
#include "cache.h"
#include <map>
#include <set>
#include <cassert>
#include <set>
#include "bo_percore.h" 

unsigned int total_access;
unsigned int predictions;
unsigned int no_prediction;
uint64_t last_address;
uint64_t average_distance;

//#define HYBRID
struct GHB_Entry
{
    uint64_t phy_addr;
    int last_index;
};

struct STMS_prefetcher_t
{
    vector<GHB_Entry> GHB;
    map<uint64_t, uint64_t> index_table;

    void stms_train(uint64_t phy_addr)
    {
        GHB_Entry new_entry;
        new_entry.phy_addr = phy_addr;
        if(index_table.find(phy_addr) != index_table.end())
            new_entry.last_index = index_table[phy_addr];
        else
            new_entry.last_index = -1;

        GHB.push_back(new_entry);
        assert(GHB.size() >= 1);

        index_table[phy_addr] = (GHB.size() - 1);
        average_distance += (GHB.size() - new_entry.last_index);
        //cout << (GHB.size() - new_entry.last_index) << endl;
    }

    vector<uint64_t> stms_predict(uint64_t trigger_phy_addr)
    {
        vector<uint64_t> candidates;
        candidates.clear();

        if(index_table.find(trigger_phy_addr) != index_table.end())
        {
            uint64_t index = index_table[trigger_phy_addr];
            assert(GHB[index].phy_addr == trigger_phy_addr);

            for(unsigned int i=1; i<=32; i++)
            {
                if((index+i) >= GHB.size())
                    break;
                uint64_t candidate_phy_addr = GHB[index+i].phy_addr;
                candidates.push_back(candidate_phy_addr);
            }
        }
        else
            no_prediction++;

        return candidates;
    }

    public :
    STMS_prefetcher_t()
    {
        GHB.clear();
        index_table.clear();
    }
};

STMS_prefetcher_t stms[NUM_CPUS];


void CACHE::l2c_prefetcher_initialize(uint32_t cpu) 
{
    last_address = 0;
    total_access = 0;
    predictions = 0;
    no_prediction = 0;
    average_distance = 0;
#ifdef HYBRID
    bo_l2c_prefetcher_initialize();
#endif
}

void CACHE::l2c_prefetcher_operate(uint32_t cpu, uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type)
{
    if (type != LOAD)
        return;

    if(cache_hit)
        return;

     uint64_t addr_B = (addr >> 6) << 6;

    if(addr_B == last_address)
        return;
    last_address = addr_B;

    total_access++;


#ifdef HYBRID
    uint64_t bo_trigger_addr = 0;
    uint64_t bo_target_offset = 0;
    uint64_t bo_target_addr = 0;
    bo_l2c_prefetcher_operate(addr, ip, cache_hit, type, this, &bo_trigger_addr, &bo_target_offset, 0);

    if (bo_trigger_addr && bo_target_offset) {

        for(unsigned int i=1; i<=DEGREE; i++) {
            bo_target_addr = bo_trigger_addr + (i*bo_target_offset); 
            bo_issue_prefetcher(this, ip, bo_trigger_addr, bo_target_addr, FILL_LLC);
        }
    }
#endif
       //Predict before training
    vector<uint64_t> candidates = stms[cpu].stms_predict(addr_B);

    unsigned int num_prefetched = 0;
    for(unsigned int i=0; i<candidates.size(); i++)
    {
        int ret = prefetch_line(ip, addr, candidates[i], FILL_LLC);
        if (ret == 1)
        {
            predictions++;
            num_prefetched++;
        }
        if(num_prefetched >= DEGREE)
            break;
    }
    stms[cpu].stms_train(addr_B);                
}

void CACHE::l2c_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr)
{
#ifdef HYBRID
    bo_l2c_prefetcher_cache_fill(addr, set, way, prefetch, evicted_addr, this, 0);
#endif
}

//void CACHE::inform_tlb_eviction(uint64_t insert_page_addr, uint64_t evict_page_addr)
//{
//}



void CACHE::l2c_prefetcher_final_stats(uint32_t cpu)
{
#ifdef HYBRID
	bo_l2c_prefetcher_final_stats();
#endif
  printf("Prefetcher final stats\n");
    cout << "Index Table Size: " << stms[cpu].index_table.size() << endl;
    cout << "GHB size: " << stms[cpu].GHB.size() << endl;;
    cout << endl << endl;
    cout << "Triggers: " << total_access << endl;
    cout << "No Prediction: " << no_prediction << " " << 100*(double)no_prediction/(double)total_access << endl;
    cout << "Predictions: " << predictions << " " << 100*(double)predictions/(double)total_access << endl;

    cout << "Average distance: " << (double)average_distance/(double)total_access << endl;
    cout << endl << endl;
}

void CACHE::complete_metadata_req(uint64_t meta_data_addr)
{
}
