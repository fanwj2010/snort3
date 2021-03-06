//--------------------------------------------------------------------------
// Copyright (C) 2015-2016 Cisco and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License Version 2 as published
// by the Free Software Foundation.  You may not use, modify or distribute
// this program under any other version of the GNU General Public License.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//--------------------------------------------------------------------------

// flow_ip_tracker.cc author Carter Waxman <cwaxman@cisco.com>

#include "flow_ip_tracker.h"
#include "perf_module.h"

#include "log/messages.h"
#include "sfip/sf_ip.h"
#include "utils/util.h"

#define FLIP_FILE (PERF_NAME "_flow_ip.csv")

struct FlowStateKey
{
    sfip_t ipA;
    sfip_t ipB;
};

THREAD_LOCAL FlowIPTracker* perf_flow_ip;

FlowStateValue* FlowIPTracker::find_stats(const sfip_t* src_addr, const sfip_t* dst_addr,
    int* swapped)
{
    SFXHASH_NODE* node;
    FlowStateKey key;
    FlowStateValue* value;

    if (sfip_lesser(src_addr, dst_addr))
    {
        sfip_copy(key.ipA, src_addr);
        sfip_copy(key.ipB, dst_addr);
        *swapped = 0;
    }
    else
    {
        sfip_copy(key.ipA, dst_addr);
        sfip_copy(key.ipB, src_addr);
        *swapped = 1;
    }

    value = (FlowStateValue*)sfxhash_find(ipMap, &key);
    if (!value)
    {
        node = sfxhash_get_node(ipMap, &key);
        if (!node)
        {
            DEBUG_WRAP(DebugMessage(DEBUG_STREAM,
                "Key/Value pair didn't exist in the flow stats table and we couldn't add it!\n");
                );
            return nullptr;
        }
        memset(node->data, 0, sizeof(FlowStateValue));
        value = (FlowStateValue*)node->data;
    }

    return value;
}

FlowIPTracker::FlowIPTracker(PerfConfig* perf) : PerfTracker(perf,
        perf->output == PERF_FILE ? FLIP_FILE : nullptr)
{
    formatter->register_section("flow_ip");
    formatter->register_field("ip_a", ip_a);
    formatter->register_field("ip_b", ip_b);
    formatter->register_field("tcp_packets_a_b",
        &stats.traffic_stats[SFS_TYPE_TCP].packets_a_to_b);
    formatter->register_field("tcp_bytes_a_b",
        &stats.traffic_stats[SFS_TYPE_TCP].bytes_a_to_b);
    formatter->register_field("tcp_packets_b_a",
        &stats.traffic_stats[SFS_TYPE_TCP].packets_b_to_a);
    formatter->register_field("tcp_bytes_b_a",
        &stats.traffic_stats[SFS_TYPE_TCP].bytes_b_to_a);
    formatter->register_field("udp_packets_a_b",
        &stats.traffic_stats[SFS_TYPE_UDP].packets_a_to_b);
    formatter->register_field("udp_bytes_a_b",
        &stats.traffic_stats[SFS_TYPE_UDP].bytes_a_to_b);
    formatter->register_field("udp_packets_b_a",
        &stats.traffic_stats[SFS_TYPE_UDP].packets_b_to_a);
    formatter->register_field("udp_bytes_b_a",
        &stats.traffic_stats[SFS_TYPE_UDP].bytes_b_to_a);
    formatter->register_field("other_packets_a_b",
        &stats.traffic_stats[SFS_TYPE_OTHER].packets_a_to_b);
    formatter->register_field("other_bytes_a_b",
        &stats.traffic_stats[SFS_TYPE_OTHER].bytes_a_to_b);
    formatter->register_field("other_packets_b_a",
        &stats.traffic_stats[SFS_TYPE_OTHER].packets_b_to_a);
    formatter->register_field("other_bytes_b_a",
        &stats.traffic_stats[SFS_TYPE_OTHER].bytes_b_to_a);
    formatter->register_field("tcp_established", (PegCount*)
        &stats.state_changes[SFS_STATE_TCP_ESTABLISHED]);
    formatter->register_field("tcp_closed", (PegCount*)
        &stats.state_changes[SFS_STATE_TCP_CLOSED]);
    formatter->register_field("udp_created", (PegCount*)
        &stats.state_changes[SFS_STATE_UDP_CREATED]);
    formatter->finalize_fields();
}

FlowIPTracker::~FlowIPTracker()
{
    if (ipMap)
    {
        sfxhash_delete(ipMap);
        ipMap = nullptr;
    }
}

void FlowIPTracker::reset()
{
    static THREAD_LOCAL bool first = true;

    if (first)
    {
        ipMap = sfxhash_new(1021, sizeof(FlowStateKey), sizeof(FlowStateValue),
            perfmon_config->flowip_memcap, 1, nullptr, nullptr, 1);

        if (!ipMap)
            // FIXIT-M FlowIp allocations should all occur at thread init
            FatalError("Unable to allocate memory for FlowIP stats\n");

        first = false;
    }
    else
        sfxhash_make_empty(ipMap);
}

void FlowIPTracker::update(Packet* p)
{
    if (p->has_ip() && !p->is_rebuilt())
    {
        FlowType type = SFS_TYPE_OTHER;
        int swapped;

        const sfip_t* src_addr = p->ptrs.ip_api.get_src();
        const sfip_t* dst_addr = p->ptrs.ip_api.get_dst();
        int len = p->pkth->caplen;

        if (p->ptrs.tcph)
            type = SFS_TYPE_TCP;
        else if (p->ptrs.udph)
            type = SFS_TYPE_UDP;

        FlowStateValue* value = find_stats(src_addr, dst_addr, &swapped);
        if (!value)
            return;

        TrafficStats* stats = &value->traffic_stats[type];

        if (!swapped)
        {
            stats->packets_a_to_b++;
            stats->bytes_a_to_b += len;
        }
        else
        {
            stats->packets_b_to_a++;
            stats->bytes_b_to_a += len;
        }
        value->total_packets++;
        value->total_bytes += len;
    }
}

void FlowIPTracker::process(bool)
{
    for (auto node = sfxhash_findfirst(ipMap); node; node = sfxhash_findnext(ipMap))
    {
        FlowStateKey* key = (FlowStateKey*)node->key;
        FlowStateValue* cur_stats = (FlowStateValue*)node->data;

        sfip_raw_ntop(key->ipA.family, key->ipA.ip32, ip_a, sizeof(ip_a));
        sfip_raw_ntop(key->ipB.family, key->ipB.ip32, ip_b, sizeof(ip_b));
        memcpy(&stats, cur_stats, sizeof(stats));

        write();
    }

    if ( !(config->perf_flags & PERF_SUMMARY) )
        reset();
}

int FlowIPTracker::update_state(const sfip_t* src_addr, const sfip_t* dst_addr, FlowState state)
{
    int swapped;

    FlowStateValue* value = find_stats(src_addr, dst_addr, &swapped);
    if (!value)
        return 1;

    value->state_changes[state]++;

    return 0;
}

