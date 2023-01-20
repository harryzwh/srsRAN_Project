/*
 *
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "f1u_bearer_impl.h"

using namespace srsgnb;
using namespace srs_cu_up;

f1u_bearer_impl::f1u_bearer_impl(drb_id_t                  drb_id_,
                                 f1u_tx_pdu_notifier&      tx_pdu_notifier_,
                                 f1u_rx_delivery_notifier& rx_delivery_notifier_,
                                 f1u_rx_sdu_notifier&      rx_sdu_notifier_) :
  logger("F1-U", 0, lcid_t(LCID_MIN_DRB)), // TODO: What do we use here? There is no LCID in the CU-UP
  drb_id(drb_id_),
  tx_pdu_notifier(tx_pdu_notifier_),
  rx_delivery_notifier(rx_delivery_notifier_),
  rx_sdu_notifier(rx_sdu_notifier_)
{
  (void)drb_id;
  (void)tx_pdu_notifier;
  (void)rx_delivery_notifier;
  (void)rx_sdu_notifier;
}

void f1u_bearer_impl::handle_pdu(nru_ul_message msg)
{
  logger.log_debug("F1-U bearer received PDU");
  if (!msg.t_pdu.empty()) {
    rx_sdu_notifier.on_new_sdu(std::move(msg.t_pdu));
  }
  // TODO: notify successfully delivered PDCP SNs
}

void f1u_bearer_impl::handle_sdu(pdcp_tx_pdu sdu)
{
  logger.log_debug("F1-U bearer received SDU with PDCP count={}", sdu.pdcp_count);
  nru_dl_message msg = {};
  msg.t_pdu          = std::move(sdu.buf);
  msg.pdcp_count     = sdu.pdcp_count;
  tx_pdu_notifier.on_new_pdu(std::move(msg));
}

void f1u_bearer_impl::discard_sdu(uint32_t count)
{
  logger.log_debug("F1-U bearer received order to discard SDU with count={}", count);
  nru_dl_message msg                               = {};
  msg.dl_user_data.dl_discard_blocks               = true;
  msg.dl_user_data.dl_discard_nof_blocks           = 1;
  msg.dl_user_data.discard_blocks[0].pdcp_sn_start = count;
  msg.dl_user_data.discard_blocks[0].block_size    = 1;
  tx_pdu_notifier.on_new_pdu(std::move(msg));
}
