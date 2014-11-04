#include <linux/module.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <net/genetlink.h>
#include <net/gue.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/xfrm.h>
#include <uapi/linux/fou.h>
#include <uapi/linux/genetlink.h>

static DEFINE_SPINLOCK(fou_lock);
static LIST_HEAD(fou_list);

struct fou {
	struct socket *sock;
	u8 protocol;
	u16 port;
	struct udp_offload udp_offloads;
	struct list_head list;
	struct rcu_head rcu;
};

struct fou_cfg {
	u16 type;
	u8 protocol;
	struct udp_port_cfg udp_config;
};

static inline struct fou *fou_from_sock(struct sock *sk)
{
	return sk->sk_user_data;
}

static void fou_recv_pull(struct sk_buff *skb, size_t len)
{
	struct iphdr *iph = ip_hdr(skb);

	/* Remove 'len' bytes from the packet (UDP header and
	 * FOU header if present).
	 */
	iph->tot_len = htons(ntohs(iph->tot_len) - len);
	__skb_pull(skb, len);
	skb_postpull_rcsum(skb, udp_hdr(skb), len);
	skb_reset_transport_header(skb);
}

static int fou_udp_recv(struct sock *sk, struct sk_buff *skb)
{
	struct fou *fou = fou_from_sock(sk);

	if (!fou)
		return 1;

	fou_recv_pull(skb, sizeof(struct udphdr));

	return -fou->protocol;
}

static struct guehdr *gue_remcsum(struct sk_buff *skb, struct guehdr *guehdr,
				  void *data, int hdrlen, u8 ipproto)
{
	__be16 *pd = data;
	u16 start = ntohs(pd[0]);
	u16 offset = ntohs(pd[1]);
	u16 poffset = 0;
	u16 plen;
	__wsum csum, delta;
	__sum16 *psum;

	if (skb->remcsum_offload) {
		/* Already processed in GRO path */
		skb->remcsum_offload = 0;
		return guehdr;
	}

	if (start > skb->len - hdrlen ||
	    offset > skb->len - hdrlen - sizeof(u16))
		return NULL;

	if (unlikely(skb->ip_summed != CHECKSUM_COMPLETE))
		__skb_checksum_complete(skb);

	plen = hdrlen + offset + sizeof(u16);
	if (!pskb_may_pull(skb, plen))
		return NULL;
	guehdr = (struct guehdr *)&udp_hdr(skb)[1];

	if (ipproto == IPPROTO_IP && sizeof(struct iphdr) < plen) {
		struct iphdr *ip = (struct iphdr *)(skb->data + hdrlen);

		/* If next header happens to be IP we can skip that for the
		 * checksum calculation since the IP header checksum is zero
		 * if correct.
		 */
		poffset = ip->ihl * 4;
	}

	csum = csum_sub(skb->csum, skb_checksum(skb, poffset + hdrlen,
						start - poffset - hdrlen, 0));

	/* Set derived checksum in packet */
	psum = (__sum16 *)(skb->data + hdrlen + offset);
	delta = csum_sub(csum_fold(csum), *psum);
	*psum = csum_fold(csum);

	/* Adjust skb->csum since we changed the packet */
	skb->csum = csum_add(skb->csum, delta);

	return guehdr;
}

static int gue_control_message(struct sk_buff *skb, struct guehdr *guehdr)
{
	/* No support yet */
	kfree_skb(skb);
	return 0;
}

static int gue_udp_recv(struct sock *sk, struct sk_buff *skb)
{
	struct fou *fou = fou_from_sock(sk);
	size_t len, optlen, hdrlen;
	struct guehdr *guehdr;
	void *data;
	u16 doffset = 0;

	if (!fou)
		return 1;

	len = sizeof(struct udphdr) + sizeof(struct guehdr);
	if (!pskb_may_pull(skb, len))
		goto drop;

	guehdr = (struct guehdr *)&udp_hdr(skb)[1];

	optlen = guehdr->hlen << 2;
	len += optlen;

	if (!pskb_may_pull(skb, len))
		goto drop;

	/* guehdr may change after pull */
	guehdr = (struct guehdr *)&udp_hdr(skb)[1];

	hdrlen = sizeof(struct guehdr) + optlen;

	if (guehdr->version != 0 || validate_gue_flags(guehdr, optlen))
		goto drop;

	hdrlen = sizeof(struct guehdr) + optlen;

	ip_hdr(skb)->tot_len = htons(ntohs(ip_hdr(skb)->tot_len) - len);

	/* Pull UDP header now, skb->data points to guehdr */
	__skb_pull(skb, sizeof(struct udphdr));

	/* Pull csum through the guehdr now . This can be used if
	 * there is a remote checksum offload.
	 */
	skb_postpull_rcsum(skb, udp_hdr(skb), len);

	data = &guehdr[1];

	if (guehdr->flags & GUE_FLAG_PRIV) {
		__be32 flags = *(__be32 *)(data + doffset);

		doffset += GUE_LEN_PRIV;

		if (flags & GUE_PFLAG_REMCSUM) {
			guehdr = gue_remcsum(skb, guehdr, data + doffset,
					     hdrlen, guehdr->proto_ctype);
			if (!guehdr)
				goto drop;

			data = &guehdr[1];

			doffset += GUE_PLEN_REMCSUM;
		}
	}

	if (unlikely(guehdr->control))
		return gue_control_message(skb, guehdr);

	__skb_pull(skb, hdrlen);
	skb_reset_transport_header(skb);

	return -guehdr->proto_ctype;

drop:
	kfree_skb(skb);
	return 0;
}

static struct sk_buff **fou_gro_receive(struct sk_buff **head,
					struct sk_buff *skb)
{
	const struct net_offload *ops;
	struct sk_buff **pp = NULL;
	u8 proto = NAPI_GRO_CB(skb)->proto;
	const struct net_offload **offloads;

	/* We can clear the encap_mark for FOU as we are essentially doing
	 * one of two possible things.  We are either adding an L4 tunnel
	 * header to the outer L3 tunnel header, or we are are simply
	 * treating the GRE tunnel header as though it is a UDP protocol
	 * specific header such as VXLAN or GENEVE.
	 */
	NAPI_GRO_CB(skb)->encap_mark = 0;

	rcu_read_lock();
	offloads = NAPI_GRO_CB(skb)->is_ipv6 ? inet6_offloads : inet_offloads;
	ops = rcu_dereference(offloads[proto]);
	if (!ops || !ops->callbacks.gro_receive)
		goto out_unlock;

	pp = call_gro_receive(ops->callbacks.gro_receive, head, skb);

out_unlock:
	rcu_read_unlock();

	return pp;
}

static int fou_gro_complete(struct sk_buff *skb, int nhoff)
{
	const struct net_offload *ops;
	u8 proto = NAPI_GRO_CB(skb)->proto;
	int err = -ENOSYS;
	const struct net_offload **offloads;

	udp_tunnel_gro_complete(skb, nhoff);

	rcu_read_lock();
	offloads = NAPI_GRO_CB(skb)->is_ipv6 ? inet6_offloads : inet_offloads;
	ops = rcu_dereference(offloads[proto]);
	if (WARN_ON(!ops || !ops->callbacks.gro_complete))
		goto out_unlock;

	err = ops->callbacks.gro_complete(skb, nhoff);

out_unlock:
	rcu_read_unlock();

	return err;
}

static struct guehdr *gue_gro_remcsum(struct sk_buff *skb, unsigned int off,
				      struct guehdr *guehdr, void *data,
				      size_t hdrlen, u8 ipproto)
{
	__be16 *pd = data;
	u16 start = ntohs(pd[0]);
	u16 offset = ntohs(pd[1]);
	u16 poffset = 0;
	u16 plen;
	void *ptr;
	__wsum csum, delta;
	__sum16 *psum;

	if (skb->remcsum_offload)
		return guehdr;

	if (start > skb_gro_len(skb) - hdrlen ||
	    offset > skb_gro_len(skb) - hdrlen - sizeof(u16) ||
	    !NAPI_GRO_CB(skb)->csum_valid || skb->remcsum_offload)
		return NULL;

	plen = hdrlen + offset + sizeof(u16);

	/* Pull checksum that will be written */
	if (skb_gro_header_hard(skb, off + plen)) {
		guehdr = skb_gro_header_slow(skb, off + plen, off);
		if (!guehdr)
			return NULL;
	}

	ptr = (void *)guehdr + hdrlen;

	if (ipproto == IPPROTO_IP &&
	    (hdrlen + sizeof(struct iphdr) < plen)) {
		struct iphdr *ip = (struct iphdr *)(ptr + hdrlen);

		/* If next header happens to be IP we can skip
		 * that for the checksum calculation since the
		 * IP header checksum is zero if correct.
		 */
		poffset = ip->ihl * 4;
	}

	csum = csum_sub(NAPI_GRO_CB(skb)->csum,
			csum_partial(ptr + poffset, start - poffset, 0));

	/* Set derived checksum in packet */
	psum = (__sum16 *)(ptr + offset);
	delta = csum_sub(csum_fold(csum), *psum);
	*psum = csum_fold(csum);

	/* Adjust skb->csum since we changed the packet */
	skb->csum = csum_add(skb->csum, delta);
	NAPI_GRO_CB(skb)->csum = csum_add(NAPI_GRO_CB(skb)->csum, delta);

	skb->remcsum_offload = 1;

	return guehdr;
}

static struct sk_buff **gue_gro_receive(struct sk_buff **head,
					struct sk_buff *skb)
{
	const struct net_offload **offloads;
	const struct net_offload *ops;
	struct sk_buff **pp = NULL;
	struct sk_buff *p;
	struct guehdr *guehdr;
	size_t len, optlen, hdrlen, off;
	void *data;
	u16 doffset = 0;
	int flush = 1;

	off = skb_gro_offset(skb);
	len = off + sizeof(*guehdr);

	guehdr = skb_gro_header_fast(skb, off);
	if (skb_gro_header_hard(skb, len)) {
		guehdr = skb_gro_header_slow(skb, len, off);
		if (unlikely(!guehdr))
			goto out;
	}

	optlen = guehdr->hlen << 2;
	len += optlen;

	if (skb_gro_header_hard(skb, len)) {
		guehdr = skb_gro_header_slow(skb, len, off);
		if (unlikely(!guehdr))
			goto out;
	}

	if (unlikely(guehdr->control) || guehdr->version != 0 ||
	    validate_gue_flags(guehdr, optlen))
		goto out;

	hdrlen = sizeof(*guehdr) + optlen;

	/* Adjust NAPI_GRO_CB(skb)->csum to account for guehdr,
	 * this is needed if there is a remote checkcsum offload.
	 */
	skb_gro_postpull_rcsum(skb, guehdr, hdrlen);

	data = &guehdr[1];

	if (guehdr->flags & GUE_FLAG_PRIV) {
		__be32 flags = *(__be32 *)(data + doffset);

		doffset += GUE_LEN_PRIV;

		if (flags & GUE_PFLAG_REMCSUM) {
			guehdr = gue_gro_remcsum(skb, off, guehdr,
						 data + doffset, hdrlen,
						 guehdr->proto_ctype);
			if (!guehdr)
				goto out;

			data = &guehdr[1];

			doffset += GUE_PLEN_REMCSUM;
		}
	}

	skb_gro_pull(skb, hdrlen);

	flush = 0;

	for (p = *head; p; p = p->next) {
		const struct guehdr *guehdr2;

		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		guehdr2 = (struct guehdr *)(p->data + off);

		/* Compare base GUE header to be equal (covers
		 * hlen, version, proto_ctype, and flags.
		 */
		if (guehdr->word != guehdr2->word) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}

		/* Compare optional fields are the same. */
		if (guehdr->hlen && memcmp(&guehdr[1], &guehdr2[1],
					   guehdr->hlen << 2)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
	}

	/* We can clear the encap_mark for GUE as we are essentially doing
	 * one of two possible things.  We are either adding an L4 tunnel
	 * header to the outer L3 tunnel header, or we are are simply
	 * treating the GRE tunnel header as though it is a UDP protocol
	 * specific header such as VXLAN or GENEVE.
	 */
	NAPI_GRO_CB(skb)->encap_mark = 0;

	rcu_read_lock();
	offloads = NAPI_GRO_CB(skb)->is_ipv6 ? inet6_offloads : inet_offloads;
	ops = rcu_dereference(offloads[guehdr->proto_ctype]);
	if (WARN_ON(!ops || !ops->callbacks.gro_receive))
		goto out_unlock;

	pp = call_gro_receive(ops->callbacks.gro_receive, head, skb);

out_unlock:
	rcu_read_unlock();
out:
	NAPI_GRO_CB(skb)->flush |= flush;

	return pp;
}

static int gue_gro_complete(struct sk_buff *skb, int nhoff)
{
	const struct net_offload **offloads;
	struct guehdr *guehdr = (struct guehdr *)(skb->data + nhoff);
	const struct net_offload *ops;
	unsigned int guehlen;
	u8 proto;
	int err = -ENOENT;

	proto = guehdr->proto_ctype;

	guehlen = sizeof(*guehdr) + (guehdr->hlen << 2);

	rcu_read_lock();
	offloads = NAPI_GRO_CB(skb)->is_ipv6 ? inet6_offloads : inet_offloads;
	ops = rcu_dereference(offloads[proto]);
	if (WARN_ON(!ops || !ops->callbacks.gro_complete))
		goto out_unlock;

	err = ops->callbacks.gro_complete(skb, nhoff + guehlen);

out_unlock:
	rcu_read_unlock();
	return err;
}

static int fou_add_to_port_list(struct fou *fou)
{
	struct fou *fout;

	spin_lock(&fou_lock);
	list_for_each_entry(fout, &fou_list, list) {
		if (fou->port == fout->port) {
			spin_unlock(&fou_lock);
			return -EALREADY;
		}
	}

	list_add(&fou->list, &fou_list);
	spin_unlock(&fou_lock);

	return 0;
}

static void fou_release(struct fou *fou)
{
	struct socket *sock = fou->sock;
	struct sock *sk = sock->sk;

	udp_del_offload(&fou->udp_offloads);

	list_del(&fou->list);

	/* Remove hooks into tunnel socket */
	sk->sk_user_data = NULL;

	sock_release(sock);

	kfree_rcu(fou, rcu);
}

static int fou_encap_init(struct sock *sk, struct fou *fou, struct fou_cfg *cfg)
{
	udp_sk(sk)->encap_rcv = fou_udp_recv;
	fou->protocol = cfg->protocol;
	fou->udp_offloads.callbacks.gro_receive = fou_gro_receive;
	fou->udp_offloads.callbacks.gro_complete = fou_gro_complete;
	fou->udp_offloads.port = cfg->udp_config.local_udp_port;
	fou->udp_offloads.ipproto = cfg->protocol;

	return 0;
}

static int gue_encap_init(struct sock *sk, struct fou *fou, struct fou_cfg *cfg)
{
	udp_sk(sk)->encap_rcv = gue_udp_recv;
	fou->udp_offloads.callbacks.gro_receive = gue_gro_receive;
	fou->udp_offloads.callbacks.gro_complete = gue_gro_complete;
	fou->udp_offloads.port = cfg->udp_config.local_udp_port;

	return 0;
}

static int fou_create(struct net *net, struct fou_cfg *cfg,
		      struct socket **sockp)
{
	struct fou *fou = NULL;
	int err;
	struct socket *sock = NULL;
	struct sock *sk;

	/* Open UDP socket */
	err = udp_sock_create(net, &cfg->udp_config, &sock);
	if (err < 0)
		goto error;

	/* Allocate FOU port structure */
	fou = kzalloc(sizeof(*fou), GFP_KERNEL);
	if (!fou) {
		err = -ENOMEM;
		goto error;
	}

	sk = sock->sk;

	fou->port = cfg->udp_config.local_udp_port;

	/* Initial for fou type */
	switch (cfg->type) {
	case FOU_ENCAP_DIRECT:
		err = fou_encap_init(sk, fou, cfg);
		if (err)
			goto error;
		break;
	case FOU_ENCAP_GUE:
		err = gue_encap_init(sk, fou, cfg);
		if (err)
			goto error;
		break;
	default:
		err = -EINVAL;
		goto error;
	}

	udp_sk(sk)->encap_type = 1;
	udp_encap_enable();

	sk->sk_user_data = fou;
	fou->sock = sock;

	udp_set_convert_csum(sk, true);

	sk->sk_allocation = GFP_ATOMIC;

	if (cfg->udp_config.family == AF_INET) {
		err = udp_add_offload(&fou->udp_offloads);
		if (err)
			goto error;
	}

	err = fou_add_to_port_list(fou);
	if (err)
		goto error;

	if (sockp)
		*sockp = sock;

	return 0;

error:
	kfree(fou);
	if (sock)
		sock_release(sock);

	return err;
}

static int fou_destroy(struct net *net, struct fou_cfg *cfg)
{
	struct fou *fou;
	u16 port = cfg->udp_config.local_udp_port;
	int err = -EINVAL;

	spin_lock(&fou_lock);
	list_for_each_entry(fou, &fou_list, list) {
		if (fou->port == port) {
			udp_del_offload(&fou->udp_offloads);
			fou_release(fou);
			err = 0;
			break;
		}
	}
	spin_unlock(&fou_lock);

	return err;
}

static struct genl_family fou_nl_family = {
	.id		= GENL_ID_GENERATE,
	.hdrsize	= 0,
	.name		= FOU_GENL_NAME,
	.version	= FOU_GENL_VERSION,
	.maxattr	= FOU_ATTR_MAX,
	.netnsok	= true,
};

static struct nla_policy fou_nl_policy[FOU_ATTR_MAX + 1] = {
	[FOU_ATTR_PORT] = { .type = NLA_U16, },
	[FOU_ATTR_AF] = { .type = NLA_U8, },
	[FOU_ATTR_IPPROTO] = { .type = NLA_U8, },
	[FOU_ATTR_TYPE] = { .type = NLA_U8, },
};

static int parse_nl_config(struct genl_info *info,
			   struct fou_cfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	cfg->udp_config.family = AF_INET;

	if (info->attrs[FOU_ATTR_AF]) {
		u8 family = nla_get_u8(info->attrs[FOU_ATTR_AF]);

		if (family != AF_INET && family != AF_INET6)
			return -EINVAL;

		cfg->udp_config.family = family;
	}

	if (info->attrs[FOU_ATTR_PORT]) {
		u16 port = nla_get_u16(info->attrs[FOU_ATTR_PORT]);

		cfg->udp_config.local_udp_port = port;
	}

	if (info->attrs[FOU_ATTR_IPPROTO])
		cfg->protocol = nla_get_u8(info->attrs[FOU_ATTR_IPPROTO]);

	if (info->attrs[FOU_ATTR_TYPE])
		cfg->type = nla_get_u8(info->attrs[FOU_ATTR_TYPE]);

	return 0;
}

static int fou_nl_cmd_add_port(struct sk_buff *skb, struct genl_info *info)
{
	struct fou_cfg cfg;
	int err;

	err = parse_nl_config(info, &cfg);
	if (err)
		return err;

	return fou_create(&init_net, &cfg, NULL);
}

static int fou_nl_cmd_rm_port(struct sk_buff *skb, struct genl_info *info)
{
	struct fou_cfg cfg;

	parse_nl_config(info, &cfg);

	return fou_destroy(&init_net, &cfg);
}

static const struct genl_ops fou_nl_ops[] = {
	{
		.cmd = FOU_CMD_ADD,
		.doit = fou_nl_cmd_add_port,
		.policy = fou_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = FOU_CMD_DEL,
		.doit = fou_nl_cmd_rm_port,
		.policy = fou_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

static void fou_build_udp(struct sk_buff *skb, struct ip_tunnel_encap *e,
			  struct flowi4 *fl4, u8 *protocol, __be16 sport)
{
	struct udphdr *uh;

	skb_push(skb, sizeof(struct udphdr));
	skb_reset_transport_header(skb);

	uh = udp_hdr(skb);

	uh->dest = e->dport;
	uh->source = sport;
	uh->len = htons(skb->len);
	uh->check = 0;
	udp_set_csum(!(e->flags & TUNNEL_ENCAP_FLAG_CSUM), skb,
		     fl4->saddr, fl4->daddr, skb->len);

	*protocol = IPPROTO_UDP;
}

int fou_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
		     u8 *protocol, struct flowi4 *fl4)
{
	bool csum = !!(e->flags & TUNNEL_ENCAP_FLAG_CSUM);
	int type = csum ? SKB_GSO_UDP_TUNNEL_CSUM : SKB_GSO_UDP_TUNNEL;
	__be16 sport;

	skb = iptunnel_handle_offloads(skb, csum, type);

	if (IS_ERR(skb))
		return PTR_ERR(skb);

	sport = e->sport ? : udp_flow_src_port(dev_net(skb->dev),
					       skb, 0, 0, false);
	fou_build_udp(skb, e, fl4, protocol, sport);

	return 0;
}
EXPORT_SYMBOL(fou_build_header);

int gue_build_header(struct sk_buff *skb, struct ip_tunnel_encap *e,
		     u8 *protocol, struct flowi4 *fl4)
{
	bool csum = !!(e->flags & TUNNEL_ENCAP_FLAG_CSUM);
	int type = csum ? SKB_GSO_UDP_TUNNEL_CSUM : SKB_GSO_UDP_TUNNEL;
	struct guehdr *guehdr;
	size_t hdrlen, optlen = 0;
	__be16 sport;
	void *data;
	bool need_priv = false;

	if ((e->flags & TUNNEL_ENCAP_FLAG_REMCSUM) &&
	    skb->ip_summed == CHECKSUM_PARTIAL) {
		csum = false;
		optlen += GUE_PLEN_REMCSUM;
		type |= SKB_GSO_TUNNEL_REMCSUM;
		need_priv = true;
	}

	optlen += need_priv ? GUE_LEN_PRIV : 0;

	skb = iptunnel_handle_offloads(skb, csum, type);

	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* Get source port (based on flow hash) before skb_push */
	sport = e->sport ? : udp_flow_src_port(dev_net(skb->dev),
					       skb, 0, 0, false);

	hdrlen = sizeof(struct guehdr) + optlen;

	skb_push(skb, hdrlen);

	guehdr = (struct guehdr *)skb->data;

	guehdr->control = 0;
	guehdr->version = 0;
	guehdr->hlen = optlen >> 2;
	guehdr->flags = 0;
	guehdr->proto_ctype = *protocol;

	data = &guehdr[1];

	if (need_priv) {
		__be32 *flags = data;

		guehdr->flags |= GUE_FLAG_PRIV;
		*flags = 0;
		data += GUE_LEN_PRIV;

		if (type & SKB_GSO_TUNNEL_REMCSUM) {
			u16 csum_start = skb_checksum_start_offset(skb);
			__be16 *pd = data;

			if (csum_start < hdrlen)
				return -EINVAL;

			csum_start -= hdrlen;
			pd[0] = htons(csum_start);
			pd[1] = htons(csum_start + skb->csum_offset);

			if (!skb_is_gso(skb)) {
				skb->ip_summed = CHECKSUM_NONE;
				skb->encapsulation = 0;
			}

			*flags |= GUE_PFLAG_REMCSUM;
			data += GUE_PLEN_REMCSUM;
		}

	}

	fou_build_udp(skb, e, fl4, protocol, sport);

	return 0;
}
EXPORT_SYMBOL(gue_build_header);

static int __init fou_init(void)
{
	int ret;

	ret = genl_register_family_with_ops(&fou_nl_family,
					    fou_nl_ops);

	return ret;
}

static void __exit fou_fini(void)
{
	struct fou *fou, *next;

	genl_unregister_family(&fou_nl_family);

	/* Close all the FOU sockets */

	spin_lock(&fou_lock);
	list_for_each_entry_safe(fou, next, &fou_list, list)
		fou_release(fou);
	spin_unlock(&fou_lock);
}

module_init(fou_init);
module_exit(fou_fini);
MODULE_AUTHOR("Tom Herbert <therbert@google.com>");
MODULE_LICENSE("GPL");
