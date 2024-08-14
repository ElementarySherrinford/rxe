// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <linux/skbuff.h>
#include <crypto/hash.h>

#include "rxe.h"
#include "rxe_loc.h"
#include "rxe_queue.h"

static int next_opcode(struct rxe_qp *qp, struct rxe_send_wqe *wqe,
		       u32 opcode);

//bitshift operations to 'find first zero' (or find new hole), done by dividing the bitmap into chunks of 32 and operating on them in parallel. 
static inline bool findNextHoleTx(__uint128_t bitmap, u8 nextHole) {
	u8 bits_to_shiftArr[BDPBY32];
	u8 bits_to_shift = 0;

	//collectPartStats: 
	for(int i=0; i < BDPBY32; i++) {
		int idx = i << 5;
		u32 part = extractBits(bitmap,idx, idx + 31);
		u8 bits_to_shift_part = 0;
		if((part & 0xffff) == 0xffff) {
			bits_to_shift_part += 16;
			part = part >> 16;
		}
		if((part & 0xff) == 0xff) {
			bits_to_shift_part += 8;
			part = part >> 8;
		}
		if((part & 0xf) == 0xf) {
			bits_to_shift_part += 4;
			part = part >> 4;
		}
		if((part & 3) == 3) {
			bits_to_shift_part += 2;
			part = part >> 2;
		}
		if((part & 1) == 1){
			bits_to_shift_part += 1;
			part = part >> 1;
		}
		if((part & 1) == 1){
			bits_to_shift_part += 1;
		}
		bits_to_shiftArr[i] = bits_to_shift_part;

		bool factor = 1;
		if(i > 0)
		{
			u8 factor_u = bits_to_shiftArr[i - 1] >> 5;//LOGBDP bit
			factor = !!factor_u;
		} 

		bits_to_shiftArr[i] *= factor;
		bits_to_shift += bits_to_shiftArr[i];
	}
	bitmap = bitmap >> bits_to_shift;
	nextHole = bits_to_shift;
	if(bitmap == 0) return 0;
	return 1; 
} 


static inline void retry_first_write_send(struct rxe_qp *qp,
					  struct rxe_send_wqe *wqe,
					  unsigned int mask, int npsn)
{
	int i;

	for (i = 0; i < npsn; i++) {
		int to_send = (wqe->dma.resid > qp->mtu) ?
				qp->mtu : wqe->dma.resid;

		qp->req.opcode = next_opcode(qp, wqe,
					     wqe->wr.opcode);

		if (wqe->wr.send_flags & IB_SEND_INLINE) {
			wqe->dma.resid -= to_send;
			wqe->dma.sge_offset += to_send;
		} else {
			advance_dma_data(&wqe->dma, to_send);
		}
		if (mask & WR_WRITE_MASK)
			wqe->iova += qp->mtu;
	}
}

static void req_retry(struct rxe_qp *qp)
{
	struct rxe_send_wqe *wqe;
	unsigned int wqe_index;
	unsigned int mask;
	int npsn;
	int first = 1;

	qp->req.wqe_index	= consumer_index(qp->sq.queue);
	qp->req.psn		= qp->comp.psn;
	qp->req.opcode		= -1;

	for (wqe_index = consumer_index(qp->sq.queue);
		wqe_index != producer_index(qp->sq.queue);
		wqe_index = next_index(qp->sq.queue, wqe_index)) {
		wqe = addr_from_index(qp->sq.queue, wqe_index);
		mask = wr_opcode_mask(wqe->wr.opcode, qp);

		if (wqe->state == wqe_state_posted)
			break;

		if (wqe->state == wqe_state_done)
			continue;

		wqe->iova = (mask & WR_ATOMIC_MASK) ?
			     wqe->wr.wr.atomic.remote_addr :
			     (mask & WR_READ_OR_WRITE_MASK) ?
			     wqe->wr.wr.rdma.remote_addr :
			     0;

		if (!first || (mask & WR_READ_MASK) == 0) {
			wqe->dma.resid = wqe->dma.length;
			wqe->dma.cur_sge = 0;
			wqe->dma.sge_offset = 0;
		}

		if (first) {
			first = 0;

			if (mask & WR_WRITE_OR_SEND_MASK) {
				npsn = (qp->comp.psn - wqe->first_psn) &
					BTH_PSN_MASK;
				retry_first_write_send(qp, wqe, mask, npsn);
			}

			if (mask & WR_READ_MASK) {
				npsn = (wqe->dma.length - wqe->dma.resid) /
					qp->mtu;
				wqe->iova += npsn * qp->mtu;
			}
		}

		wqe->state = wqe_state_posted;
	}
}

void rnr_nak_timer(struct timer_list *t)
{
	struct rxe_qp *qp = from_timer(qp, t, rnr_nak_timer);

	pr_debug("qp#%d rnr nak timer fired\n", qp_num(qp));
	rxe_run_task(&qp->req.task, 1);
}

static struct rxe_send_wqe *req_next_wqe(struct rxe_qp *qp)
{
	struct rxe_send_wqe *wqe = queue_head(qp->sq.queue);
	unsigned long flags;

	if (unlikely(qp->req.state == QP_STATE_DRAIN)) {
		/* check to see if we are drained;
		 * state_lock used by requester and completer
		 */
		spin_lock_irqsave(&qp->state_lock, flags);
		do {
			if (qp->req.state != QP_STATE_DRAIN) {
				/* comp just finished */
				spin_unlock_irqrestore(&qp->state_lock,
						       flags);
				break;
			}

			if (wqe && ((qp->req.wqe_index !=
				consumer_index(qp->sq.queue)) ||
				(wqe->state != wqe_state_posted))) {
				/* comp not done yet */
				spin_unlock_irqrestore(&qp->state_lock,
						       flags);
				break;
			}

			qp->req.state = QP_STATE_DRAINED;
			spin_unlock_irqrestore(&qp->state_lock, flags);

			if (qp->ibqp.event_handler) {
				struct ib_event ev;

				ev.device = qp->ibqp.device;
				ev.element.qp = &qp->ibqp;
				ev.event = IB_EVENT_SQ_DRAINED;
				qp->ibqp.event_handler(&ev,
					qp->ibqp.qp_context);
			}
		} while (0);
	}

	if (qp->req.wqe_index == producer_index(qp->sq.queue))
		return NULL;

	wqe = addr_from_index(qp->sq.queue, qp->req.wqe_index);

	if (unlikely((qp->req.state == QP_STATE_DRAIN ||
		      qp->req.state == QP_STATE_DRAINED) &&
		     (wqe->state != wqe_state_processing)))
		return NULL;

	if (unlikely((wqe->wr.send_flags & IB_SEND_FENCE) &&
		     (qp->req.wqe_index != consumer_index(qp->sq.queue)))) {
		qp->req.wait_fence = 1;
		return NULL;
	}

	wqe->mask = wr_opcode_mask(wqe->wr.opcode, qp);
	return wqe;
}

static int next_opcode_rc(struct rxe_qp *qp, u32 opcode, int fits)
{
	switch (opcode) {
	case IB_WR_RDMA_WRITE:
		if (qp->req.opcode == IB_OPCODE_RC_RDMA_WRITE_FIRST ||
		    qp->req.opcode == IB_OPCODE_RC_RDMA_WRITE_MIDDLE)
			return fits ?
				IB_OPCODE_RC_RDMA_WRITE_LAST :
				IB_OPCODE_RC_RDMA_WRITE_MIDDLE;
		else
			return fits ?
				IB_OPCODE_RC_RDMA_WRITE_ONLY :
				IB_OPCODE_RC_RDMA_WRITE_FIRST;

	case IB_WR_RDMA_WRITE_WITH_IMM:
		if (qp->req.opcode == IB_OPCODE_RC_RDMA_WRITE_FIRST ||
		    qp->req.opcode == IB_OPCODE_RC_RDMA_WRITE_MIDDLE)
			return fits ?
				IB_OPCODE_RC_RDMA_WRITE_LAST_WITH_IMMEDIATE :
				IB_OPCODE_RC_RDMA_WRITE_MIDDLE;
		else
			return fits ?
				IB_OPCODE_RC_RDMA_WRITE_ONLY_WITH_IMMEDIATE :
				IB_OPCODE_RC_RDMA_WRITE_FIRST;

	case IB_WR_SEND:
		if (qp->req.opcode == IB_OPCODE_RC_SEND_FIRST ||
		    qp->req.opcode == IB_OPCODE_RC_SEND_MIDDLE)
			return fits ?
				IB_OPCODE_RC_SEND_LAST :
				IB_OPCODE_RC_SEND_MIDDLE;
		else
			return fits ?
				IB_OPCODE_RC_SEND_ONLY :
				IB_OPCODE_RC_SEND_FIRST;

	case IB_WR_SEND_WITH_IMM:
		if (qp->req.opcode == IB_OPCODE_RC_SEND_FIRST ||
		    qp->req.opcode == IB_OPCODE_RC_SEND_MIDDLE)
			return fits ?
				IB_OPCODE_RC_SEND_LAST_WITH_IMMEDIATE :
				IB_OPCODE_RC_SEND_MIDDLE;
		else
			return fits ?
				IB_OPCODE_RC_SEND_ONLY_WITH_IMMEDIATE :
				IB_OPCODE_RC_SEND_FIRST;

	case IB_WR_RDMA_READ:
		return IB_OPCODE_RC_RDMA_READ_REQUEST;

	case IB_WR_ATOMIC_CMP_AND_SWP:
		return IB_OPCODE_RC_COMPARE_SWAP;

	case IB_WR_ATOMIC_FETCH_AND_ADD:
		return IB_OPCODE_RC_FETCH_ADD;

	case IB_WR_SEND_WITH_INV:
		if (qp->req.opcode == IB_OPCODE_RC_SEND_FIRST ||
		    qp->req.opcode == IB_OPCODE_RC_SEND_MIDDLE)
			return fits ? IB_OPCODE_RC_SEND_LAST_WITH_INVALIDATE :
				IB_OPCODE_RC_SEND_MIDDLE;
		else
			return fits ? IB_OPCODE_RC_SEND_ONLY_WITH_INVALIDATE :
				IB_OPCODE_RC_SEND_FIRST;
	case IB_WR_REG_MR:
	case IB_WR_LOCAL_INV:
		return opcode;
	}

	return -EINVAL;
}

static int next_opcode_uc(struct rxe_qp *qp, u32 opcode, int fits)
{
	switch (opcode) {
	case IB_WR_RDMA_WRITE:
		if (qp->req.opcode == IB_OPCODE_UC_RDMA_WRITE_FIRST ||
		    qp->req.opcode == IB_OPCODE_UC_RDMA_WRITE_MIDDLE)
			return fits ?
				IB_OPCODE_UC_RDMA_WRITE_LAST :
				IB_OPCODE_UC_RDMA_WRITE_MIDDLE;
		else
			return fits ?
				IB_OPCODE_UC_RDMA_WRITE_ONLY :
				IB_OPCODE_UC_RDMA_WRITE_FIRST;

	case IB_WR_RDMA_WRITE_WITH_IMM:
		if (qp->req.opcode == IB_OPCODE_UC_RDMA_WRITE_FIRST ||
		    qp->req.opcode == IB_OPCODE_UC_RDMA_WRITE_MIDDLE)
			return fits ?
				IB_OPCODE_UC_RDMA_WRITE_LAST_WITH_IMMEDIATE :
				IB_OPCODE_UC_RDMA_WRITE_MIDDLE;
		else
			return fits ?
				IB_OPCODE_UC_RDMA_WRITE_ONLY_WITH_IMMEDIATE :
				IB_OPCODE_UC_RDMA_WRITE_FIRST;

	case IB_WR_SEND:
		if (qp->req.opcode == IB_OPCODE_UC_SEND_FIRST ||
		    qp->req.opcode == IB_OPCODE_UC_SEND_MIDDLE)
			return fits ?
				IB_OPCODE_UC_SEND_LAST :
				IB_OPCODE_UC_SEND_MIDDLE;
		else
			return fits ?
				IB_OPCODE_UC_SEND_ONLY :
				IB_OPCODE_UC_SEND_FIRST;

	case IB_WR_SEND_WITH_IMM:
		if (qp->req.opcode == IB_OPCODE_UC_SEND_FIRST ||
		    qp->req.opcode == IB_OPCODE_UC_SEND_MIDDLE)
			return fits ?
				IB_OPCODE_UC_SEND_LAST_WITH_IMMEDIATE :
				IB_OPCODE_UC_SEND_MIDDLE;
		else
			return fits ?
				IB_OPCODE_UC_SEND_ONLY_WITH_IMMEDIATE :
				IB_OPCODE_UC_SEND_FIRST;
	}

	return -EINVAL;
}

static int next_opcode(struct rxe_qp *qp, struct rxe_send_wqe *wqe,
		       u32 opcode)
{
	int fits = (wqe->dma.resid <= qp->mtu);

	switch (qp_type(qp)) {
	case IB_QPT_RC:
		return next_opcode_rc(qp, opcode, fits);

	case IB_QPT_UC:
		return next_opcode_uc(qp, opcode, fits);

	case IB_QPT_SMI:
	case IB_QPT_UD:
	case IB_QPT_GSI:
		switch (opcode) {
		case IB_WR_SEND:
			return IB_OPCODE_UD_SEND_ONLY;

		case IB_WR_SEND_WITH_IMM:
			return IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE;
		}
		break;

	default:
		break;
	}

	return -EINVAL;
}

static inline int check_init_depth(struct rxe_qp *qp, struct rxe_send_wqe *wqe)
{
	int depth;

	if (wqe->has_rd_atomic)
		return 0;

	qp->req.need_rd_atomic = 1;
	depth = atomic_dec_return(&qp->req.rd_atomic);

	if (depth >= 0) {
		qp->req.need_rd_atomic = 0;
		wqe->has_rd_atomic = 1;
		return 0;
	}

	atomic_inc(&qp->req.rd_atomic);
	return -EAGAIN;
}

static inline int get_mtu(struct rxe_qp *qp)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);

	if ((qp_type(qp) == IB_QPT_RC) || (qp_type(qp) == IB_QPT_UC))
		return qp->mtu;

	return rxe->port.mtu_cap;
}

static struct sk_buff *init_req_packet(struct rxe_qp *qp,
				       struct rxe_send_wqe *wqe,
				       int opcode, int payload,
				       struct rxe_pkt_info *pkt)
{
	struct rxe_dev		*rxe = to_rdev(qp->ibqp.device);
	struct sk_buff		*skb;
	struct rxe_send_wr	*ibwr = &wqe->wr;
	struct rxe_av		*av;
	int			pad = (-payload) & 0x3;
	int			paylen;
	int			solicited;
	u16			pkey;
	u32			qp_num;
	int			ack_req;

	/* length from start of bth to end of icrc */
	paylen = rxe_opcode[opcode].length + payload + pad + RXE_ICRC_SIZE;

	/* pkt->hdr, rxe, port_num and mask are initialized in ifc
	 * layer
	 */
	pkt->opcode	= opcode;
	pkt->qp		= qp;
	pkt->psn	= qp->req.psn;
	pkt->mask	= rxe_opcode[opcode].mask;
	pkt->paylen	= paylen;
	pkt->wqe	= wqe;

	/* init skb */
	av = rxe_get_av(pkt);
	skb = rxe_init_packet(rxe, av, paylen, pkt);
	if (unlikely(!skb))
		return NULL;

	/* init bth */
	solicited = (ibwr->send_flags & IB_SEND_SOLICITED) &&
			(pkt->mask & RXE_END_MASK) &&
			((pkt->mask & (RXE_SEND_MASK)) ||
			(pkt->mask & (RXE_WRITE_MASK | RXE_IMMDT_MASK)) ==
			(RXE_WRITE_MASK | RXE_IMMDT_MASK));

	pkey = IB_DEFAULT_PKEY_FULL;

	qp_num = (pkt->mask & RXE_DETH_MASK) ? ibwr->wr.ud.remote_qpn :
					 qp->attr.dest_qp_num;

	ack_req = ((pkt->mask & RXE_END_MASK) ||
		(qp->req.noack_pkts++ > RXE_MAX_PKT_PER_ACK));
	if (ack_req)
		qp->req.noack_pkts = 0;

	bth_init(pkt, pkt->opcode, solicited, 0, pad, pkey, qp_num,
		 ack_req, pkt->psn);

	/* init optional headers */
	if (pkt->mask & RXE_RETH_MASK) {
		reth_set_rkey(pkt, ibwr->wr.rdma.rkey);
		reth_set_va(pkt, wqe->iova);
		reth_set_len(pkt, wqe->dma.resid);
	}

	if (pkt->mask & RXE_IMMDT_MASK)
		immdt_set_imm(pkt, ibwr->ex.imm_data);

	if (pkt->mask & RXE_IETH_MASK)
		ieth_set_rkey(pkt, ibwr->ex.invalidate_rkey);

	if (pkt->mask & RXE_ATMETH_MASK) {
		atmeth_set_va(pkt, wqe->iova);
		if (opcode == IB_OPCODE_RC_COMPARE_SWAP ||
		    opcode == IB_OPCODE_RD_COMPARE_SWAP) {
			atmeth_set_swap_add(pkt, ibwr->wr.atomic.swap);
			atmeth_set_comp(pkt, ibwr->wr.atomic.compare_add);
		} else {
			atmeth_set_swap_add(pkt, ibwr->wr.atomic.compare_add);
		}
		atmeth_set_rkey(pkt, ibwr->wr.atomic.rkey);
	}

	if (pkt->mask & RXE_DETH_MASK) {
		if (qp->ibqp.qp_num == 1)
			deth_set_qkey(pkt, GSI_QKEY);
		else
			deth_set_qkey(pkt, ibwr->wr.ud.remote_qkey);
		deth_set_sqp(pkt, qp->ibqp.qp_num);
	}

	return skb;
}

static int fill_packet(struct rxe_qp *qp, struct rxe_send_wqe *wqe,
		       struct rxe_pkt_info *pkt, struct sk_buff *skb,
		       int paylen)
{
	struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
	u32 crc = 0;
	u32 *p;
	int err;

	err = rxe_prepare(pkt, skb, &crc);
	if (err)
		return err;

	if (pkt->mask & RXE_WRITE_OR_SEND) {
		if (wqe->wr.send_flags & IB_SEND_INLINE) {
			u8 *tmp = &wqe->dma.inline_data[wqe->dma.sge_offset];

			crc = rxe_crc32(rxe, crc, tmp, paylen);
			memcpy(payload_addr(pkt), tmp, paylen);

			wqe->dma.resid -= paylen;
			wqe->dma.sge_offset += paylen;
		} else {
			err = copy_data(qp->pd, 0, &wqe->dma,
					payload_addr(pkt), paylen,
					from_mem_obj,
					&crc);
			if (err)
				return err;
		}
		if (bth_pad(pkt)) {
			u8 *pad = payload_addr(pkt) + paylen;

			memset(pad, 0, bth_pad(pkt));
			crc = rxe_crc32(rxe, crc, pad, bth_pad(pkt));
		}
	}
	p = payload_addr(pkt) + paylen + bth_pad(pkt);

	*p = ~crc;

	return 0;
}

static void update_wqe_state(struct rxe_qp *qp,
		struct rxe_send_wqe *wqe,
		struct rxe_pkt_info *pkt)
{
	if (pkt->mask & RXE_END_MASK ) {
		if (qp_type(qp) == IB_QPT_RC)
			wqe->state = wqe_state_pending;
	} else {
		wqe->state = wqe_state_processing;
	}
}

static void update_wqe_psn(struct rxe_qp *qp,
			   struct rxe_send_wqe *wqe,
			   struct rxe_pkt_info *pkt,
			   int payload)
{
	/* number of packets left to send including current one */
	int num_pkt = (wqe->dma.resid + payload + qp->mtu - 1) / qp->mtu;

	/* handle zero length packet case */
	if (num_pkt == 0)
		num_pkt = 1;

	if (pkt->mask & RXE_START_MASK) {
		wqe->first_psn = qp->req.psn;
		wqe->last_psn = (qp->req.psn + num_pkt - 1) & BTH_PSN_MASK;
	}

	if (pkt->mask & RXE_READ_MASK)
		qp->req.psn = (wqe->first_psn + num_pkt) & BTH_PSN_MASK;
	else
		//qp->req.psn = (qp->req.psn + 1) & BTH_PSN_MASK;
		qp->req.nextSNtoSend = (qp->req.nextSNtoSend + 1) & BTH_PSN_MASK;//TODO:check nextSNtoSend flag is set correctly.
}

static void save_state(struct rxe_send_wqe *wqe,
		       struct rxe_qp *qp,
		       struct rxe_send_wqe *rollback_wqe,
		       u32 *rollback_psn, u32 *rollback_nextpsn)
{
	rollback_wqe->state     = wqe->state;
	rollback_wqe->first_psn = wqe->first_psn;
	rollback_wqe->last_psn  = wqe->last_psn;
	*rollback_psn		= qp->req.psn;
	*rollback_nextpsn = qp->req.nextSNtoSend;
}

static void rollback_state(struct rxe_send_wqe *wqe,
			   struct rxe_qp *qp,
			   struct rxe_send_wqe *rollback_wqe,
			   u32 rollback_psn, u32 rollback_nextpsn)
{
	wqe->state     = rollback_wqe->state;
	wqe->first_psn = rollback_wqe->first_psn;
	wqe->last_psn  = rollback_wqe->last_psn;
	qp->req.psn    = rollback_psn;
	qp->req.nextSNtoSend = rollback_nextpsn;
}

static void update_state(struct rxe_qp *qp, struct rxe_send_wqe *wqe,
			 struct rxe_pkt_info *pkt, int payload)
{
	qp->req.opcode = pkt->opcode;

	if (pkt->mask & RXE_END_MASK){
		//pr_alert("end entered");
		qp->req.wqe_index = next_index(qp->sq.queue, qp->req.wqe_index);
		//pr_alert("wqe index advanced, req opcode is %s.", rxe_opcode[qp->req.opcode].name);
	}

	qp->need_req_skb = 0;

	if (qp->qp_timeout_jiffies && !timer_pending(&qp->retrans_timer))
		mod_timer(&qp->retrans_timer,
			  jiffies + qp->qp_timeout_jiffies);
}

int rxe_requester(void *arg)
{
	struct rxe_qp *qp = (struct rxe_qp *)arg;
	struct rxe_pkt_info pkt;
	struct sk_buff *skb;
	struct rxe_send_wqe *wqe;
	enum rxe_hdr_mask mask;
	int payload;
	int mtu;
	int opcode;
	int ret;
	struct rxe_send_wqe rollback_wqe;
	u32 rollback_psn;
	u32 rollback_nextpsn;
	rxe_add_ref(qp);
	//qp->req.nextSNtoSend = qp->req.psn;

next_wqe:

	wqe = req_next_wqe(qp);

	if (unlikely(!qp->valid || qp->req.state == QP_STATE_ERROR))
		goto exit;

	if (unlikely(qp->req.state == QP_STATE_RESET)) {
		pr_alert("qp reset");
		qp->req.wqe_index = consumer_index(qp->sq.queue);
		qp->req.opcode = -1;
		qp->req.need_rd_atomic = 0;
		qp->req.wait_psn = 0;
		qp->req.need_retry = 0;
		goto exit;
	}

	if (unlikely(qp->req.need_retry)) {
		pr_alert("qp retry");
		req_retry(qp);
		qp->req.need_retry = 0;
	}
	//if a packet has been marked for retransmission and if it has not already been acked,
	//set it as the packet to be sent, update the recovery sequence, 
	//disable further retransmission, and set flag to find new hole in bitmap.
	//inRecovery is set to true, if the retransmit SN is equal to cumulative ack.
	if((qp->req.doRetransmit) && (qp->req.retransmitSN >= qp->comp.psn)) {
			pr_alert("Here.\n");
			if(qp->req.retransmitSN == qp->comp.psn) qp->req.inRecovery = true;
			qp->req.psn = qp->req.retransmitSN;
			qp->req.recoverSN = qp->req.nextSNtoSend - 1;
			qp->req.doRetransmit = false;
			qp->req.findNewHole = true;
			unsigned int maski = wr_opcode_mask(wqe->wr.opcode, qp);
			u32 npsn = (qp->comp.psn - wqe->first_psn) &
					BTH_PSN_MASK;
			retry_first_write_send(qp, wqe, maski, npsn);//set dma parameters.
			//TODO:Maybe need to set wqe index as well?
			goto next_wqe;

	} else {
		//pr_alert("Righty.\n");
		//if packet marked for retransmission has been acked, disable flag to find new hole.
		if(qp->req.doRetransmit) qp->req.findNewHole = false;
		qp->req.doRetransmit = false;
		//prepare transmission of a new packet, 
		//if the number of packets in flight is smaller than the maxCap (set to BDP). Maybe use RXE_INFLIGHT_SKBS_PER_QP_HIGH?
		//pr_alert("next %d, last ack is %d", qp->req.nextSNtoSend, qp->comp.psn);
		/*if(qp->req.nextSNtoSend  < BDP + qp->comp.psn) {
			pr_alert("req.psn is %d. nextSN is %d.", qp->req.psn, qp->req.nextSNtoSend);
			qp->req.psn = qp->req.nextSNtoSend;
		} */
		//pr_alert("req.psn is %d. nextSN is %d.", qp->req.psn, qp->req.nextSNtoSend);
		qp->req.psn = qp->req.nextSNtoSend;
	}

	//if the flag to find new hole is set, search for the next hole in the bitmap.
	if(qp->req.findNewHole) {
		pr_alert("find new hole");
		__uint128_t tempBitmap;// BDP-bit
		u32 startidx;// 32 bit
		if(qp->req.retransmitSN >= qp->comp.psn) {
			tempBitmap = qp->comp.sack_bitmap >> (qp->req.retransmitSN - qp->comp.psn + 1); //shift out the used part.
			startidx = qp->req.retransmitSN + 1;
		} else {
			//reset the bitmap
			tempBitmap = qp->comp.sack_bitmap;
			startidx = qp->comp.psn;
		}
		u8 nextHole = 0; // LOGBDP bit
		bool holeFound;
		holeFound = findNextHoleTx(tempBitmap, nextHole);
		//if hole is found, prepare the sequence for retransmission.
		if(holeFound) {
			qp->req.retransmitSN = nextHole + startidx;
			qp->req.doRetransmit = true;
		} 
		qp->req.findNewHole = false;
	}

	
	if (unlikely(!wqe)){
		pr_alert("error !wqe");
		goto exit;
	}
		

	if (wqe->mask & WR_REG_MASK) {
		pr_alert("reg wr.\n");
		if (wqe->wr.opcode == IB_WR_LOCAL_INV) {
			struct rxe_dev *rxe = to_rdev(qp->ibqp.device);
			struct rxe_mem *rmr;

			rmr = rxe_pool_get_index(&rxe->mr_pool,
						 wqe->wr.ex.invalidate_rkey >> 8);
			if (!rmr) {
				pr_err("No mr for key %#x\n",
				       wqe->wr.ex.invalidate_rkey);
				wqe->state = wqe_state_error;
				wqe->status = IB_WC_MW_BIND_ERR;
				goto exit;
			}
			rmr->state = RXE_MEM_STATE_FREE;
			rxe_drop_ref(rmr);
			wqe->state = wqe_state_done;
			wqe->status = IB_WC_SUCCESS;
		} else if (wqe->wr.opcode == IB_WR_REG_MR) {
			struct rxe_mem *rmr = to_rmr(wqe->wr.wr.reg.mr);

			rmr->state = RXE_MEM_STATE_VALID;
			rmr->access = wqe->wr.wr.reg.access;
			rmr->ibmr.lkey = wqe->wr.wr.reg.key;
			rmr->ibmr.rkey = wqe->wr.wr.reg.key;
			rmr->iova = wqe->wr.wr.reg.mr->iova;
			wqe->state = wqe_state_done;
			wqe->status = IB_WC_SUCCESS;
		} else {
			pr_alert("error wr");
			goto exit;
		}
		if ((wqe->wr.send_flags & IB_SEND_SIGNALED) ||
		    qp->sq_sig_type == IB_SIGNAL_ALL_WR)
			rxe_run_task(&qp->comp.task, 1);
		qp->req.wqe_index = next_index(qp->sq.queue,
						qp->req.wqe_index);
		goto next_wqe;
	}

	
	/*if (unlikely(qp_type(qp) == IB_QPT_RC &&
		psn_compare(qp->req.nextSNtoSend, (qp->comp.psn +
				RXE_MAX_UNACKED_PSNS)) > 0)) {
					pr_alert("wait set.");
		qp->req.wait_psn = 1;
		goto exit;
	}*/

	/* Limit the number of inflight SKBs per QP */
	/*if (unlikely(atomic_read(&qp->skb_out) >
		     RXE_INFLIGHT_SKBS_PER_QP_HIGH)) {
				pr_alert("limit set");
		qp->need_req_skb = 1;
		goto exit;
	}*/
	if (unlikely(atomic_read(&qp->skb_out) >
		     BDP)) {
				pr_alert("limit set");
		qp->need_req_skb = 1;
		goto exit;
	}

	opcode = next_opcode(qp, wqe, wqe->wr.opcode);
	if (unlikely(opcode < 0)) {
		pr_alert("opcode wrong");
		wqe->status = IB_WC_LOC_QP_OP_ERR;
		goto exit;
	}

	mask = rxe_opcode[opcode].mask;
	if (unlikely(mask & RXE_READ_OR_ATOMIC)) {
		if (check_init_depth(qp, wqe))
			goto exit;
	}

	mtu = get_mtu(qp);
	payload = (mask & RXE_WRITE_OR_SEND) ? wqe->dma.resid : 0;
	if (payload > mtu) {
		if (qp_type(qp) == IB_QPT_UD) {
			/* C10-93.1.1: If the total sum of all the buffer lengths specified for a
			 * UD message exceeds the MTU of the port as returned by QueryHCA, the CI
			 * shall not emit any packets for this message. Further, the CI shall not
			 * generate an error due to this condition.
			 */

			/* fake a successful UD send */
			wqe->first_psn = qp->req.psn;
			wqe->last_psn = qp->req.psn;
			qp->req.psn = (qp->req.psn + 1) & BTH_PSN_MASK;
			qp->req.opcode = IB_OPCODE_UD_SEND_ONLY;
			qp->req.wqe_index = next_index(qp->sq.queue,
						       qp->req.wqe_index);
			wqe->state = wqe_state_done;
			wqe->status = IB_WC_SUCCESS;
			__rxe_do_task(&qp->comp.task);
			rxe_drop_ref(qp);
			return 0;
		}
		payload = mtu;
	}

	pr_info("init req.psn is %d. nextSN is %d.", qp->req.psn, qp->req.nextSNtoSend);
	skb = init_req_packet(qp, wqe, opcode, payload, &pkt);
	if (unlikely(!skb)) {
		pr_err("qp#%d Failed allocating skb\n", qp_num(qp));
		goto err;
	}

	if (fill_packet(qp, wqe, &pkt, skb, payload)) {
		pr_debug("qp#%d Error during fill packet\n", qp_num(qp));
		kfree_skb(skb);
		goto err;
	}

	/*
	 * To prevent a race on wqe access between requester and completer,
	 * wqe members state and psn need to be set before calling
	 * rxe_xmit_packet().
	 * Otherwise, completer might initiate an unjustified retry flow.
	 */
	save_state(wqe, qp, &rollback_wqe, &rollback_psn, &rollback_nextpsn);
	update_wqe_state(qp, wqe, &pkt);//TODO:maybe set state based on sack?
	update_wqe_psn(qp, wqe, &pkt, payload);
	ret = rxe_xmit_packet(qp, &pkt, skb);
	pr_info("xmit pkt psn %d", pkt.psn);
	if (ret) {
		qp->need_req_skb = 1;

		rollback_state(wqe, qp, &rollback_wqe, rollback_psn, rollback_nextpsn);

		if (ret == -EAGAIN) {
			rxe_run_task(&qp->req.task, 1);
			goto exit;
		}

		goto err;
	}

	update_state(qp, wqe, &pkt, payload);

	goto next_wqe;

err:
	wqe->status = IB_WC_LOC_PROT_ERR;
	wqe->state = wqe_state_error;
	__rxe_do_task(&qp->comp.task);

exit:
	rxe_drop_ref(qp);
	return -EAGAIN;
}
