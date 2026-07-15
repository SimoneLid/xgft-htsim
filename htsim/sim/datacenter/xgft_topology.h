// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef XGFT
#define XGFT
#include "main.h"
#include "randomqueue.h"
#include "pipe.h"
#include "config.h"
#include "loggers.h"
#include "network.h"
#include "firstfit.h"
#include "topology.h"
#include "logfile.h"
#include "eventlist.h"
#include "switch.h"
#include <ostream>
#include <memory>
#include <optional>

//#define N K*K*K/4

#ifndef QT
#define QT
typedef enum {UNDEFINED, RANDOM, ECN, COMPOSITE, PRIORITY,
              CTRL_PRIO, FAIR_PRIO, LOSSLESS, LOSSLESS_INPUT, LOSSLESS_INPUT_ECN,
              COMPOSITE_ECN, COMPOSITE_ECN_LB, SWIFT_SCHEDULER, ECN_PRIO, AEOLUS, AEOLUS_ECN} queue_type;
typedef enum {UPLINK, DOWNLINK} link_direction;
#endif

// avoid random constants
/* must be defined when creating the topology

#define TOR_TIER 0
#define AGG_TIER 1
#define CORE_TIER 2*/

class XGFTTopology;


class XGFTTopologyCfg {
friend class XGFTTopology;
friend std::ostream &operator<<(std::ostream &os, XGFTTopologyCfg const &m);
public:

    XGFTTopologyCfg(queue_type q, queue_type snd);
    
    /*
    TODO after
    XGFTTopologyCfg(istream& file, mem_b queue_size, queue_type q, queue_type snd);
    */
    
    XGFTTopologyCfg(uint32_t tiers, vector<uint32_t> no_of_children, vector<uint32_t> no_of_parent, linkspeed_bps linkspeed, 
                    mem_b queuesize, simtime_picosec latency, simtime_picosec switch_latency, 
                    queue_type q, queue_type snd);

    /*
    TODO after

    XGFTTopologyCfg(uint32_t no_of_nodes, linkspeed_bps linkspeed, mem_b queuesize,
                       queue_type q);

    XGFTTopologyCfg(uint32_t no_of_nodes, linkspeed_bps linkspeed, mem_b queuesize,
                       queue_type q, uint32_t num_failed);

    XGFTTopologyCfg(uint32_t no_of_nodes, linkspeed_bps linkspeed, mem_b queuesize, uint32_t fail,
                       queue_type q, queue_type snd);
    */

    static unique_ptr<XGFTTopologyCfg> load(string filename, mem_b queuesize, queue_type q_type, queue_type sender_q_type);
    /*
     Check if all settings in the config are correct. Will abort and print an error message if not.
    */
    void check_consistency() const;

    void set_tier_parameters(int tier, int radix_up, int radix_down, mem_b queue_up, mem_b queue_down, int bundlesize, linkspeed_bps downlink_speed, int oversub);

    void set_ecn_parameters(bool enable_ecn, bool enable_on_tor_downlink, mem_b ecn_low, mem_b ecn_high){
        _enable_ecn = enable_ecn;
        _enable_ecn_on_tor_downlink = enable_on_tor_downlink;
        _ecn_low = ecn_low;
        _ecn_high = ecn_high;
    }

    void set_failed_links(int num_failed_links) {
        _num_failed_links = num_failed_links;
    }

    void set_tiers(uint32_t tiers) { if(tiers!=0) _tiers = tiers;}
    uint32_t get_tiers() const { return _tiers; }


    // modified to be modular with different tiers
    void set_latencies(vector<simtime_picosec> link_latencies, vector<simtime_picosec> switch_latencies) {
        for (int tier = 0; tier < link_latencies.size() && tier < _tiers; tier++) {
            _link_latencies[tier] = link_latencies[tier];
        }

        for (int tier = 0; tier < switch_latencies.size() && tier < _tiers; tier++) {
            _switch_latencies[tier] = switch_latencies[tier];
        }
    }
    

    void set_linkspeeds(linkspeed_bps linkspeed);
    void set_queue_sizes(mem_b queuesize);

    void set_params(vector<uint32_t> no_of_children, vector<uint32_t> no_of_parent);
    void set_custom_params(uint32_t no_of_nodes);


    uint32_t no_of_nodes() const {return _no_of_nodes;}
    uint32_t no_of_switches(int tier) const {return NSW[tier];}
    uint32_t no_of_servers() const {return NSRV;}
    uint32_t bundlesize(int tier) const {return _bundlesize[tier];}
    uint32_t radix_up(int tier) const {return _radix_up[tier];}
    uint32_t radix_down(int tier) const {return _radix_down[tier];}
    uint32_t queue_up(int tier) const {return _queue_up[tier];}
    uint32_t queue_down(int tier) const {return _queue_down[tier];}

    // modified with the tier as input
    int get_oversubscription_ratio(int tier){return _oversub[tier];}
    
    
    simtime_picosec get_diameter_latency() {return _diameter_latency;}
    simtime_picosec get_two_point_diameter_latency(int src, int dst);

    uint16_t get_diameter() {return _diameter;}
private:
    void initialize(uint32_t tiers, vector<uint32_t> no_of_children, vector<uint32_t> no_of_parent, linkspeed_bps linkspeed, 
                    mem_b queuesize, simtime_picosec latency, simtime_picosec switch_latency, 
                    queue_type q, queue_type snd);
    void read_cfg(istream& file, mem_b queuesize);

    bool _from_file;

    queue_type _qt;
    queue_type _sender_qt;

    // unified in a vector NSW[_tiers] to be accessed easily
    vector<uint32_t> NSW;

    uint32_t _tiers;

    uint32_t TOR_TIER = 0;
    uint32_t LAST_AGG_TIER;
    uint32_t CORE_TIER;


    // _link_latencies[0] is the ToR->host latency.
    vector<simtime_picosec> _link_latencies;

    // _switch_latencies[0] is the ToR switch latency.
    vector<simtime_picosec> _switch_latencies;

    // How many uplinks to bundle from each node in a tier to the same
    // node in the tier below.  Eg bundlesize[2] = 2 means two
    // bundled links from Core to Upper pod switch (and vice versa)
    //
    // Note: we don't currently support bundling from the hosts to
    // ToRs because transport needs to know for that to work.
    vector<uint32_t> _bundlesize;

    // Linkspeed of each link in a switch tier to the tier below. ToRs are tier 0.
    // Eg. _downlink_speeds[0] = 400Gbps indicates 400Gbps links from hosts
    // to ToRs.
    vector<linkspeed_bps> _downlink_speeds;

    // degree of oversubscription at tier.  Eg _oversub[TOR_TIER] = 3 implies 3x more bw to hosts than to agg switches.
    vector<uint32_t> _oversub;

    // switch radix used.  Eg _radix_down[0] = 32 indicates 32 downlinks from ToRs.  _radix_up[2] should be zero in a 3-tier topology.  
    vector<uint32_t> _radix_down;
    vector<uint32_t> _radix_up;

    // switch queue size used.  Eg _queue_down[0] = 32 indicates 32 downlinks from ToRs.  _queue_up[2] should be zero in a 3-tier topology.  
    vector<mem_b> _queue_down;
    vector<mem_b> _queue_up;


    //ecn parameters
    bool _enable_ecn;
    bool _enable_ecn_on_tor_downlink;
    mem_b _ecn_low;
    mem_b _ecn_high;

    //failed links hack
    uint32_t _num_failed_links;
    double _failed_link_ratio;

    uint32_t _no_of_nodes;
    simtime_picosec _hop_latency,_switch_latency;

    simtime_picosec _diameter_latency;
    uint16_t _diameter;
};

template<class P> void delete_4d_vector(vector<vector<vector<vector<P*>>>>& vec4d);

class XGFTTopology: public Topology{
public:
    XGFTTopology(const XGFTTopologyCfg* cfg,
                    QueueLoggerFactory* logger_factory,
                    EventList* ev,
                    FirstFit * fit);
    ~XGFTTopology() override;


    /*
    vector <Switch*> switches_lp;
    vector <Switch*> switches_up;
    vector <Switch*> switches_c;

    // 3rd index is link number in bundle
    vector< vector< vector<Pipe*> > > pipes_nc_nup;
    vector< vector< vector<Pipe*> > > pipes_nup_nlp;
    vector< vector< vector<Pipe*> > > pipes_nlp_ns;
    vector< vector< vector<BaseQueue*> > > queues_nc_nup;
    vector< vector< vector<BaseQueue*> > > queues_nup_nlp;
    vector< vector< vector<BaseQueue*> > > queues_nlp_ns;

    vector< vector< vector<Pipe*> > > pipes_nup_nc;
    vector< vector< vector<Pipe*> > > pipes_nlp_nup;
    vector< vector< vector<Pipe*> > > pipes_ns_nlp;
    vector< vector< vector<BaseQueue*> > > queues_nup_nc;
    vector< vector< vector<BaseQueue*> > > queues_nlp_nup;
    vector< vector< vector<BaseQueue*> > > queues_ns_nlp;
    */


    // everything in a single vector where the 1st index is the tier
    vector<vector<Switch*>> switches;

    vector<vector<vector<vector<Pipe*>>>> pipes_down;
    vector<vector<vector<vector<BaseQueue*>>>> queues_down;

    // when going up the 1st index is +1
    vector<vector<vector<vector<Pipe*>>>> pipes_up;
    vector<vector<vector<vector<BaseQueue*>>>> queues_up;


    QueueLoggerFactory* _logger_factory;
    EventList* _eventlist;
    FirstFit* _ff;

    virtual vector<const Route*>* get_bidir_paths(uint32_t src, uint32_t dest, bool reverse);

    BaseQueue* alloc_src_queue(QueueLogger* q);
    BaseQueue* alloc_queue(QueueLogger* q, const mem_b queuesize, link_direction dir, int switch_tier, bool tor=false);
    BaseQueue* alloc_queue(QueueLogger* q, linkspeed_bps speed, const mem_b queuesize, link_direction dir,  int switch_tier, bool tor, bool reduced_speed);
    void count_queue(Queue*);
    void print_path(std::ofstream& paths,uint32_t src,const Route* route);
    vector<uint32_t>* get_neighbours(uint32_t src) { return NULL;};

    void add_failed_link(uint32_t type, uint32_t switch_id, uint32_t link_id);

    // add loggers to record total queue size at switches
    virtual void add_switch_loggers(Logfile& log, simtime_picosec sample_period); 

    const XGFTTopologyCfg& cfg() { return *_cfg; };
private:
    const XGFTTopologyCfg* _cfg;
    map<Queue*,int> _link_usage;
    int64_t find_lp_switch(Queue* queue);
    int64_t find_up_switch(Queue* queue);
    int64_t find_core_switch(Queue* queue);
    int64_t find_destination(Queue* queue);
    void alloc_vectors();
};

#endif
