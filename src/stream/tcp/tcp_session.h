/****************************************************************************
 * Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
 * Copyright (C) 2005-2013 Sourcefire, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.  You may not use, modify or
 * distribute this program under any other version of the GNU General
 * Public License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 ****************************************************************************/

#ifndef TCP_SESSION_H
#define TCP_SESSION_H

#include "stream_tcp.h"
#include "stream_paf.h"
#include "flow/session.h"

/* Only track a maximum number of alerts per session */
#define MAX_SESSION_ALERTS 8

struct StateMgr
{
    uint8_t    state;
    uint8_t    sub_state;
    uint8_t    state_queue;
    uint8_t    expected_flags;
    uint32_t   transition_seq;
    uint32_t   stq_get_seq;
};

//-------------------------------------------------------------------------
// extra, extra - read all about it!
// -- u2 is the only output plugin that currently supports extra data
// -- extra data may be captured before or after alerts
// -- extra data may be per packet or persistent (saved on session)
//
// -- per packet extra data is logged iff we alert on the packet
//    containing the extra data - u2 drives this
// -- an extra data mask is added to Packet to indicate when per packet
//    extra data is available
//
// -- persistent extra data must be logged exactly once for each alert
//    regardless of capture/alert ordering - s5 purge_alerts drives this
// -- an extra data mask is added to the session trackers to indicate that
//    persistent extra data is available
//
// -- event id and second are added to the session alert trackers so that
//    the extra data can be correlated with events
// -- event id and second are not available when Stream5AddSessionAlertTcp
//    is called; u2 calls Stream5UpdateSessionAlertTcp as events are logged
//    to set these fields
//-------------------------------------------------------------------------

struct Stream5AlertInfo
{
    /* For storing alerts that have already been seen on the session */
    uint32_t sid;
    uint32_t gid;
    uint32_t seq;
    // if we log extra data, event_* is used to correlate with alert
    uint32_t event_id;
    uint32_t event_second;
};

//-----------------------------------------------------------------
// we make a lot of StreamSegments, StreamTrackers, and TcpSessions
// so they are organized by member size/alignment requirements to
// minimize unused space in the structs.
// ... however, use of padding below is critical, adjust if needed
//-----------------------------------------------------------------

struct StreamSegment
{
    uint8_t    *data;
    uint8_t    *payload;

    StreamSegment *prev;
    StreamSegment *next;

#ifdef DEBUG
    int ordinal;
#endif
    struct timeval tv;
    uint32_t caplen;
    uint32_t pktlen;

    uint32_t   ts;
    uint32_t   seq;

    uint16_t   orig_dsize;
    uint16_t   size;

    uint16_t   urg_offset;
    uint8_t    buffered;

    // this sequence ensures 4-byte alignment of iph in pkt
    // (only significant if we call the grinder)
    uint8_t    pad1;
    uint16_t   pad2;
    uint8_t    pkt[1];  // variable length

};

struct StreamTracker
{
    StateMgr  s_mgr;        /* state tracking goodies */
    FlushMgr  flush_mgr;    /* please flush twice, it's a long way to
                             * the bitbucket... */

    // this is intended to be private to s5_paf but is included
    // directly to avoid the need for allocation; do not directly
    // manipulate within this module.
    PAF_State paf_state;    // for tracking protocol aware flushing

    Stream5AlertInfo alerts[MAX_SESSION_ALERTS]; /* history of alerts */

    StreamTcpConfig* config;
    StreamSegment *seglist;       /* first queued segment */
    StreamSegment *seglist_tail;  /* last queued segment */

    // TBD move out of here since only used per packet?
    StreamSegment* seglist_next;  /* next queued segment to flush */

#ifdef DEBUG
    int segment_ordinal;
#endif

    /* Local for these variables means the local part of the connection.  For
     * example, if this particular StreamTracker was tracking the client side
     * of a connection, the l_unackd value would represent the client side of
     * the connection's last unacked sequence number
     */
    uint32_t l_unackd;     /* local unack'd seq number */
    uint32_t l_nxt_seq;    /* local next expected sequence */
    uint32_t l_window;     /* local receive window */

    uint32_t r_nxt_ack;    /* next expected ack from remote side */
    uint32_t r_win_base;   /* remote side window base sequence number
                            * (i.e. the last ack we got) */
    uint32_t isn;          /* initial sequence number */
    uint32_t ts_last;      /* last timestamp (for PAWS) */
    uint32_t ts_last_pkt;  /* last packet timestamp we got */

    uint32_t seglist_base_seq;   /* seq of first queued segment */
    uint32_t seg_count;          /* number of current queued segments */
    uint32_t seg_bytes_total;    /* total bytes currently queued */
    uint32_t seg_bytes_logical;  /* logical bytes queued (total - overlaps) */
    uint32_t total_bytes_queued; /* total bytes queued (life of session) */
    uint32_t total_segs_queued;  /* number of segments queued (life) */
    uint32_t overlap_count;      /* overlaps encountered */
    uint32_t small_seg_count;
    uint32_t flush_count;        /* number of flushed queued segments */
    uint32_t xtradata_mask;      /* extra data available to log */

    uint16_t os_policy;
    uint16_t reassembly_policy;

    uint16_t wscale;       /* window scale setting */
    uint16_t mss;          /* max segment size */

    uint8_t  mac_addr[6];
    uint8_t  flags;        /* bitmap flags (TF_xxx) */

    uint8_t  alert_count;  /* number alerts stored (up to MAX_SESSION_ALERTS) */

};

class TcpSession : public Session
{
public:
    TcpSession(Flow*);

    bool setup (Packet*);
    void update_direction(char dir, snort_ip*, uint16_t port);
    int process(Packet*);

    void reset();
    void clear();
    void cleanup();

public:
    StreamTracker client;
    StreamTracker server;

#ifdef HAVE_DAQ_ADDRESS_SPACE_ID
    int32_t ingress_index;  /* Index of the inbound interface. */
    int32_t egress_index;   /* Index of the outbound interface. */
    int32_t ingress_group;  /* Index of the inbound group. */
    int32_t egress_group;   /* Index of the outbound group. */
    uint32_t daq_flags;     /* Flags for the packet (DAQ_PKT_FLAG_*) */
    uint16_t address_space_id;
#endif

    uint8_t ecn;
    bool lws_init;
    bool tcp_init;
};

void tcp_init();
void tcp_sum();
void tcp_stats();
void tcp_reset();
void tcp_show(class StreamTcpConfig*);

#endif
