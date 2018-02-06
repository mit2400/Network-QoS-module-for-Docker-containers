/*
 *	Handle incoming frames
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/netfilter_bridge.h>
#include <linux/neighbour.h>
#include <net/arp.h>
#include <linux/export.h>
#include <linux/rculist.h>
#include "br_private.h"
#include "br_private_tunnel.h"

#ifdef CONFIG_BRIDGE_CREDIT_MODE//minkoo
int (*fp_pay)(struct net_bridge_port *p, unsigned int packet_data_len);
EXPORT_SYMBOL(fp_pay);
#endif

/* Hook for brouter */
br_should_route_hook_t __rcu *br_should_route_hook __read_mostly;
EXPORT_SYMBOL(br_should_route_hook);

static int
br_netif_receive_skb(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	br_drop_fake_rtable(skb);
	return netif_receive_skb(skb);
}

static int br_pass_frame_up(struct sk_buff *skb)
{
	struct net_device *indev, *brdev = BR_INPUT_SKB_CB(skb)->brdev;
	struct net_bridge *br = netdev_priv(brdev);
	struct net_bridge_vlan_group *vg;
	struct pcpu_sw_netstats *brstats = this_cpu_ptr(br->stats);

	u64_stats_update_begin(&brstats->syncp);
	brstats->rx_packets++;
	brstats->rx_bytes += skb->len;
	u64_stats_update_end(&brstats->syncp);

	vg = br_vlan_group_rcu(br);
	/* Bridge is just like any other port.  Make sure the
	 * packet is allowed except in promisc modue when someone
	 * may be running packet capture.
	 */
	if (!(brdev->flags & IFF_PROMISC) &&
	    !br_allowed_egress(vg, skb)) {
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	indev = skb->dev;
	skb->dev = brdev;
	skb = br_handle_vlan(br, NULL, vg, skb);
	if (!skb)
		return NET_RX_DROP;
	/* update the multicast stats if the packet is IGMP/MLD */
	br_multicast_count(br, NULL, skb, br_multicast_igmp_type(skb),
			   BR_MCAST_DIR_TX);

	return NF_HOOK(NFPROTO_BRIDGE, NF_BR_LOCAL_IN,
		       dev_net(indev), NULL, skb, indev, NULL,
		       br_netif_receive_skb);
}

static void br_do_proxy_arp(struct sk_buff *skb, struct net_bridge *br,
			    u16 vid, struct net_bridge_port *p)
{
	struct net_device *dev = br->dev;
	struct neighbour *n;
	struct arphdr *parp;
	u8 *arpptr, *sha;
	__be32 sip, tip;

	BR_INPUT_SKB_CB(skb)->proxyarp_replied = false;

	if ((dev->flags & IFF_NOARP) ||
	    !pskb_may_pull(skb, arp_hdr_len(dev)))
		return;

	parp = arp_hdr(skb);

	if (parp->ar_pro != htons(ETH_P_IP) ||
	    parp->ar_op != htons(ARPOP_REQUEST) ||
	    parp->ar_hln != dev->addr_len ||
	    parp->ar_pln != 4)
		return;

	arpptr = (u8 *)parp + sizeof(struct arphdr);
	sha = arpptr;
	arpptr += dev->addr_len;	/* sha */
	memcpy(&sip, arpptr, sizeof(sip));
	arpptr += sizeof(sip);
	arpptr += dev->addr_len;	/* tha */
	memcpy(&tip, arpptr, sizeof(tip));

	if (ipv4_is_loopback(tip) ||
	    ipv4_is_multicast(tip))
		return;

	n = neigh_lookup(&arp_tbl, &tip, dev);
	if (n) {
		struct net_bridge_fdb_entry *f;

		if (!(n->nud_state & NUD_VALID)) {
			neigh_release(n);
			return;
		}

		f = br_fdb_find_rcu(br, n->ha, vid);
		if (f && ((p->flags & BR_PROXYARP) ||
			  (f->dst && (f->dst->flags & BR_PROXYARP_WIFI)))) {
			arp_send(ARPOP_REPLY, ETH_P_ARP, sip, skb->dev, tip,
				 sha, n->ha, sha);
			BR_INPUT_SKB_CB(skb)->proxyarp_replied = true;
		}

		neigh_release(n);
	}
}

/* note: already called with rcu_read_lock */
int br_handle_frame_finish(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct net_bridge_port *p = br_port_get_rcu(skb->dev);
	const unsigned char *dest = eth_hdr(skb)->h_dest;
	enum br_pkt_type pkt_type = BR_PKT_UNICAST;
	struct net_bridge_fdb_entry *dst = NULL;
	struct net_bridge_mdb_entry *mdst;
	bool local_rcv, mcast_hit = false;
	struct net_bridge *br;
	u16 vid = 0;

	if (!p || p->state == BR_STATE_DISABLED)
		goto drop;

	if (!br_allowed_ingress(p->br, nbp_vlan_group_rcu(p), skb, &vid))
		goto out;

	nbp_switchdev_frame_mark(p, skb);

	/* insert into forwarding database after filtering to avoid spoofing */
	br = p->br;
	if (p->flags & BR_LEARNING)
		br_fdb_update(br, p, eth_hdr(skb)->h_source, vid, false);

	local_rcv = !!(br->dev->flags & IFF_PROMISC);
	if (is_multicast_ether_addr(dest)) {
		/* by definition the broadcast is also a multicast address */
		if (is_broadcast_ether_addr(dest)) {
			pkt_type = BR_PKT_BROADCAST;
			local_rcv = true;
		} else {
			pkt_type = BR_PKT_MULTICAST;
			if (br_multicast_rcv(br, p, skb, vid))
				goto drop;
		}
	}

	if (p->state == BR_STATE_LEARNING)
		goto drop;

	BR_INPUT_SKB_CB(skb)->brdev = br->dev;

	if (IS_ENABLED(CONFIG_INET) && skb->protocol == htons(ETH_P_ARP))
		br_do_proxy_arp(skb, br, vid, p);

	switch (pkt_type) {
	case BR_PKT_MULTICAST:
		mdst = br_mdb_get(br, skb, vid);
		if ((mdst || BR_INPUT_SKB_CB_MROUTERS_ONLY(skb)) &&
		    br_multicast_querier_exists(br, eth_hdr(skb))) {
			if ((mdst && mdst->mglist) ||
			    br_multicast_is_router(br)) {
				local_rcv = true;
				br->dev->stats.multicast++;
			}
			mcast_hit = true;
		} else {
			local_rcv = true;
			br->dev->stats.multicast++;
		}
		break;
	case BR_PKT_UNICAST:
		dst = br_fdb_find_rcu(br, dest, vid);
	default:
		break;
	}

	if (dst) {
		unsigned long now = jiffies;

		if (dst->is_local)
			return br_pass_frame_up(skb);

		if (now != dst->used)
			dst->used = now;
		br_forward(dst->dst, skb, local_rcv, false);
	} else {
		if (!mcast_hit)
			br_flood(br, skb, pkt_type, local_rcv, false);
		else
			br_multicast_flood(mdst, skb, local_rcv, false);
	}

	if (local_rcv)
		return br_pass_frame_up(skb);

out:
	return 0;
drop:
	kfree_skb(skb);
	goto out;
}
EXPORT_SYMBOL_GPL(br_handle_frame_finish);

static void __br_handle_local_finish(struct sk_buff *skb)
{
	struct net_bridge_port *p = br_port_get_rcu(skb->dev);
	u16 vid = 0;

	/* check if vlan is allowed, to avoid spoofing */
	if (p->flags & BR_LEARNING && br_should_learn(p, skb, &vid))
		br_fdb_update(p->br, p, eth_hdr(skb)->h_source, vid, false);
}

/* note: already called with rcu_read_lock */
static int br_handle_local_finish(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct net_bridge_port *p = br_port_get_rcu(skb->dev);

	__br_handle_local_finish(skb);

	BR_INPUT_SKB_CB(skb)->brdev = p->br->dev;
	br_pass_frame_up(skb);
	return 0;
}

/*
 * Return NULL if skb is handled
 * note: already called with rcu_read_lock
 */
rx_handler_result_t br_handle_frame(struct sk_buff **pskb)
{
	struct net_bridge_port *p;
	struct sk_buff *skb = *pskb;
	const unsigned char *dest = eth_hdr(skb)->h_dest;
	br_should_route_hook_t *rhook;

#ifndef CONFIG_BRIDGE_CREDIT_MODE
	if (unlikely(skb->pkt_type == PACKET_LOOPBACK))
		return RX_HANDLER_PASS;
#else
	if (unlikely(skb->pkt_type == PACKET_LOOPBACK)) {
		printk(KERN_DEBUG "packet:loopback.\n");
		return RX_HANDLER_PASS;
	}
#endif

#ifndef CONFIG_BRIDGE_CREDIT_MODE
	if (!is_valid_ether_addr(eth_hdr(skb)->h_source))
		goto drop;
#else
	if (!is_valid_ether_addr(eth_hdr(skb)->h_source)) {
		printk(KERN_DEBUG "packet:not valid ethernet address.\n");
		goto drop;
	}
#endif

	skb = skb_share_check(skb, GFP_ATOMIC);
#ifndef CONFIG_BRIDGE_CREDIT_MODE
	if (!skb)
		return RX_HANDLER_CONSUMED;
#else
	if (!skb) {
		printk(KERN_DEBUG "packet:no skb.\n");
		return RX_HANDLER_CONSUMED;
	}
#endif

	p = br_port_get_rcu(skb->dev);
#ifdef CONFIG_BRIDGE_CREDIT_MODE//minkoo
	// len: all bytes of original packet
	// data_len : each skb's packet bytes
	//if (!br_pay_credit(p, skb->data_len, skb->len, skb->data_len)) {
	//	printk(KERN_DEBUG "packet:pay fail.\n");
	//	goto drop;
	//}
	if((*fp_pay)!=NULL){
		if(!fp_pay(p,skb->data_len)){
			printk(KERN_DEBUG "packet:pay fail.\n");
			goto drop;
		}
	}
#endif
	if (p->flags & BR_VLAN_TUNNEL) {
#ifndef CONFIG_BRIDGE_CREDIT_MODE
		if (br_handle_ingress_vlan_tunnel(skb, p,
						  nbp_vlan_group_rcu(p)))
			goto drop;
#else
		if (br_handle_ingress_vlan_tunnel(skb, p,
						  nbp_vlan_group_rcu(p))) {
			printk(KERN_DEBUG "packet:ingress vlan tunnel set.\n");
			goto drop;
		}
#endif
	}

	if (unlikely(is_link_local_ether_addr(dest))) {
		u16 fwd_mask = p->br->group_fwd_mask_required;

		/*
		 * See IEEE 802.1D Table 7-10 Reserved addresses
		 *
		 * Assignment		 		Value
		 * Bridge Group Address		01-80-C2-00-00-00
		 * (MAC Control) 802.3		01-80-C2-00-00-01
		 * (Link Aggregation) 802.3	01-80-C2-00-00-02
		 * 802.1X PAE address		01-80-C2-00-00-03
		 *
		 * 802.1AB LLDP 		01-80-C2-00-00-0E
		 *
		 * Others reserved for future standardization
		 */
		switch (dest[5]) {
		case 0x00:	/* Bridge Group Address */
			/* If STP is turned off,
			   then must forward to keep loop detection */
#ifndef CONFIG_BRIDGE_CREDIT_MODE
			if (p->br->stp_enabled == BR_NO_STP ||
			    fwd_mask & (1u << dest[5]))
				goto forward;
#else
			if (p->br->stp_enabled == BR_NO_STP ||
			    fwd_mask & (1u << dest[5])) {
				printk(KERN_DEBUG "packet:no STP or cut by fwd mask.\n");
				goto forward;
			}
#endif
			*pskb = skb;
			__br_handle_local_finish(skb);
#ifdef CONFIG_BRIDGE_CREDIT_MODE
			printk(KERN_DEBUG "packet:Bridge Group Address.\n");
#endif
			return RX_HANDLER_PASS;

		case 0x01:	/* IEEE MAC (Pause) */
#ifdef CONFIG_BRIDGE_CREDIT_MODE
			printk(KERN_DEBUG "packet:Pause packet.\n");
#endif
			goto drop;

		case 0x0E:	/* 802.1AB LLDP */
			fwd_mask |= p->br->group_fwd_mask;
#ifndef CONFIG_BRIDGE_CREDIT_MODE
			if (fwd_mask & (1u << dest[5]))
				goto forward;
#else
			if (fwd_mask & (1u << dest[5])) {
				printk(KERN_DEBUG "packet:lldp cut by fwd mask.\n");
				goto forward;
			}
#endif
			*pskb = skb;
			__br_handle_local_finish(skb);
#ifdef CONFIG_BRIDGE_CREDIT_MODE
			printk(KERN_DEBUG "packet:LLDP.\n");
#endif
			return RX_HANDLER_PASS;

		default:
			/* Allow selective forwarding for most other protocols */
#ifdef CONFIG_BRIDGE_CREDIT_MODE
			printk(KERN_DEBUG "packet:Allow selective forwarding for most other protocols.\n");
#endif
			fwd_mask |= p->br->group_fwd_mask;
#ifndef CONFIG_BRIDGE_CREDIT_MODE
			if (fwd_mask & (1u << dest[5]))
				goto forward;
#else
			if (fwd_mask & (1u << dest[5])) {
				printk(KERN_DEBUG "packet:default cut by fwd mask.\n");
				goto forward;
			}
#endif
		}

		/* Deliver packet to local host only */
#ifdef CONFIG_BRIDGE_CREDIT_MODE
		printk(KERN_DEBUG "packet:local finish.\n");
#endif
		NF_HOOK(NFPROTO_BRIDGE, NF_BR_LOCAL_IN, dev_net(skb->dev),
			NULL, skb, skb->dev, NULL, br_handle_local_finish);

		return RX_HANDLER_CONSUMED;
	}

forward:
	switch (p->state) {
	case BR_STATE_FORWARDING:
		rhook = rcu_dereference(br_should_route_hook);
#ifdef CONFIG_BRIDGE_CREDIT_MODE
		printk(KERN_DEBUG "packet:forwarding state.\n");
#endif
		if (rhook) {
#ifdef CONFIG_BRIDGE_CREDIT_MODE
			printk(KERN_DEBUG "packet:rhook exist.\n");
#endif
			if ((*rhook)(skb)) {
#ifdef CONFIG_BRIDGE_CREDIT_MODE
				printk(KERN_DEBUG "packet:rhook skb function exist.\n");
#endif
				*pskb = skb;
				return RX_HANDLER_PASS;
			}
			dest = eth_hdr(skb)->h_dest;
		}
		/* fall through */
	case BR_STATE_LEARNING:
#ifdef CONFIG_BRIDGE_CREDIT_MODE
		printk(KERN_DEBUG "packet:learning state.\n");
#endif
#ifndef CONFIG_BRIDGE_CREDIT_MODE
		if (ether_addr_equal(p->br->dev->dev_addr, dest))
			skb->pkt_type = PACKET_HOST;
#else
		if (ether_addr_equal(p->br->dev->dev_addr, dest)) {
			printk(KERN_DEBUG "packet:ehter addr equal.\n");
			skb->pkt_type = PACKET_HOST;
		}
#endif

#ifdef CONFIG_BRIDGE_CREDIT_MODE
		printk(KERN_DEBUG "packet:handle frame finish.\n");
#endif
		NF_HOOK(NFPROTO_BRIDGE, NF_BR_PRE_ROUTING,
			dev_net(skb->dev), NULL, skb, skb->dev, NULL,
			br_handle_frame_finish);
		break;
	default:
drop:
		kfree_skb(skb);
	}

	return RX_HANDLER_CONSUMED;
}
