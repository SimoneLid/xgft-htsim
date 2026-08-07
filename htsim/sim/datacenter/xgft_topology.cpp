// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "xgft_topology.h"
#include <vector>
#include "string.h"
#include <sstream>

#include <iostream>
#include "main.h"
#include "queue.h"
#include "xgft_switch.h"
#include "compositequeue.h"
#include "aeolusqueue.h"
#include "prioqueue.h"
#include "ecnprioqueue.h"
#include "queue_lossless.h"
#include "queue_lossless_input.h"
#include "queue_lossless_output.h"
#include "swift_scheduler.h"
#include "ecnqueue.h"

// use tokenize from connection matrix
extern void tokenize(string const &str, const char delim, vector<string> &out);

// in-place conversion to lower case
void to_lower(string& s) {
    string::iterator i;
    for (i = s.begin(); i != s.end(); i++) {
        *i = std::tolower(*i);
    }
        //std::transform(s.begin(), s.end(), s.begin(),
        //[](unsigned char c){ return std::tolower(c); });
}

std::ostream &operator<<(std::ostream &os, XGFTTopologyCfg const &m) { 
    os << "XGFTTopologyCfg" << " NSW=[";
    for (size_t i = 0; i < m.NSW.size(); i++) {
        os << m.NSW[i] << (i + 1 < m.NSW.size() ? "," : "");
    }
    os << "]";

    os  << " NSRV=" << m.NSRV
        << " tiers=" << m._tiers
        << " enabled_ecn=" << m._enable_ecn
        << " enable_ecn_on_tor_downlink=" << m._enable_ecn_on_tor_downlink
        << " ecn_low=" << m._ecn_low
        << " ecn_high=" << m._ecn_high
        << " num_failed_links=" << m._num_failed_links
        << " failed_link_ratio=" << m._failed_link_ratio
        << " no_of_nodes=" << m._no_of_nodes
        << " hop_latency=" << m._hop_latency
        << " switch_latency=" << m._switch_latency
        << " diameter_latency=" << m._diameter_latency
        << " diameter=" << m._diameter;
    
    for (uint32_t tier = 0; tier < m._tiers; tier++) {
        os  << " tier=" << tier
            << " link_latency=" << m._link_latencies[tier]
            << " switch_latencies=" << m._switch_latencies[tier]
            << " bundlesize=" << m._bundlesize[tier]
            << " downlink_speeds=" << m._downlink_speeds[tier]
            << " oversub=" << m._oversub[tier]
            << " radix_down=" << m._radix_down[tier]
            << " queue_down=" << m._queue_down[tier];
    
        if (tier < m._tiers-1) {
            os   << " radix_up=" << m._radix_up[tier]
                 << " queue_up=" << m._queue_up[tier];
        }
    }

    return os;
}



XGFTTopologyCfg::XGFTTopologyCfg(queue_type q, queue_type snd):
                        _from_file(false),
                        _qt(q),
                        _sender_qt(snd), 
                        NSRV(0),
                        _tiers(3),
                        _enable_ecn(false),
                        _enable_ecn_on_tor_downlink(false),
                        _ecn_low(0),
                        _ecn_high(0),
                        _num_failed_links(0),
                        _failed_link_ratio(0.25),
                        _no_of_nodes(0),
                        _hop_latency(0),
                        _switch_latency(0),
                        _diameter_latency(0),
                        _diameter(0)
                        {

}




XGFTTopologyCfg::XGFTTopologyCfg(uint32_t tiers, uint32_t no_of_nodes, vector<uint32_t> no_of_children, vector<uint32_t> no_of_parent, 
                                    linkspeed_bps linkspeed, mem_b queuesize,
                                    simtime_picosec latency, simtime_picosec switch_latency, 
                                    queue_type q, queue_type snd):
                                    XGFTTopologyCfg(q, snd) {
    initialize(tiers, no_of_nodes, no_of_children, no_of_parent, linkspeed, queuesize, latency, switch_latency, q, snd);
}

/* TODO after
XGFTTopologyCfg::XGFTTopologyCfg(istream& file, mem_b queue_size,
                                       queue_type q, queue_type snd):
                                       XGFTTopologyCfg(q, snd) {
    read_cfg(file, queue_size);
    _from_file = true;
    initialize(0u, _no_of_nodes, 0u, 0u, 0u, 0u, q, snd);
}
*/

void XGFTTopologyCfg::initialize(uint32_t tiers, uint32_t no_of_nodes, vector<uint32_t> no_of_children, vector<uint32_t> no_of_parent,
                                    linkspeed_bps linkspeed, mem_b queuesize,
                                    simtime_picosec latency, simtime_picosec switch_latency, 
                                    queue_type q, queue_type snd) {

    if (tiers == 1) {
        LAST_AGG_TIER = 0;
        CORE_TIER = 0;
    } else if (tiers == 2) {
        LAST_AGG_TIER = tiers-1;
        CORE_TIER = 0;
    } else { // tiers >= 3
        CORE_TIER = tiers-1;
        LAST_AGG_TIER = tiers-2;
    }
    
    assert(tiers > 0);
    // define the size of all the vectors
    _link_latencies.resize(tiers, 0);
    _switch_latencies.resize(tiers, 0);
    _bundlesize.resize(tiers, 1);
    _downlink_speeds.resize(tiers, 0);
    _oversub.resize(tiers, 1);
    _radix_down.resize(tiers, 0);
    _radix_up.resize(tiers-1, 0);
    _queue_down.resize(tiers, 0);
    _queue_up.resize(tiers-1, 0);
    W.resize(tiers+1, 1);


    set_tiers(tiers);
    set_linkspeeds(linkspeed);
    set_queue_sizes(queuesize);
    if ((latency != 0 || switch_latency != 0)) {
        for (int tier = TOR_TIER; tier <= CORE_TIER; tier++) {
            if ((_link_latencies[tier] != 0 && _link_latencies[tier] != latency)
                || (_switch_latencies[tier] != 0 && _switch_latencies[tier] != switch_latency)) {
                cerr << "Tier " << tier << " Link latency " << _link_latencies[tier] << " Switch Latency " << _switch_latencies[tier] << endl;
                cerr << "Global " << " Latency " << latency << " Switch Latency " << switch_latency << endl;
                cerr << "Don't set latencies using both the constructor and set_latencies - use only one of the two\n";
                exit(1);
            }
        }
    }
    _hop_latency = latency;
    _switch_latency = switch_latency;

    _diameter_latency = 0;
    _diameter = (2 * _tiers);
    if (_link_latencies[TOR_TIER] == 0) {
        _diameter_latency = (_hop_latency * (2 * _tiers)) + (_switch_latency * (2 * _tiers - 1));
        cout << "XGFT topology (0) with " << timeAsUs(_hop_latency) << "us links and " 
             << timeAsUs(_switch_latency) << "us switching latency for " 
             << timeAsUs(_diameter_latency) << "us diameter latency." << endl;
    } else {

        _diameter_latency = 2*_link_latencies[TOR_TIER] + _switch_latencies[TOR_TIER];
        for (int tier = TOR_TIER+1; tier <= CORE_TIER; tier++){
            _diameter_latency += 2*_link_latencies[tier] + _switch_latencies[tier] + _switch_latencies[tier-1];
        }

        cout << "XGFT topology (0) with "
             << timeAsUs(_link_latencies[TOR_TIER]) << "us Src-ToR links, ";
        if (_tiers >= 2){
            cout << timeAsUs(_link_latencies[1]) << "us ToR-Agg1 links, ";
            for (int tier = 2; tier <= LAST_AGG_TIER; tier++){
                cout << timeAsUs(_link_latencies[tier]) << "us Agg" << tier-1 << "-Agg" << tier << " links, ";
            }
            if (_tiers >= 3){
                cout << timeAsUs(_link_latencies[CORE_TIER]) << "us Agg" << LAST_AGG_TIER << "-Core links, ";
            }
        }

        cout << timeAsUs(_switch_latencies[TOR_TIER]) << "us ToR switch latency, ";
        if (_tiers >= 2) {
            for (int tier = 1; tier <= LAST_AGG_TIER; tier++){
                    cout << timeAsUs(_switch_latencies[tier]) << "us Agg" << tier << " switch latency";
            }
        }   
        if (_tiers >= 3) {
            cout << ", " << timeAsUs(_switch_latencies[CORE_TIER]) << "us Core switch latency." << endl;
        } 
        cout << " for " << timeAsUs(_diameter_latency) << "us diameter latency." << endl;
    }
    set_params(no_of_nodes, no_of_children, no_of_parent);
}

/*
XGFTTopologyCfg::XGFTTopologyCfg(uint32_t no_of_nodes, linkspeed_bps linkspeed, mem_b queuesize,
                                       queue_type q):
                                       XGFTTopologyCfg(q, FAIR_PRIO) {
    set_linkspeeds(linkspeed);
    set_queue_sizes(queuesize);
    _num_failed_links = 0;
    if (_link_latencies[TOR_TIER] == 0) {
        _hop_latency = timeFromUs((uint32_t)1);
    } else {
        _hop_latency = timeFromUs((uint32_t)0); 
    }
    _switch_latency = timeFromUs((uint32_t)0); 

    _diameter_latency = 2 * (_hop_latency + _hop_latency) \
                        + 3 * _switch_latency;
    if (_tiers == 3) {
        _diameter_latency += 2 * _hop_latency \
                                + 2 * _switch_latency;
    }
 
    cout << "Fat tree topology (1) with " << no_of_nodes << " nodes"
         << " and " << timeAsUs(_diameter_latency) << "us diameter latency." << endl;;
    set_params(no_of_nodes);
}

XGFTTopologyCfg::XGFTTopologyCfg(uint32_t no_of_nodes, linkspeed_bps linkspeed, mem_b queuesize,
                                       queue_type q, uint32_t num_failed):
                                       XGFTTopologyCfg(q, FAIR_PRIO) {
    set_linkspeeds(linkspeed);
    set_queue_sizes(queuesize);
    if (_link_latencies[TOR_TIER] == 0) {
        _hop_latency = timeFromUs((uint32_t)1);
    } else {
        _hop_latency = timeFromUs((uint32_t)0); 
    }
    _switch_latency = timeFromUs((uint32_t)0); 
    _diameter_latency = 2 * (_hop_latency + _hop_latency) \
                        + 3 * _switch_latency;
    if (_tiers == 3) {
        _diameter_latency += 2 * _hop_latency \
                                + 2 * _switch_latency;
    }
    _diameter = (2 * _tiers);
 
    _num_failed_links = 0;
  
    cout << "Fat tree topology (2) with " << no_of_nodes << " nodes" 
         << " and " << timeAsUs(_diameter_latency) << "us diameter latency." << endl;;
    set_params(no_of_nodes);
}

XGFTTopologyCfg::XGFTTopologyCfg(uint32_t no_of_nodes, linkspeed_bps linkspeed, mem_b queuesize,
                                       uint32_t num_failed, queue_type q, queue_type snd):
                                       XGFTTopologyCfg(q, snd) {
    set_linkspeeds(linkspeed);
    set_queue_sizes(queuesize);
    if (_link_latencies[TOR_TIER] == 0) {
        _hop_latency = timeFromUs((uint32_t)1);
    } else {
        _hop_latency = timeFromUs((uint32_t)0); 
    }
    _switch_latency = timeFromUs((uint32_t)0); 
    _diameter_latency = 2 * (_hop_latency + _hop_latency) \
                        + 3 * _switch_latency;
    if (_tiers == 3) {
        _diameter_latency += 2 * _hop_latency \
                                + 2 * _switch_latency;
    }
    _diameter = (2 * _tiers);
    _num_failed_links = num_failed;

    cout << "Fat tree topology (3) with " << no_of_nodes << " nodes" 
         << " and " << timeAsUs(_diameter_latency) << "us diameter latency." << endl;;
    set_params(no_of_nodes);
}
*/

void XGFTTopologyCfg::set_custom_params(uint32_t no_of_nodes) {
    //cout << "set_custom_params" << endl;

    // check bundlesizes are feasible with switch radix
    for (uint32_t tier = TOR_TIER; tier < _tiers; tier++) {
        if (_radix_down[tier] == 0) {
            cerr << "Custom topology, but radix_down not set for tier " << tier << endl;
            exit(1);
        }
        if (_radix_down[tier] % _bundlesize[tier] != 0) {
            cerr << "Mismatch between tier " << tier << " down radix of " << _radix_down[tier] << " and bundlesize " << _bundlesize[tier] << "\n";
            cerr << "Radix must be a multiple of bundlesize\n";
            exit(1);
        }
        if (tier < (_tiers - 1) && _radix_up[tier] == 0) {
            cerr << "Custom topology, but radix_up not set for tier " << tier << endl;
            exit(1);
        }
        if (tier < (_tiers - 1) && _radix_up[tier] % _bundlesize[tier+1] != 0) {
            cerr << "Mismatch between tier " << tier << " up radix of " << _radix_up[tier] << " and tier " << tier+1 << " down bundlesize " << _bundlesize[tier+1] << "\n";
            cerr << "Radix must be a multiple of bundlesize\n";
            exit(1);
        }
    }

    _no_of_nodes = no_of_nodes;
    int no_of_tor_uplinks = 0;
    int no_of_agg_uplinks = 0;
    int no_of_core_switches = 0;
    if (no_of_nodes % _hosts_per_pod != 0) {
        cerr << "No_of_nodes is not a multiple of hosts_per_pod\n";
        exit(1);
    }

    no_of_pods = no_of_nodes / _hosts_per_pod; // we don't allow multi-port hosts yet
    assert(_bundlesize[TOR_TIER] == 1);
    if (_hosts_per_pod % _radix_down[TOR_TIER] != 0) {
        cerr << "Mismatch between TOR radix " << _radix_down[TOR_TIER] << " and podsize " << _hosts_per_pod << endl;
        exit(1);
    }
    _tor_switches_per_pod = _hosts_per_pod / _radix_down[TOR_TIER];

    assert((no_of_nodes * _downlink_speeds[TOR_TIER]) % (_downlink_speeds[AGG_TIER] * _oversub[TOR_TIER]) == 0);
    no_of_tor_uplinks = (no_of_nodes * _downlink_speeds[TOR_TIER]) / (_downlink_speeds[AGG_TIER] *  _oversub[TOR_TIER]);
    cout << "no_of_tor_uplinks: " << no_of_tor_uplinks << endl;

    if (_radix_down[TOR_TIER]/_radix_up[TOR_TIER] != _oversub[TOR_TIER]) {
        cerr << "Mismatch between TOR linkspeeds (" << speedAsGbps(_downlink_speeds[TOR_TIER]) << "Gbps down, "
             << speedAsGbps(_downlink_speeds[AGG_TIER]) << "Gbps up) and TOR radix (" << _radix_down[TOR_TIER] << " down, "
             << _radix_up[TOR_TIER] << " up) and oversubscription ratio of " << _oversub[TOR_TIER] << endl;
        exit(1);
    }

    assert(no_of_tor_uplinks % (no_of_pods * _radix_down[AGG_TIER]) == 0);
    _agg_switches_per_pod = no_of_tor_uplinks / (no_of_pods * _radix_down[AGG_TIER]);
    if (_agg_switches_per_pod * _bundlesize[AGG_TIER] != _radix_up[TOR_TIER]) {
        cerr << "Mismatch between TOR up radix " << _radix_up[TOR_TIER] << " and " << _agg_switches_per_pod
             << " aggregation switches per pod required by " << no_of_tor_uplinks << " TOR uplinks in "
             << no_of_pods << " pods " << " with an aggregation switch down radix of " << _radix_down[AGG_TIER] << endl;
        if (_bundlesize[AGG_TIER] == 1 && _radix_up[TOR_TIER] % _agg_switches_per_pod  == 0 && _radix_up[TOR_TIER]/_agg_switches_per_pod > 1) {
            cerr << "Did you miss specifying a Tier 1 bundle size of " << _radix_up[TOR_TIER]/_agg_switches_per_pod << "?" << endl;
        } else if (_radix_up[TOR_TIER] % _agg_switches_per_pod  == 0
                   && _radix_up[TOR_TIER]/_agg_switches_per_pod != _bundlesize[AGG_TIER]) {
            cerr << "Tier 1 bundle size is " << _bundlesize[AGG_TIER] << ". Did you mean it to be "
                 << _radix_up[TOR_TIER]/_agg_switches_per_pod << "?" << endl;
        }
        exit(1);
    }

    if (_tiers == 3) {
        assert((no_of_tor_uplinks * _downlink_speeds[AGG_TIER]) % (_downlink_speeds[CORE_TIER] * _oversub[AGG_TIER]) == 0);
        no_of_agg_uplinks = (no_of_tor_uplinks * _downlink_speeds[AGG_TIER]) / (_downlink_speeds[CORE_TIER] * _oversub[AGG_TIER]);
        cout << "no_of_agg_uplinks: " << no_of_agg_uplinks << endl;

        assert(no_of_agg_uplinks % _radix_down[CORE_TIER] == 0);
        no_of_core_switches = no_of_agg_uplinks / _radix_down[CORE_TIER];

        if (no_of_core_switches % _agg_switches_per_pod != 0) {
            cerr << "Topology results in " << no_of_core_switches << " core switches, which isn't an integer multiple of "
                 << _agg_switches_per_pod << " aggregation switches per pod, computed from Tier 0 and 1 values\n";
            exit(1);
        }

        if ((no_of_core_switches * _bundlesize[CORE_TIER])/ _agg_switches_per_pod  != _radix_up[AGG_TIER]) {
            cerr << "Mismatch between the AGG switch up-radix of " << _radix_up[AGG_TIER] << " and calculated "
                 << _agg_switches_per_pod << " aggregation switched per pod with " << no_of_core_switches << " core switches" << endl;
            if (_bundlesize[CORE_TIER] == 1
                && _radix_up[AGG_TIER] % (no_of_core_switches/_agg_switches_per_pod) == 0
                && _radix_up[AGG_TIER] / (no_of_core_switches/_agg_switches_per_pod) > 1) {
                cerr << "Did you miss specifying a Tier 2 bundle size of "
                     << _radix_up[AGG_TIER] / (no_of_core_switches/_agg_switches_per_pod) << "?" << endl;
            } else if (_radix_up[AGG_TIER] % (no_of_core_switches/_agg_switches_per_pod) == 0
                       && _radix_up[AGG_TIER] / (no_of_core_switches/_agg_switches_per_pod) != _bundlesize[CORE_TIER]) {
                cerr << "Tier 2 bundle size is " << _bundlesize[CORE_TIER] << ". Did you mean it to be "
                     << _radix_up[AGG_TIER] /	(no_of_core_switches/_agg_switches_per_pod) << "?" << endl;
            }
            exit(1);
        }
    }

    cout << "No of nodes: " << no_of_nodes << endl;
    cout << "No of pods: " << no_of_pods << endl;
    cout << "Hosts per pod: " << _hosts_per_pod << endl;
    cout << "Hosts per pod: " << _hosts_per_pod << endl;
    cout << "ToR switches per pod: " << _tor_switches_per_pod << endl;
    cout << "Agg switches per pod: " << _agg_switches_per_pod << endl;
    cout << "No of core switches: " << no_of_core_switches << endl;
    for (uint32_t tier = TOR_TIER; tier < _tiers; tier++) {
        if (_queue_down[tier] > 0)
            cout << "Tier " << tier << " QueueSize Down " << _queue_down[tier] << " bytes" << endl;
        if (tier < CORE_TIER)
            if (_queue_up[tier] > 0)
                cout << "Tier " << tier << " QueueSize Up " << _queue_up[tier] << " bytes" << endl;
    }

    // looks like we're OK, lets build it
    NSRV = no_of_nodes;
    NTOR = _tor_switches_per_pod * no_of_pods;
    NAGG = _agg_switches_per_pod * no_of_pods;
    NPOD = no_of_pods;
    NCORE = no_of_core_switches;
}


void
XGFTTopologyCfg::set_tier_parameters(int tier, int radix_up, int radix_down, mem_b queue_up, mem_b queue_down, int bundlesize, linkspeed_bps linkspeed, int oversub) {
    if (tier < _tiers-1) {
        // no uplinks from core switches
        _radix_up[tier] = radix_up;
        _queue_up[tier] = queue_up;
    }
    _radix_down[tier] = radix_down;
    _queue_down[tier] = queue_down;
    _bundlesize[tier] = bundlesize;
    _downlink_speeds[tier] = linkspeed; // this is the link going downwards from this tier.  up/down linkspeeds are symmetric.
    _oversub[tier] = oversub;
    // xxx what to do about queue sizes
}

void XGFTTopologyCfg::set_linkspeeds(linkspeed_bps linkspeed) {
    if (linkspeed != 0 && _downlink_speeds[TOR_TIER] != 0 && linkspeed != _downlink_speeds[TOR_TIER]) {
        cerr << "Don't set linkspeeds using both the constructor and set_tier_parameters - use only one of the two\n";
        exit(1);
    }
    if (linkspeed == 0 && _downlink_speeds[TOR_TIER] == 0) {
        cerr << "Linkspeed is not set, either as a default or by constructor\n";
        exit(1);
    }
    // set tier linkspeeds if no defaults are specified
    for (int tier = TOR_TIER; tier <= _tiers-1; tier++) {
        if (_downlink_speeds[tier] == 0) { _downlink_speeds[tier] = linkspeed;}
    }
}

void XGFTTopologyCfg::set_queue_sizes(mem_b queuesize) {
    // all tiers use the same queuesize
    for (int tier = TOR_TIER; tier <= _tiers-1; tier++) {
        _queue_down[tier] = queuesize;
        if (tier < _tiers-1) {
            _queue_up[tier] = queuesize;
        }        
    }

    for (int tier = TOR_TIER; tier <= _tiers-1; tier++) {
        if (_queue_down[tier] > 0)
            cout << "Tier " << tier << " QueueSize Down " << _queue_down[tier] << " bytes" << endl;
        if (tier < _tiers-1)
            if (_queue_up[tier] > 0)
                cout << "Tier " << tier << " QueueSize Up " << _queue_up[tier] << " bytes" << endl;
    }
}


void XGFTTopologyCfg::set_params(uint32_t no_of_nodes, vector<uint32_t> no_of_children, vector<uint32_t> no_of_parent) {
    /*
    can't check _hosts_per_pod but probably can check _no_of_nodes

    if (_hosts_per_pod > 0) {
        // if we've set all the detailed parameters, we'll use them, otherwise fall through to defaults
        set_custom_params(no_of_nodes);
        return;

        to check if something is already set or not
    }*/
    
    cout << "Set params " << no_of_nodes << endl;
    cout << "Configuration array = [" << _tiers << "; ";
    for (int tier = TOR_TIER; tier <= _tiers-1; tier++) {
        cout << no_of_children[tier];
        if (tier < _tiers-1) {
            cout << ", ";
        }
    }
    cout << "; ";
    for (int tier = TOR_TIER; tier <= _tiers-1; tier++) {
        cout << no_of_parent[tier];
        if (tier < _tiers-1) {
            cout << ", ";
        }
    }
    cout << "]" <<endl;


    for (int tier = TOR_TIER; tier <= _tiers-1; tier++) {
        if (_queue_down[tier] > 0)
            cout << "Tier " << tier << " QueueSize Down " << _queue_down[tier] << " bytes" << endl;
        if (tier < _tiers-1)
            if (_queue_up[tier] > 0)
                cout << "Tier " << tier << " QueueSize Up " << _queue_up[tier] << " bytes" << endl;
    }
    
    assert(_no_of_nodes == 0);

    _no_of_nodes = 1;
    for (int tier = TOR_TIER; tier <= _tiers-1; tier++) {
        _no_of_nodes *= no_of_children[tier];
    }

    if (_no_of_nodes != no_of_nodes) {
        cerr << "Topology Error: can't have a XGFT with " << no_of_nodes
                << " nodes with that specific configuration array\n";
        exit(1);
    }

    NSRV = _no_of_nodes;
    
    NSW.resize(_tiers, 0);

    for (int i = 1; i <= _tiers; i++) {
        W[i] = W[i - 1] * no_of_parent[i - 1];
    }
    
    for (int tier = TOR_TIER; tier <= _tiers-1; tier++) {
        no_of_nodes = (no_of_nodes / no_of_children[tier]) * no_of_parent[tier];
        NSW[tier] = no_of_nodes;

        _radix_down[tier] = no_of_children[tier];

        if (tier < _tiers-1) {
            _radix_up[tier] = no_of_parent[tier + 1];
        }
    }
    
    cout << "_no_of_nodes " << _no_of_nodes << endl;
    cout << "Queue type " << _qt << endl;
}

simtime_picosec XGFTTopologyCfg::get_two_point_diameter_latency(int src, int dst) {
    simtime_picosec diameter_latency_end_point = 0;
    simtime_picosec one_hop_delay = 0;
    if(_link_latencies[TOR_TIER] == 0){
        one_hop_delay = 2* (_hop_latency + _switch_latency);
    }
    if (_tiers == 2) {
        if (HOST_POD_SWITCH(src) != HOST_POD_SWITCH(dst)) {
            diameter_latency_end_point = _diameter_latency;
        } else {
            if(_link_latencies[TOR_TIER] == 0){
                diameter_latency_end_point = one_hop_delay;
            }else{
                diameter_latency_end_point = 2 * _link_latencies[TOR_TIER] + _switch_latencies[TOR_TIER];
            }
        }
    }else if (_tiers == 3) {
        if (HOST_POD_SWITCH(src) == HOST_POD_SWITCH(dst)) {
            if(_link_latencies[TOR_TIER] == 0){
                diameter_latency_end_point = one_hop_delay;
            }else{
                diameter_latency_end_point = 2 * _link_latencies[TOR_TIER] + _switch_latencies[TOR_TIER];
            }
        } else if (HOST_POD(src) == HOST_POD(dst)) {
            if (_link_latencies[TOR_TIER] == 0){
                diameter_latency_end_point = 2*one_hop_delay;
            }else{
                diameter_latency_end_point = 2 * _link_latencies[TOR_TIER] + 2 * _switch_latencies[TOR_TIER] +
                                             2 * _link_latencies[AGG_TIER] + _switch_latencies[AGG_TIER];
            }
        } else {
            diameter_latency_end_point = _diameter_latency;
        }
    }
    // cout << " _tiers " << _tiers <<  " HOST_POD_SWITCH src " << HOST_POD_SWITCH(src) << " dst " << HOST_POD_SWITCH(dst) << " diameter_latency_end_point " << diameter_latency_end_point<< endl;

    return diameter_latency_end_point;
}

unique_ptr<XGFTTopologyCfg> XGFTTopologyCfg::load(string filename,
                                                        mem_b queuesize,
                                                        queue_type q_type,
                                                        queue_type sender_q_type) {
    std::ifstream file(filename);
    if (file.is_open()) {
        unique_ptr<XGFTTopologyCfg> cfg = make_unique<XGFTTopologyCfg>(file, queuesize, q_type, sender_q_type);
        cout << "XGFTCfg constructor done." << endl;

        file.close();
        return cfg;
    } else {
        cerr << "Failed to open XGFT config file " << filename << endl;
        exit(1);
    }
}

void XGFTTopologyCfg::read_cfg(istream& file, mem_b queuesize) {
    //cout << "topo load start\n";
    std::string line;
    int linecount = 0;
    _tiers = 0;
    _hosts_per_pod = 0;
    for (int tier = 0; tier < 3; tier++) {
        _queue_down[tier] = queuesize;
        if (tier != 2)
            _queue_up[tier] = queuesize;
    }

    while (std::getline(file, line)) {
        linecount++;
        vector<string> tokens;
        tokenize(line, ' ', tokens);
        if (tokens.size() == 0)
            continue;
        if (tokens[0][0] == '#') {
            continue;
        }
        to_lower(tokens[0]);
        if (tokens[0] == "nodes") {
            _no_of_nodes = stoi(tokens[1]);
        } else if (tokens[0] == "tiers") {
            _tiers = stoi(tokens[1]);
        } else if (tokens[0] == "podsize") {
            _hosts_per_pod = stoi(tokens[1]);
        } else if (tokens[0] == "tier") {
            // we're done with the header
            break;
        }
    }
    if (_no_of_nodes == 0) {
        cerr << "Missing number of nodes in header" << endl;
        exit(1);
    }
    if (_tiers == 0) {
        cerr << "Missing number of tiers in header" << endl;
        exit(1);
    }
    if (_tiers < 2 || _tiers > 3) {
        cerr << "Invalid number of tiers: " << _tiers << endl;
        exit(1);
    }
    if (_hosts_per_pod == 0) {
        cerr << "Missing pod size in header" << endl;
        exit(1);
    }
    linecount--;
    bool tiers_done[3] = {false, false, false};
    int current_tier = -1;
    do {
        linecount++;
        vector<string> tokens;
        tokenize(line, ' ', tokens);
        if (tokens.size() < 1) {
            continue;
    	}
        to_lower(tokens[0]);
        if (tokens.size() == 0 || tokens[0][0] == '#') {
            continue;
        } else if (tokens[0] == "tier") {
            current_tier = stoi(tokens[1]);
            if (current_tier < 0 || current_tier > 2) {
                cerr << "Invalid tier " << current_tier << " at line " << linecount << endl;
                exit(1);
            }
            tiers_done[current_tier] = true;
        } else if (tokens[0] == "downlink_speed_gbps") {
            if (_downlink_speeds[current_tier] != 0) {
                cerr << "Duplicate linkspeed setting for tier " << current_tier << " at line " << linecount << endl;
                exit(1);
            }
            _downlink_speeds[current_tier] = ((linkspeed_bps)stoi(tokens[1])) * 1000000000;
        } else if (tokens[0] == "radix_up") {
            if (_radix_up[current_tier] != 0) {
                cerr << "Duplicate radix_up setting for tier " << current_tier << " at line " << linecount << endl;
                exit(1);
            }
            if (current_tier == 2) {
                cerr << "Can't specific radix_up for tier " << current_tier << " at line " << linecount << " (no uplinks from top tier!)" << endl;
                exit(1);
            }
            _radix_up[current_tier] = stoi(tokens[1]);
        } else if (tokens[0] == "radix_down") {
            if (_radix_down[current_tier] != 0) {
                cerr << "Duplicate radix_down setting for tier " << current_tier << " at line " << linecount << endl;
                exit(1);
            }
            _radix_down[current_tier] = stoi(tokens[1]);
        } else if (tokens[0] == "queue_up") {
            if (_queue_up[current_tier] != 0) {
                cerr << "Duplicate queue_up setting for tier " << current_tier << " at line " << linecount << endl;
                exit(1);
            }
            if (current_tier == 2) {
                cerr << "Can't specific queue_up for tier " << current_tier << " at line " << linecount << " (no uplinks from top tier!)" << endl;
                exit(1);
            }
            _queue_up[current_tier] = stoi(tokens[1]);
        } else if (tokens[0] == "queue_down") {
            if (_queue_down[current_tier] != 0) {
                cerr << "Duplicate queue_down setting for tier " << current_tier << " at line " << linecount << endl;
                exit(1);
            }
            _queue_down[current_tier] = stoi(tokens[1]);
        } else if (tokens[0] == "oversubscribed") {
            if (_oversub[current_tier] != 1) {
                cerr << "Duplicate oversubscribed setting for tier " << current_tier << " at line " << linecount << endl;
                exit(1);
            }
            _oversub[current_tier] = stoi(tokens[1]); 
        } else if (tokens[0] == "bundle") {
            if (_bundlesize[current_tier] != 1) {
                cerr << "Duplicate bundle size setting for tier " << current_tier << " at line " << linecount << endl;
                exit(1);
            }
            _bundlesize[current_tier] = stoi(tokens[1]); 
        } else if (tokens[0] == "switch_latency_ns") {
            if (_switch_latencies[current_tier] != 0) {
                cerr << "Duplicate switch_latency setting for tier " << current_tier << " at line " << linecount << endl;
                exit(1);
            }
            _switch_latencies[current_tier] = timeFromNs(stoi(tokens[1])); 
        } else if (tokens[0] == "downlink_latency_ns") {
            if (_link_latencies[current_tier] != 0) {
                cerr << "Duplicate link latency setting for tier " << current_tier << " at line " << linecount << endl;
                exit(1);
            }
            _link_latencies[current_tier] = timeFromNs(stoi(tokens[1])); 
        } else {
            cerr << "Error: Unknown attribute " << tokens[0] << " at line " << linecount << endl;
            cerr << "Allowed attributes are: tier, downlink_speed_gbps, radix_up, radix_down, queue_up, queue_down, oversubscribed, bundle, switch_latency_ns, downlink_latency_ns" << endl;
            exit(1);
        }
    } while (std::getline(file, line));

    for (uint32_t tier = 0; tier < _tiers; tier++) {
        if (tiers_done[tier] == false) {
            cerr << "No configuration found for tier " << tier << endl;
            exit(1);
        }
    }

    cout << "Topology load done\n";
}


void XGFTTopologyCfg::check_consistency() const {

    if (_no_of_nodes == 0) {
        cerr << "Missing number of nodes" << endl;
        exit(1);
    }
    if (_tiers == 0) {
        cerr << "Missing number of tiers" << endl;
        exit(1);
    }

    for (uint32_t tier = 0; tier < _tiers; tier++) {
        if (_downlink_speeds[tier] == 0) {
            cerr << "Missing downlink_speed_gbps for tier " << tier << endl;
            exit(1);
        }
        if (_link_latencies[tier] == 0) {
            cerr << "Missing downlink_latency_ns for tier " << tier << endl;
            exit(1);
        }
        if (tier < (_tiers - 1) && _radix_up[tier] == 0) {
            cerr << "Missing radix_up for tier " << tier << endl;
            exit(1);
        }
        if (_radix_down[tier] == 0) {
            cerr << "Missing radix_down for tier " << tier << endl;
            exit(1);
        }
        if (tier < (_tiers - 1) && _queue_up[tier] == 0) {
            cerr << "Missing queue_up for tier " << tier << endl;
            exit(1);
        }
        if (_queue_down[tier] == 0) {
            cerr << "Missing queue_down for tier " << tier << endl;
            exit(1);
        }
    }
}


XGFTTopology::XGFTTopology(const XGFTTopologyCfg* cfg,
                                QueueLoggerFactory* logger_factory,
                                EventList* ev,
                                FirstFit * fit
                                ):
                                _logger_factory(logger_factory),
                                _eventlist(ev),
                                _ff(fit),
                                _cfg(cfg)
                                {
    // Only build topology after verifying that things are in order.
    if (_cfg->_from_file) {
        _cfg->check_consistency();
    }
    alloc_vectors();

    QueueLogger* queueLogger;

    for (int tier = _cfg->CORE_TIER; tier >= 0; tier--) {
        for (uint32_t j=0;j<_cfg->NSW[tier];j++) {
            uint32_t down_children = (tier == 0) ? _cfg->NSRV : _cfg->NSW[tier - 1];
            for (uint32_t k=0;k<down_children;k++) {
                for (uint32_t b = 0; b < _cfg->_bundlesize[tier]; b++) {
                    queues_down[tier][j][k][b] = NULL;
                    pipes_down[tier][j][k][b] = NULL;
                    // is not tier-1 because in queues_up and pipes_up 0 is not Tor but host
                    queues_up[tier][k][j][b] = NULL;
                    pipes_up[tier][k][j][b] = NULL;
                }
            }
        }
    }

    for (int tier = _cfg->CORE_TIER; tier >= 0; tier--){
        simtime_picosec switch_latency = (_cfg->_switch_latencies[tier] > 0) ? _cfg->_switch_latencies[tier] : _cfg->_switch_latency;
        for (uint32_t j=0;j<_cfg->NSW[tier];j++){
            if (tier == 0){
                switches[tier][j]  = new XGFTSwitch(*_eventlist, "Switch_LowerPod_"+ntoa(j),XGFTSwitch::TOR,j,switch_latency,this);
            } else if (tier == _cfg->CORE_TIER){
                switches[tier][j]  = new XGFTSwitch(*_eventlist, "Switch_Core_"+ntoa(j), XGFTSwitch::CORE,j,switch_latency,this);
            } else {
                switches[tier][j]  = new XGFTSwitch(*_eventlist, "Switch_UpperPod" + ntoa(tier) + "_"+ntoa(j), XGFTSwitch::AGG,j,switch_latency,this);
            } 
        }
    }
      
    // Tor->Host / Host->Tor
    for (uint32_t tor = 0; tor < _cfg->NSW[TOR_TIER]; tor++) {
        uint32_t link_bundles = _cfg->_radix_down[TOR_TIER]/_cfg->_bundlesize[TOR_TIER];
        for (uint32_t l = 0; l < link_bundles; l++) {
            uint32_t srv = tor * link_bundles + l;
            for (uint32_t b = 0; b < _cfg->_bundlesize[TOR_TIER]; b++) {
                // Downlink
                if (_logger_factory) {
                    queueLogger = _logger_factory->createQueueLogger();
                } else {
                    queueLogger = NULL;
                }
            
                queues_down[TOR_TIER][tor][srv][b] = alloc_queue(queueLogger, _cfg->_queue_down[TOR_TIER], DOWNLINK, TOR_TIER, true);
                queues_down[TOR_TIER][tor][srv][b]->setName("LS" + ntoa(tor) + "->DST" +ntoa(srv) + "(" + ntoa(b) + ")");
                //if (logfile) logfile->writeName(*(queues_nlp_ns[tor][srv]));
                simtime_picosec hop_latency = (_cfg->_hop_latency == 0) ? _cfg->_link_latencies[TOR_TIER] : _cfg->_hop_latency;
                pipes_down[TOR_TIER][tor][srv][b] = new Pipe(hop_latency, *_eventlist);
                pipes_down[TOR_TIER][tor][srv][b]->setName("Pipe-LS" + ntoa(tor)  + "->DST" + ntoa(srv) + "(" + ntoa(b) + ")");
                //if (logfile) logfile->writeName(*(pipes_nlp_ns[tor][srv]));
            
                // Uplink
                if (_logger_factory) {
                    queueLogger = _logger_factory->createQueueLogger();
                } else {
                    queueLogger = NULL;
                }
                queues_up[TOR_TIER][srv][tor][b] = alloc_src_queue(queueLogger);   
                queues_up[TOR_TIER][srv][tor][b]->setName("SRC" + ntoa(srv) + "->LS" +ntoa(tor) + "(" + ntoa(b) + ")");
                //cout << queues_up[TOR_TIER][srv][tor][b]->str() << endl;
                //if (logfile) logfile->writeName(*(queues_up[TOR_TIER][srv][tor]));

                queues_up[TOR_TIER][srv][tor][b]->setRemoteEndpoint(switches[TOR_TIER][tor]);

                assert(switches[TOR_TIER][tor]->addPort(queues_down[TOR_TIER][tor][srv][b]) < 96);

                if (cfg->_qt==LOSSLESS_INPUT || cfg->_qt == LOSSLESS_INPUT_ECN){
                    //no virtual queue needed at server
                    new LosslessInputQueue(*_eventlist, queues_up[TOR_TIER][srv][tor][b], switches[TOR_TIER][tor], hop_latency);
                }
        
                pipes_up[TOR_TIER][srv][tor][b] = new Pipe(hop_latency, *_eventlist);
                pipes_up[TOR_TIER][srv][tor][b]->setName("Pipe-SRC" + ntoa(srv) + "->LS" + ntoa(tor) + "(" + ntoa(b) + ")");
                //if (logfile) logfile->writeName(*(pipes_ns_nlp[srv][tor]));
            
                if (_ff){
                    _ff->add_queue(queues_down[TOR_TIER][tor][srv][b]);
                    _ff->add_queue(queues_up[TOR_TIER][srv][tor][b]);
                }
            }
        }
    }

    //Tor->Agg / Agg->Tor
    if (_cfg->_tiers >= 2){
        for (uint32_t tor = 0; tor < _cfg->NSW[TOR_TIER]; tor++) {
            uint32_t base = _cfg->base_parent(tor, TOR_TIER);
            for (uint32_t y=0; y < _cfg->_radix_up[TOR_TIER]; y++){
                for (uint32_t b = 0; b < _cfg->_bundlesize[TOR_TIER + 1]; b++) {
                    // Downlink
                    if (_logger_factory) {
                        queueLogger = _logger_factory->createQueueLogger();
                    } else {
                        queueLogger = NULL;
                    }
                    uint32_t agg = base + y;

                    if (_cfg->_tiers == 2 && (agg - base) < _cfg->_num_failed_links){
                        queues_down[TOR_TIER + 1][agg][tor][b] = alloc_queue(queueLogger, _cfg->_downlink_speeds[TOR_TIER + 1],_cfg->_queue_down[TOR_TIER + 1], DOWNLINK, TOR_TIER + 1,false,true);
                        cout << "Failure: US" + ntoa(TOR_TIER + 1)+ "_" + ntoa(agg) + "->LS" + ntoa(tor) + "(" + ntoa(b) + ") linkspeed set to " << speedAsGbps(_cfg->_downlink_speeds[TOR_TIER + 1] * _cfg->_failed_link_ratio) << endl;
                    }
                    else
                        queues_down[TOR_TIER + 1][agg][tor][b] = alloc_queue((QueueLogger*)queueLogger, (const mem_b)_cfg->_queue_down[TOR_TIER + 1], DOWNLINK, TOR_TIER + 1);

                    queues_down[TOR_TIER + 1][agg][tor][b]->setName("US" + ntoa(TOR_TIER + 1)+ "_" + ntoa(agg) + "->LS" + ntoa(tor) + "(" + ntoa(b) + ")");
                    //if (logfile) logfile->writeName(*(queues_down[TOR_TIER + 1][agg][tor]));
                
                    simtime_picosec hop_latency = (_cfg->_hop_latency == 0) ? _cfg->_link_latencies[TOR_TIER + 1] : _cfg->_hop_latency;
                    pipes_down[TOR_TIER + 1][agg][tor][b] = new Pipe(hop_latency, *_eventlist);
                    pipes_down[TOR_TIER + 1][agg][tor][b]->setName("Pipe-US" + ntoa(TOR_TIER + 1)+ "_" + ntoa(agg) + "->LS" + ntoa(tor) + "(" + ntoa(b) + ")");
                    //if (logfile) logfile->writeName(*(pipes_down[TOR_TIER + 1][agg][tor]));
                
                    // Uplink
                    if (_logger_factory) {
                        queueLogger = _logger_factory->createQueueLogger();
                    } else {
                        queueLogger = NULL;
                    }

                    if (_cfg->_tiers == 2 && (agg - base) < _cfg->_num_failed_links){
                        queues_up[TOR_TIER][tor][agg][b] = alloc_queue(queueLogger, _cfg->_downlink_speeds[TOR_TIER + 1], _cfg->_queue_up[TOR_TIER], UPLINK, TOR_TIER, true, true);
                        cout << "Failure: LS" + ntoa(tor) + "->US" + ntoa(TOR_TIER + 1)+ "_" + ntoa(agg) + "(" + ntoa(b) + ") linkspeed set to " << speedAsGbps(_cfg->_downlink_speeds[TOR_TIER + 1] * _cfg->_failed_link_ratio) << endl;
                    }
                    else 
                        queues_up[TOR_TIER][tor][agg][b] = alloc_queue(queueLogger, _cfg->_queue_up[TOR_TIER], UPLINK, TOR_TIER, true);

                    queues_up[TOR_TIER][tor][agg][b]->setName("LS" + ntoa(tor) + "->US" + ntoa(TOR_TIER + 1)+ "_" + ntoa(agg) + "(" + ntoa(b) + ")");
                    //cout << queues_up[TOR_TIER][tor][agg][b]->str() << endl;
                    //if (logfile) logfile->writeName(*(queues_up[TOR_TIER][tor][agg]));

                    assert(switches[TOR_TIER][tor]->addPort(queues_up[TOR_TIER][tor][agg][b]) < 128);
                    assert(switches[TOR_TIER+1][agg]->addPort(queues_down[TOR_TIER + 1][agg][tor][b]) < 128);
                    queues_up[TOR_TIER][tor][agg][b]->setRemoteEndpoint(switches[TOR_TIER+1][agg]);
                    queues_down[TOR_TIER + 1][agg][tor][b]->setRemoteEndpoint(switches[TOR_TIER][tor]);

                    /*if (_qt==LOSSLESS){
                    ((LosslessQueue*)queues_up[TOR_TIER][tor][agg])->setRemoteEndpoint(queues_down[TOR_TIER + 1][agg][tor]);
                    ((LosslessQueue*)queues_down[TOR_TIER + 1][agg][tor])->setRemoteEndpoint(queues_up[TOR_TIER][tor][agg]);
                    }else */
                    if (_cfg->_qt==LOSSLESS_INPUT || _cfg->_qt == LOSSLESS_INPUT_ECN){            
                        new LosslessInputQueue(*_eventlist, queues_up[TOR_TIER][tor][agg][b],switches[TOR_TIER+1][agg], hop_latency);
                        new LosslessInputQueue(*_eventlist, queues_down[TOR_TIER + 1][agg][tor][b],switches[TOR_TIER][tor], hop_latency);
                    }
            
                    pipes_up[TOR_TIER][tor][agg][b] = new Pipe(hop_latency, *_eventlist);
                    pipes_up[TOR_TIER][tor][agg][b]->setName("Pipe-LS" + ntoa(tor) + "->US" + ntoa(TOR_TIER + 1)+ "_" + ntoa(agg) + "(" + ntoa(b) + ")");
                    //if (logfile) logfile->writeName(*(pipes_up[TOR_TIER][tor][agg]));
            
                    if (_ff){
                        _ff->add_queue(queues_up[TOR_TIER][tor][agg][b]);
                        _ff->add_queue(queues_down[TOR_TIER + 1][agg][tor][b]);
                    }
                }
            }
        }

        //Agg->Agg
        for (int tier = TOR_TIER + 1; tier < _cfg->LAST_AGG_TIER; tier++){
            for (uint32_t low = 0; low < _cfg->NSW[tier]; low++) {
                uint32_t base = _cfg->base_parent(low, tier);
                for (uint32_t y=0; y < _cfg->_radix_up[tier]; y++){
                    for (uint32_t b = 0; b < _cfg->_bundlesize[tier + 1]; b++) {
                        // Downlink
                        if (_logger_factory) {
                            queueLogger = _logger_factory->createQueueLogger();
                        } else {
                            queueLogger = NULL;
                        }
                        uint32_t up = base + y;

                        queues_down[tier + 1][up][low][b] = alloc_queue((QueueLogger*)queueLogger, (const mem_b)_cfg->_queue_down[tier + 1], DOWNLINK, tier + 1);

                        queues_down[tier + 1][up][low][b]->setName("US" + ntoa(tier + 1)+ "_" + ntoa(up) + "->US" + ntoa(tier)+ "_" + ntoa(low) + "(" + ntoa(b) + ")");
                        //if (logfile) logfile->writeName(*(queues_down[tier + 1][up][low]));
                    
                        simtime_picosec hop_latency = (_cfg->_hop_latency == 0) ? _cfg->_link_latencies[tier + 1] : _cfg->_hop_latency;
                        pipes_down[tier + 1][up][low][b] = new Pipe(hop_latency, *_eventlist);
                        pipes_down[tier + 1][up][low][b]->setName("Pipe-US" + ntoa(tier + 1)+ "_" + ntoa(up) + "->US" + ntoa(tier)+ "_" + ntoa(low) + "(" + ntoa(b) + ")");
                        //if (logfile) logfile->writeName(*(pipes_down[tier + 1][up][low]));
                    
                        // Uplink
                        if (_logger_factory) {
                            queueLogger = _logger_factory->createQueueLogger();
                        } else {
                            queueLogger = NULL;
                        }

                        queues_up[tier][low][up][b] = alloc_queue(queueLogger, _cfg->_queue_up[tier], UPLINK, tier, true);

                        queues_up[tier][low][up][b]->setName("US" + ntoa(tier)+ "_" + ntoa(low) + "->US" + ntoa(tier + 1)+ "_" + ntoa(up) + "(" + ntoa(b) + ")");
                        //cout << queues_up[tier][low][up][b]->str() << endl;
                        //if (logfile) logfile->writeName(*(queues_up[tier][low][up]));

                        assert(switches[tier][low]->addPort(queues_up[tier][low][up][b]) < 128);
                        assert(switches[tier+1][up]->addPort(queues_down[tier + 1][up][low][b]) < 128);
                        queues_up[tier][low][up][b]->setRemoteEndpoint(switches[tier+1][up]);
                        queues_down[tier + 1][up][low][b]->setRemoteEndpoint(switches[tier][low]);

                        /*if (_qt==LOSSLESS){
                        ((LosslessQueue*)queues_up[tier][low][up])->setRemoteEndpoint(queues_down[tier + 1][up][low]);
                        ((LosslessQueue*)queues_down[tier + 1][up][low])->setRemoteEndpoint(queues_up[tier][low][up]);
                        }else */
                        if (_cfg->_qt==LOSSLESS_INPUT || _cfg->_qt == LOSSLESS_INPUT_ECN){            
                            new LosslessInputQueue(*_eventlist, queues_up[tier][low][up][b],switches[tier+1][up], hop_latency);
                            new LosslessInputQueue(*_eventlist, queues_down[tier + 1][up][low][b],switches[tier][low], hop_latency);
                        }
                
                        pipes_up[tier][low][up][b] = new Pipe(hop_latency, *_eventlist);
                        pipes_up[tier][low][up][b]->setName("Pipe-US" + ntoa(tier)+ "_" + ntoa(low) + "->US" + ntoa(tier + 1)+ "_" + ntoa(up) + "(" + ntoa(b) + ")");
                        //if (logfile) logfile->writeName(*(pipes_up[tier][low][up]));
                
                        if (_ff){
                            _ff->add_queue(queues_up[tier][low][up][b]);
                            _ff->add_queue(queues_down[tier + 1][up][low][b]);
                        }
                    }
                }
            }
        }

        //Agg->Core
        if (_cfg->_tiers >= 3){
            uint32_t last = _cfg->LAST_AGG_TIER;
            for (uint32_t agg = 0; agg < _cfg->NSW[last]; agg++) {
                uint32_t base = _cfg->base_parent(agg, last);
                for (uint32_t y=0; y < _cfg->_radix_up[last]; y++){
                    for (uint32_t b = 0; b < _cfg->_bundlesize[last + 1]; b++) {
                        // Downlink
                        if (_logger_factory) {
                            queueLogger = _logger_factory->createQueueLogger();
                        } else {
                            queueLogger = NULL;
                        }

                        uint32_t core = base + y;
                        assert(queues_up[last][agg][core][b] == NULL);
                        queues_up[last][agg][core][b] = alloc_queue(queueLogger, _cfg->_queue_up[last], UPLINK, last);
                        queues_up[last][agg][core][b]->setName("US" + ntoa(last)+ "_" + ntoa(agg) + "->CS" + ntoa(core) + "(" + ntoa(b) + ")");
                        //cout << queue_up[last][agg][core][b]->str() << endl;
                        //if (logfile) logfile->writeName(*(queues_up[last][agg][core]));
            
                        simtime_picosec hop_latency = (_cfg->_hop_latency == 0) ? _cfg->_link_latencies[last + 1] : _cfg->_hop_latency;
                        pipes_up[last][agg][core][b] = new Pipe(hop_latency, *_eventlist);
                        pipes_up[last][agg][core][b]->setName("Pipe-US" + ntoa(last)+ "_" + ntoa(agg) + "->CS" + ntoa(core) + "(" + ntoa(b) + ")");
                        //if (logfile) logfile->writeName(*(pipes_up[last][agg][core]));
            
                        // Uplink
                        if (_logger_factory) {
                            queueLogger = _logger_factory->createQueueLogger();
                        } else {
                            queueLogger = NULL;
                        }
            
                        queues_down[last + 1][core][agg][b] = alloc_queue(queueLogger, _cfg->_queue_down[last + 1], DOWNLINK, last + 1);
                        
            
                        queues_down[last + 1][core][agg][b]->setName("CS" + ntoa(core) + "->US" + ntoa(last)+ "_" + ntoa(agg) + "(" + ntoa(b) + ")");

                        assert(switches[last][agg]->addPort(queues_up[last][agg][core][b]) < 64);
                        assert(switches[last+1][core]->addPort(queues_down[last + 1][core][agg][b]) < 64);
                        queues_up[last][agg][core][b]->setRemoteEndpoint(switches[last+1][core]);
                        queues_down[last + 1][core][agg][b]->setRemoteEndpoint(switches[last][agg]);

                        /*if (_qt==LOSSLESS){
                        ((LosslessQueue*)queues_up[last][agg][core])->setRemoteEndpoint(queues_down[last + 1][core][agg]);
                        ((LosslessQueue*)queues_down[last + 1][core][agg])->setRemoteEndpoint(queues_up[last][agg][core]);
                        }
                        else*/
                        if (_cfg->_qt == LOSSLESS_INPUT || _cfg->_qt == LOSSLESS_INPUT_ECN){
                            new LosslessInputQueue(*_eventlist, queues_up[last][agg][core][b], switches[last+1][core], hop_latency);
                            new LosslessInputQueue(*_eventlist, queues_down[last + 1][core][agg][b], switches[last][agg], hop_latency);
                        }
                        //if (logfile) logfile->writeName(*(queues_down[last + 1][core][agg]));
                
                        pipes_down[last + 1][core][agg][b] = new Pipe(hop_latency, *_eventlist);
                        pipes_down[last + 1][core][agg][b]->setName("Pipe-CS" + ntoa(core) + "->US" + ntoa(last)+ "_" + ntoa(agg) + "(" + ntoa(b) + ")");
                        //if (logfile) logfile->writeName(*(pipes_down[last + 1][core][agg]));
                
                        if (_ff){
                            _ff->add_queue(queues_up[last][agg][core][b]);
                            _ff->add_queue(queues_down[last + 1][core][agg][b]);
                        }
                    }
                }
            }
        }
    }
    
    //init thresholds for lossless operation
    if (_cfg->_qt==LOSSLESS) {
        for (size_t tier = 0; tier < _cfg->_tiers; tier++) {
            for (uint32_t j=0;j<_cfg->NSW[tier];j++){
                switches[tier][j]->configureLossless();
            }
        }
    }
}

template<class P> void delete_4d_vector(vector<vector<vector<vector<P*>>>>& vec4d) {
    for (auto& vec1: vec4d) {
        for (auto& vec2: vec1) {
            for (auto& vec3: vec2) {
                for (auto* pipe: vec3) {
                    delete pipe;
                }
            }
        }
    }
    vec4d.clear();
}

XGFTTopology::~XGFTTopology() {
    for (auto& vec1: switches) {
        for (auto* swc: vec1) {
            delete swc;
        }
    }
    switches.clear();

    delete_4d_vector(pipes_down);
    delete_4d_vector(queues_down);

    delete_4d_vector(pipes_up);
    delete_4d_vector(queues_up);
}

void XGFTTopology::alloc_vectors() {

    // These vectors are sparse - we won't use all the entries
    switches.resize(_cfg->_tiers);
    pipes_down.resize(_cfg->_tiers);
    queues_down.resize(_cfg->_tiers);
    pipes_up.resize(_cfg->_tiers);
    queues_up.resize(_cfg->_tiers);
    for (size_t tier = 0; tier < _cfg->_tiers; tier++) {
        switches[tier] = vector<Switch*>(_cfg->NSW[tier], nullptr);

        // down
        size_t down_children = (tier == 0) ? _cfg->NSRV : _cfg->NSW[tier - 1];
        pipes_down[tier].resize(_cfg->NSW[tier]);
        queues_down[tier].resize(_cfg->NSW[tier]);

        for (size_t i = 0; i < _cfg->NSW[tier]; i++) {
            pipes_down[tier][i]  = vector<vector<Pipe*>>(down_children, vector<Pipe*>(_cfg->_bundlesize[tier], nullptr));
            queues_down[tier][i] = vector<vector<BaseQueue*>>(down_children, vector<BaseQueue*>(_cfg->_bundlesize[tier], nullptr));
        }

        // up
        pipes_up[tier].resize(down_children);
        queues_up[tier].resize(down_children);

        for (size_t i = 0; i < down_children; i++) {
            pipes_up[tier][i] = vector<vector<Pipe*>>(_cfg->NSW[tier],vector<Pipe*>(_cfg->_bundlesize[tier], nullptr));
            queues_up[tier][i] = vector<vector<BaseQueue*>>(_cfg->NSW[tier],vector<BaseQueue*>(_cfg->_bundlesize[tier], nullptr)); 
        }
    }
}

BaseQueue* XGFTTopology::alloc_src_queue(QueueLogger* queueLogger){
    linkspeed_bps linkspeed = _cfg->_downlink_speeds[TOR_TIER]; // linkspeeds are symmetric
    switch (_cfg->_sender_qt) {
    case SWIFT_SCHEDULER:
        return new FairScheduler(linkspeed, *_eventlist, queueLogger);
    case PRIORITY:
        return new PriorityQueue(linkspeed,
                                 memFromPkt(FEEDER_BUFFER), *_eventlist, queueLogger);
    case FAIR_PRIO:
        return new FairPriorityQueue(linkspeed,
                                     memFromPkt(FEEDER_BUFFER), *_eventlist, queueLogger);
    default:
        abort();
    }
}

BaseQueue* XGFTTopology::alloc_queue(QueueLogger* queueLogger, const mem_b queuesize,
                                        link_direction dir, int switch_tier, bool tor){
    if (dir == UPLINK) {
        switch_tier++; // _downlink_speeds is set for the downlinks, so uplinks need to use the tier above's linkspeed
    }
    return alloc_queue(queueLogger, _cfg->_downlink_speeds[switch_tier], queuesize, dir, switch_tier, tor, false);
}

BaseQueue*
XGFTTopology::alloc_queue(QueueLogger* queueLogger, linkspeed_bps speed, const mem_b queuesize_param,
                             link_direction dir, int switch_tier, bool tor, bool reduced_speed){

    mem_b queuesize = queuesize_param;
    
    if (reduced_speed){
        speed = speed * _cfg->_failed_link_ratio;
        queuesize = queuesize * _cfg->_failed_link_ratio;
    }

    switch (_cfg->_qt) {
    case RANDOM:
        return new RandomQueue(speed, queuesize, *_eventlist, queueLogger, memFromPkt(RANDOM_BUFFER));
    case COMPOSITE:
        {
            CompositeQueue* q = new CompositeQueue(speed, queuesize, *_eventlist, queueLogger,
                                                   XGFTSwitch::_trim_size, XGFTSwitch::_disable_trim);

            if (_cfg->_enable_ecn){
                if (!tor || dir == UPLINK || _cfg->_enable_ecn_on_tor_downlink) {
                        // don't use ECN on ToR downlinks unless configured so.
                        if (reduced_speed)
                            q->set_ecn_thresholds(_cfg->_ecn_low * _cfg->_failed_link_ratio, _cfg->_ecn_high * _cfg->_failed_link_ratio);
                        else
                            q->set_ecn_thresholds(_cfg->_ecn_low, _cfg->_ecn_high);
                }
            }
            return q;
        }
    case CTRL_PRIO:
        return new CtrlPrioQueue(speed, queuesize, *_eventlist, queueLogger);
    case AEOLUS:
        return new AeolusQueue(speed, queuesize, XGFTSwitch::_speculative_threshold_fraction * queuesize,  *_eventlist, queueLogger);
    case AEOLUS_ECN:
        {
            AeolusQueue* q = new AeolusQueue(speed, queuesize, XGFTSwitch::_speculative_threshold_fraction * queuesize ,  *_eventlist, queueLogger);
            if (!tor || dir == UPLINK || _cfg->_enable_ecn_on_tor_downlink) {
                // don't use ECN on ToR downlinks unless configured so.
                q->set_ecn_threshold(XGFTSwitch::_ecn_threshold_fraction * queuesize);
            }
            return q;
        }
    case ECN:
        return new ECNQueue(speed, queuesize, *_eventlist, queueLogger, memFromPkt(15));
    case ECN_PRIO:
        return new ECNPrioQueue(speed, queuesize, queuesize,
                                XGFTSwitch::_ecn_threshold_fraction * queuesize,
                                XGFTSwitch::_ecn_threshold_fraction * queuesize,
                                *_eventlist, queueLogger);
    case LOSSLESS:
        return new LosslessQueue(speed, queuesize, *_eventlist, queueLogger, NULL);
    case LOSSLESS_INPUT:
        return new LosslessOutputQueue(speed, queuesize, *_eventlist, queueLogger);
    case LOSSLESS_INPUT_ECN: 
        return new LosslessOutputQueue(speed, memFromPkt(10000), *_eventlist, queueLogger);
    case COMPOSITE_ECN:
        if (tor && dir == DOWNLINK) 
            return new CompositeQueue(speed, queuesize, *_eventlist, queueLogger, 
                                      XGFTSwitch::_trim_size, XGFTSwitch::_disable_trim);
        else
            return new ECNQueue(speed, memFromPkt(2*SWITCH_BUFFER), *_eventlist, queueLogger, memFromPkt(15));
    case COMPOSITE_ECN_LB:
        {
            CompositeQueue* q = new CompositeQueue(speed, queuesize, *_eventlist, queueLogger,
                                                   XGFTSwitch::_trim_size, XGFTSwitch::_disable_trim);
            if (!tor || dir == UPLINK || _cfg->_enable_ecn_on_tor_downlink) {
                // don't use ECN on ToR downlinks unless configured so.
                q->set_ecn_threshold(XGFTSwitch::_ecn_threshold_fraction * queuesize);
            }
            return q;
        }
    default:
        abort();
    }
}


void XGFTTopology::add_failed_link(uint32_t tier, uint32_t type, uint32_t switch_id, uint32_t link_id){
    assert(type == XGFTSwitch::AGG);
    assert(link_id < _cfg->_radix_up[tier]);
    assert(switch_id < _cfg->NSW[tier]);
    
    uint32_t podpos = switch_id%(_cfg->_agg_switches_per_pod);
    uint32_t k = podpos * _cfg->_agg_switches_per_pod + link_id;

    // note: if bundlesize > 1, we only fail the first link in a bundle.
    // TODO need to find the rigth k 
    
    assert(queues_up[tier + 1][switch_id][k][0]!=NULL && queues_down[tier + 1][k][switch_id][0]!=NULL );
    queues_up[tier + 1][switch_id][k][0] = NULL;
    queues_down[tier + 1][k][switch_id][0] = NULL;

    assert(pipes_up[tier + 1][switch_id][k][0]!=NULL && pipes_down[tier + 1][k][switch_id][0]);
    pipes_up[tier + 1][switch_id][k][0] = NULL;
    pipes_down[tier + 1][k][switch_id][0] = NULL;
}


vector<const Route*>* XGFTTopology::get_bidir_paths(uint32_t src, uint32_t dest, bool reverse){
    vector<const Route*>* paths = new vector<const Route*>();

    route_t *routeout, *routeback;
  
    //QueueLoggerSimple *simplequeuelogger = new QueueLoggerSimple();
    //QueueLoggerSimple *simplequeuelogger = 0;
    //logfile->addLogger(*simplequeuelogger);
    //Queue* pqueue = new Queue(_linkspeed, memFromPkt(FEEDER_BUFFER), *_eventlist, simplequeuelogger);
    //pqueue->setName("PQueue_" + ntoa(src) + "_" + ntoa(dest));
    //logfile->writeName(*pqueue);

    if (_cfg->HOST_POD_SWITCH(src)==_cfg->HOST_POD_SWITCH(dest)){
  
        // forward path
        routeout = new Route();
        //routeout->push_back(pqueue);
        routeout->push_back(queues_up[TOR_TIER][src][_cfg->HOST_POD_SWITCH(src)][0]);
        routeout->push_back(pipes_up[TOR_TIER][src][_cfg->HOST_POD_SWITCH(src)][0]);

        if (_cfg->_qt==LOSSLESS_INPUT || _cfg->_qt==LOSSLESS_INPUT_ECN)
            routeout->push_back(queues_up[TOR_TIER][src][_cfg->HOST_POD_SWITCH(src)][0]->getRemoteEndpoint());

        routeout->push_back(queues_down[TOR_TIER][_cfg->HOST_POD_SWITCH(dest)][dest][0]);
        routeout->push_back(pipes_down[TOR_TIER][_cfg->HOST_POD_SWITCH(dest)][dest][0]);

        if (reverse) {
            // reverse path for RTS packets
            routeback = new Route();
            routeback->push_back(queues_up[TOR_TIER][dest][_cfg->HOST_POD_SWITCH(dest)][0]);
            routeback->push_back(pipes_up[TOR_TIER][dest][_cfg->HOST_POD_SWITCH(dest)][0]);

            if (_cfg->_qt==LOSSLESS_INPUT || _cfg->_qt==LOSSLESS_INPUT_ECN)
                routeback->push_back(queues_up[TOR_TIER][dest][_cfg->HOST_POD_SWITCH(dest)][0]->getRemoteEndpoint());

            routeback->push_back(queues_down[TOR_TIER][_cfg->HOST_POD_SWITCH(src)][src][0]);
            routeback->push_back(pipes_down[TOR_TIER][_cfg->HOST_POD_SWITCH(src)][src][0]);

            routeout->set_reverse(routeback);
            routeback->set_reverse(routeout);
        }

        //print_route(*routeout);
        paths->push_back(routeout);

        check_non_null(routeout);
        //cout << "pathcount " << paths->size() << endl;
        return paths;
    } else {
        uint32_t lca = _cfg->lca_level(src, dest);
        
    }
}

void XGFTTopology::count_queue(Queue* queue){
    if (_link_usage.find(queue)==_link_usage.end()){
        _link_usage[queue] = 0;
    }

    _link_usage[queue] = _link_usage[queue] + 1;
}

int64_t XGFTTopology::find_lp_switch(Queue* queue){
    //first check ns_nlp
    for (uint32_t srv=0;srv<_cfg->NSRV;srv++)
        for (uint32_t tor = 0; tor < _cfg->NTOR; tor++)
            if (queues_ns_nlp[srv][tor][0] == queue)
                return tor;

    //only count nup to nlp
    count_queue(queue);

    for (uint32_t agg = 0; agg < _cfg->NAGG; agg++)
        for (uint32_t tor = 0; tor < _cfg->NTOR; tor++)
            for (uint32_t b = 0; b < _cfg->_bundlesize[AGG_TIER]; b++) {
                if (queues_nup_nlp[agg][tor][b] == queue)
                    return tor;
            }

    return -1;
}

int64_t XGFTTopology::find_up_switch(Queue* queue){
    count_queue(queue);
    //first check nc_nup
    for (uint32_t core=0; core < _cfg->NCORE; core++)
        for (uint32_t agg = 0; agg < _cfg->NAGG; agg++)
            for (uint32_t b = 0; b < _cfg->_bundlesize[CORE_TIER]; b++) {
                if (queues_nc_nup[core][agg][b] == queue)
                    return agg;
            }

    //check nlp_nup
    for (uint32_t tor=0; tor < _cfg->NTOR; tor++)
        for (uint32_t agg = 0; agg < _cfg->NAGG; agg++)
            for (uint32_t b = 0; b < _cfg->_bundlesize[AGG_TIER]; b++) {
                if (queues_nlp_nup[tor][agg][b] == queue)
                    return agg;
            }

    return -1;
}

int64_t XGFTTopology::find_core_switch(Queue* queue){
    count_queue(queue);
    //first check nup_nc
    for (uint32_t agg=0;agg<_cfg->NAGG;agg++)
        for (uint32_t core = 0;core<_cfg->NCORE;core++)
            for (uint32_t b = 0; b < _cfg->_bundlesize[CORE_TIER]; b++) {
                if (queues_nup_nc[agg][core][b] == queue)
                    return core;
            }

    return -1;
}

int64_t XGFTTopology::find_destination(Queue* queue){
    //first check tor_host
    for (uint32_t tor=0; tor<_cfg->NSW[0]; tor++)
        for (uint32_t srv = 0; srv<_cfg->NSRV; srv++)
            if (queues_down[0][tor][srv][0]==queue)
                return srv;

    return -1;
}

void XGFTTopology::print_path(std::ofstream &paths,uint32_t src,const Route* route){
    paths << "SRC_" << src << " ";
  
    if (route->size()/2==2){
        paths << "LS_" << find_lp_switch((Queue*)route->at(1)) << " ";
        paths << "DST_" << find_destination((Queue*)route->at(3)) << " ";
    } else if (route->size()/2==4){
        paths << "LS_" << find_lp_switch((Queue*)route->at(1)) << " ";
        paths << "US_" << find_up_switch((Queue*)route->at(3)) << " ";
        paths << "LS_" << find_lp_switch((Queue*)route->at(5)) << " ";
        paths << "DST_" << find_destination((Queue*)route->at(7)) << " ";
    } else if (route->size()/2==6){
        paths << "LS_" << find_lp_switch((Queue*)route->at(1)) << " ";
        paths << "US_" << find_up_switch((Queue*)route->at(3)) << " ";
        paths << "CS_" << find_core_switch((Queue*)route->at(5)) << " ";
        paths << "US_" << find_up_switch((Queue*)route->at(7)) << " ";
        paths << "LS_" << find_lp_switch((Queue*)route->at(9)) << " ";
        paths << "DST_" << find_destination((Queue*)route->at(11)) << " ";
    } else {
        paths << "Wrong hop count " << ntoa(route->size()/2);
    }
  
    paths << endl;
}

void XGFTTopology::add_switch_loggers(Logfile& log, simtime_picosec sample_period) {
    for (int tier = _cfg->_tiers-1; tier >= 0; tier--){
        for (uint32_t i = 0; i < _cfg->NSW[tier]; i++){
            switches[tier][i]->add_logger(log, sample_period);
        }
    }
}
