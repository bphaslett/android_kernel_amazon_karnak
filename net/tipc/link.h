/*
 * net/tipc/link.h: Include file for TIPC link code
 *
 * Copyright (c) 1995-2006, 2013-2014, Ericsson AB
 * Copyright (c) 2004-2005, 2010-2011, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _TIPC_LINK_H
#define _TIPC_LINK_H

#include <net/genetlink.h>
#include "msg.h"
#include "node.h"

/* Out-of-range value for link sequence numbers
 */
#define INVALID_LINK_SEQ 0x10000

/* Link working states
 */
#define WORKING_WORKING 560810u
#define WORKING_UNKNOWN 560811u
#define RESET_UNKNOWN   560812u
#define RESET_RESET     560813u

/* Link endpoint execution states
 */
#define LINK_STARTED    0x0001
#define LINK_STOPPED    0x0002

/* Starting value for maximum packet size negotiation on unicast links
 * (unless bearer MTU is less)
 */
#define MAX_PKT_DEFAULT 1500

struct tipc_stats {
	u32 sent_info;		/* used in counting # sent packets */
	u32 recv_info;		/* used in counting # recv'd packets */
	u32 sent_states;
	u32 recv_states;
	u32 sent_probes;
	u32 recv_probes;
	u32 sent_nacks;
	u32 recv_nacks;
	u32 sent_acks;
	u32 sent_bundled;
	u32 sent_bundles;
	u32 recv_bundled;
	u32 recv_bundles;
	u32 retransmitted;
	u32 sent_fragmented;
	u32 sent_fragments;
	u32 recv_fragmented;
	u32 recv_fragments;
	u32 link_congs;		/* # port sends blocked by congestion */
	u32 deferred_recv;
	u32 duplicates;
	u32 max_queue_sz;	/* send queue size high water mark */
	u32 accu_queue_sz;	/* used for send queue size profiling */
	u32 queue_sz_counts;	/* used for send queue size profiling */
	u32 msg_length_counts;	/* used for message length profiling */
	u32 msg_lengths_total;	/* used for message length profiling */
	u32 msg_length_profile[7]; /* used for msg. length profiling */
};

/**
 * struct tipc_link - TIPC link data structure
 * @addr: network address of link's peer node
 * @name: link name character string
 * @media_addr: media address to use when sending messages over link
 * @timer: link timer
 * @owner: pointer to peer node
 * @flags: execution state flags for link endpoint instance
 * @checkpoint: reference point for triggering link continuity checking
 * @peer_session: link session # being used by peer end of link
 * @peer_bearer_id: bearer id used by link's peer endpoint
 * @bearer_id: local bearer id used by link
 * @tolerance: minimum link continuity loss needed to reset link [in ms]
 * @continuity_interval: link continuity testing interval [in ms]
 * @abort_limit: # of unacknowledged continuity probes needed to reset link
 * @state: current state of link FSM
 * @fsm_msg_cnt: # of protocol messages link FSM has sent in current state
 * @proto_msg: template for control messages generated by link
 * @pmsg: convenience pointer to "proto_msg" field
 * @priority: current link priority
 * @net_plane: current link network plane ('A' through 'H')
 * @queue_limit: outbound message queue congestion thresholds (indexed by user)
 * @exp_msg_count: # of tunnelled messages expected during link changeover
 * @reset_checkpoint: seq # of last acknowledged message at time of link reset
 * @max_pkt: current maximum packet size for this link
 * @max_pkt_target: desired maximum packet size for this link
 * @max_pkt_probes: # of probes based on current (max_pkt, max_pkt_target)
 * @outqueue: outbound message queue
 * @next_out_no: next sequence number to use for outbound messages
 * @last_retransmitted: sequence number of most recently retransmitted message
 * @stale_count: # of identical retransmit requests made by peer
 * @next_in_no: next sequence number to expect for inbound messages
 * @deferred_queue: deferred queue saved OOS b'cast message received from node
 * @unacked_window: # of inbound messages rx'd without ack'ing back to peer
 * @next_out: ptr to first unsent outbound message in queue
 * @waiting_sks: linked list of sockets waiting for link congestion to abate
 * @long_msg_seq_no: next identifier to use for outbound fragmented messages
 * @reasm_buf: head of partially reassembled inbound message fragments
 * @stats: collects statistics regarding link activity
 */
struct tipc_link {
	u32 addr;
	char name[TIPC_MAX_LINK_NAME];
	struct tipc_media_addr media_addr;
	struct timer_list timer;
	struct tipc_node *owner;

	/* Management and link supervision data */
	unsigned int flags;
	u32 checkpoint;
	u32 peer_session;
	u32 peer_bearer_id;
	u32 bearer_id;
	u32 tolerance;
	u32 continuity_interval;
	u32 abort_limit;
	int state;
	u32 fsm_msg_cnt;
	struct {
		unchar hdr[INT_H_SIZE];
		unchar body[TIPC_MAX_IF_NAME];
	} proto_msg;
	struct tipc_msg *pmsg;
	u32 priority;
	char net_plane;
	u32 queue_limit[15];	/* queue_limit[0]==window limit */

	/* Changeover */
	u32 exp_msg_count;
	u32 reset_checkpoint;

	/* Max packet negotiation */
	u32 max_pkt;
	u32 max_pkt_target;
	u32 max_pkt_probes;

	/* Sending */
	struct sk_buff_head outqueue;
	u32 next_out_no;
	u32 last_retransmitted;
	u32 stale_count;

	/* Reception */
	u32 next_in_no;
	struct sk_buff_head deferred_queue;
	u32 unacked_window;

	/* Congestion handling */
	struct sk_buff *next_out;
	struct sk_buff_head waiting_sks;

	/* Fragmentation/reassembly */
	u32 long_msg_seq_no;
	struct sk_buff *reasm_buf;

	/* Statistics */
	struct tipc_stats stats;
};

struct tipc_port;

struct tipc_link *tipc_link_create(struct tipc_node *n_ptr,
			      struct tipc_bearer *b_ptr,
			      const struct tipc_media_addr *media_addr);
void tipc_link_delete_list(unsigned int bearer_id, bool shutting_down);
void tipc_link_failover_send_queue(struct tipc_link *l_ptr);
void tipc_link_dup_queue_xmit(struct tipc_link *l_ptr, struct tipc_link *dest);
void tipc_link_reset_fragments(struct tipc_link *l_ptr);
int tipc_link_is_up(struct tipc_link *l_ptr);
int tipc_link_is_active(struct tipc_link *l_ptr);
void tipc_link_purge_queues(struct tipc_link *l_ptr);
struct sk_buff *tipc_link_cmd_config(const void *req_tlv_area,
				     int req_tlv_space,
				     u16 cmd);
struct sk_buff *tipc_link_cmd_show_stats(const void *req_tlv_area,
					 int req_tlv_space);
struct sk_buff *tipc_link_cmd_reset_stats(const void *req_tlv_area,
					  int req_tlv_space);
void tipc_link_reset_all(struct tipc_node *node);
void tipc_link_reset(struct tipc_link *l_ptr);
void tipc_link_reset_list(unsigned int bearer_id);
int tipc_link_xmit(struct sk_buff *buf, u32 dest, u32 selector);
int __tipc_link_xmit(struct tipc_link *link, struct sk_buff *buf);
u32 tipc_link_get_max_pkt(u32 dest, u32 selector);
void tipc_link_bundle_rcv(struct sk_buff *buf);
void tipc_link_proto_xmit(struct tipc_link *l_ptr, u32 msg_typ, int prob,
			  u32 gap, u32 tolerance, u32 priority, u32 acked_mtu);
void tipc_link_push_packets(struct tipc_link *l_ptr);
u32 tipc_link_defer_pkt(struct sk_buff_head *list, struct sk_buff *buf);
void tipc_link_set_queue_limits(struct tipc_link *l_ptr, u32 window);
void tipc_link_retransmit(struct tipc_link *l_ptr,
			  struct sk_buff *start, u32 retransmits);
struct sk_buff *tipc_skb_queue_next(const struct sk_buff_head *list,
				    const struct sk_buff *skb);

int tipc_nl_link_dump(struct sk_buff *skb, struct netlink_callback *cb);
int tipc_nl_link_get(struct sk_buff *skb, struct genl_info *info);
int tipc_nl_link_set(struct sk_buff *skb, struct genl_info *info);
int tipc_nl_link_reset_stats(struct sk_buff *skb, struct genl_info *info);
int tipc_nl_parse_link_prop(struct nlattr *prop, struct nlattr *props[]);

/*
 * Link sequence number manipulation routines (uses modulo 2**16 arithmetic)
 */
static inline u32 buf_seqno(struct sk_buff *buf)
{
	return msg_seqno(buf_msg(buf));
}

static inline u32 mod(u32 x)
{
	return x & 0xffffu;
}

static inline int less_eq(u32 left, u32 right)
{
	return mod(right - left) < 32768u;
}

static inline int more(u32 left, u32 right)
{
	return !less_eq(left, right);
}

static inline int less(u32 left, u32 right)
{
	return less_eq(left, right) && (mod(right) != mod(left));
}

static inline u32 lesser(u32 left, u32 right)
{
	return less_eq(left, right) ? left : right;
}


/*
 * Link status checking routines
 */
static inline int link_working_working(struct tipc_link *l_ptr)
{
	return l_ptr->state == WORKING_WORKING;
}

static inline int link_working_unknown(struct tipc_link *l_ptr)
{
	return l_ptr->state == WORKING_UNKNOWN;
}

static inline int link_reset_unknown(struct tipc_link *l_ptr)
{
	return l_ptr->state == RESET_UNKNOWN;
}

static inline int link_reset_reset(struct tipc_link *l_ptr)
{
	return l_ptr->state == RESET_RESET;
}

static inline int link_congested(struct tipc_link *l_ptr)
{
	return skb_queue_len(&l_ptr->outqueue) >= l_ptr->queue_limit[0];
}

#endif
