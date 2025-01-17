/*
 *
 * Copyright 2021-2024 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "du_high_env_simulator.h"
#include "tests/test_doubles/f1ap/f1ap_test_message_validators.h"
#include "tests/test_doubles/mac/mac_test_messages.h"
#include "tests/unittests/f1ap/du/f1ap_du_test_helpers.h"
#include "tests/unittests/scheduler/test_utils/result_test_helpers.h"
#include "srsran/asn1/f1ap/common.h"
#include "srsran/du/du_cell_config_helpers.h"
#include "srsran/du_high/du_high_factory.h"
#include "srsran/support/test_utils.h"

using namespace srsran;
using namespace srs_du;

static f1ap_message create_f1_setup_response()
{
  f1ap_message f1ap_msg;
  f1ap_msg.pdu.set_successful_outcome().load_info_obj(ASN1_F1AP_ID_F1_SETUP);
  f1ap_msg.pdu.successful_outcome().value.f1_setup_resp()->cells_to_be_activ_list_present = true;
  f1ap_msg.pdu.successful_outcome().value.f1_setup_resp()->cells_to_be_activ_list.resize(1);
  auto& cell = f1ap_msg.pdu.successful_outcome().value.f1_setup_resp()->cells_to_be_activ_list[0];
  cell.load_info_obj(ASN1_F1AP_ID_CELLS_TO_BE_ACTIV_LIST_ITEM);
  cell->cells_to_be_activ_list_item().nr_cgi.plmn_id.from_string("00f101");
  cell->cells_to_be_activ_list_item().nr_cgi.nr_cell_id.from_string("000000000000101111000110000101001110");
  return f1ap_msg;
}

namespace {

/// Dummy F1AP Tx PDU notifier that emulates the CU-CP side.
class dummy_du_f1ap_tx_pdu_notifier : public f1ap_message_notifier
{
public:
  dummy_du_f1ap_tx_pdu_notifier(task_executor&                         test_exec_,
                                std::vector<f1ap_message>&             last_f1ap_msgs_,
                                std::unique_ptr<f1ap_message_notifier> du_rx_notifier_) :
    test_exec(test_exec_), last_f1ap_msgs(last_f1ap_msgs_), du_rx_notifier(std::move(du_rx_notifier_))
  {
  }

  void on_new_message(const f1ap_message& msg) override
  {
    if (msg.pdu.type().value == asn1::f1ap::f1ap_pdu_c::types_opts::init_msg and
        msg.pdu.init_msg().proc_code == ASN1_F1AP_ID_F1_SETUP) {
      // Auto-schedule CU response.
      du_rx_notifier->on_new_message(create_f1_setup_response());
    }

    // Dispatch storing of message to test main thread so it can be safely checked in the test function body.
    bool result = test_exec.execute([this, msg]() {
      logger.info("Received F1 UL message with {}", msg.pdu.type().to_string());
      last_f1ap_msgs.push_back(msg);
    });
    EXPECT_TRUE(result);
  }

  srslog::basic_logger&                  logger = srslog::fetch_basic_logger("TEST");
  task_executor&                         test_exec;
  std::vector<f1ap_message>&             last_f1ap_msgs;
  std::unique_ptr<f1ap_message_notifier> du_rx_notifier;
};

} // namespace

bool srsran::srs_du::is_ue_context_release_complete_valid(const f1ap_message& msg,
                                                          gnb_du_ue_f1ap_id_t du_ue_id,
                                                          gnb_cu_ue_f1ap_id_t cu_ue_id)
{
  if (not(msg.pdu.type() == asn1::f1ap::f1ap_pdu_c::types_opts::successful_outcome and
          msg.pdu.successful_outcome().proc_code == ASN1_F1AP_ID_UE_CONTEXT_RELEASE)) {
    return false;
  }
  const asn1::f1ap::ue_context_release_complete_s& resp =
      msg.pdu.successful_outcome().value.ue_context_release_complete();
  return (gnb_cu_ue_f1ap_id_t)resp->gnb_cu_ue_f1ap_id == cu_ue_id and (gnb_du_ue_f1ap_id_t)
                                                                              resp->gnb_du_ue_f1ap_id == du_ue_id;
}

phy_cell_test_dummy::phy_cell_test_dummy(task_executor& exec_) : test_exec(exec_) {}

void phy_cell_test_dummy::on_new_downlink_scheduler_results(const mac_dl_sched_result& dl_res)
{
  cached_dl_res = dl_res;
}

void phy_cell_test_dummy::on_new_downlink_data(const mac_dl_data_result& dl_data)
{
  cached_dl_data = dl_data;
}

void phy_cell_test_dummy::on_new_uplink_scheduler_results(const mac_ul_sched_result& ul_res)
{
  cached_ul_res = ul_res;
}

void phy_cell_test_dummy::on_cell_results_completion(slot_point slot)
{
  bool result =
      test_exec.execute([this,
                         slot,
                         dl_sched_res = cached_dl_res.has_value() ? *cached_dl_res->dl_res : dl_sched_result{},
                         ul_sched_res = cached_ul_res.has_value() ? *cached_ul_res->ul_res : ul_sched_result{},
                         dl_res_copy  = cached_dl_res,
                         dl_data_copy = cached_dl_data,
                         ul_res_copy  = cached_ul_res]() mutable {
        last_slot_res = slot;
        last_dl_res   = dl_res_copy;
        last_dl_data  = dl_data_copy;
        last_ul_res   = ul_res_copy;
        if (last_dl_res.has_value()) {
          last_dl_sched_res   = dl_sched_res;
          last_dl_res->dl_res = &last_dl_sched_res;
        }
        if (last_ul_res.has_value()) {
          last_ul_sched_res   = ul_sched_res;
          last_ul_res->ul_res = &last_ul_sched_res;
        }
      });
  EXPECT_TRUE(result);
  cached_dl_res  = {};
  cached_dl_data = {};
  cached_ul_res  = {};
}

dummy_f1c_test_client::dummy_f1c_test_client(task_executor& test_exec_) : test_exec(test_exec_) {}

std::unique_ptr<f1ap_message_notifier>
dummy_f1c_test_client::handle_du_connection_request(std::unique_ptr<f1ap_message_notifier> du_rx_pdu_notifier)
{
  return std::make_unique<dummy_du_f1ap_tx_pdu_notifier>(test_exec, last_f1ap_msgs, std::move(du_rx_pdu_notifier));
}

static void init_loggers()
{
  srslog::fetch_basic_logger("MAC", true).set_level(srslog::basic_levels::debug);
  srslog::fetch_basic_logger("SCHED", true).set_level(srslog::basic_levels::debug);
  srslog::fetch_basic_logger("RLC").set_level(srslog::basic_levels::info);
  srslog::fetch_basic_logger("DU-MNG").set_level(srslog::basic_levels::debug);
  srslog::fetch_basic_logger("DU-F1").set_level(srslog::basic_levels::debug);
  srslog::fetch_basic_logger("ASN1").set_level(srslog::basic_levels::debug);
  srslog::fetch_basic_logger("TEST").set_level(srslog::basic_levels::debug);
  srslog::init();
}

du_high_env_simulator::du_high_env_simulator(du_high_env_sim_params params) :
  cu_notifier(workers.test_worker),
  phy(params.nof_cells, workers.test_worker),
  du_high_cfg([this, params]() {
    init_loggers();

    du_high_configuration cfg{};
    cfg.exec_mapper  = &workers.exec_mapper;
    cfg.f1c_client   = &cu_notifier;
    cfg.f1u_gw       = &cu_up_sim;
    cfg.phy_adapter  = &phy;
    cfg.timers       = &timers;
    cfg.gnb_du_id    = 0;
    cfg.gnb_du_name  = "srsdu";
    cfg.du_bind_addr = transport_layer_address::create_from_string("127.0.0.1");

    cfg.cells.reserve(params.nof_cells);
    cell_config_builder_params builder_params;
    for (unsigned i = 0; i < params.nof_cells; ++i) {
      builder_params.pci = (pci_t)i;
      cfg.cells.push_back(config_helpers::make_default_du_cell_config(builder_params));
      cfg.cells.back().nr_cgi.nci = i;
    }

    cfg.qos       = config_helpers::make_default_du_qos_config_list(/* warn_on_drop */ true, 0);
    cfg.sched_cfg = config_helpers::make_default_scheduler_expert_config();
    cfg.mac_cfg   = mac_expert_config{.configs = {{10000, 10000, 10000}}};
    cfg.mac_p     = &mac_pcap;
    cfg.rlc_p     = &rlc_pcap;

    return cfg;
  }()),
  du_hi(make_du_high(du_high_cfg)),
  next_slot(0, test_rgen::uniform_int<unsigned>(0, 10239))
{
  // Start DU and try to connect to CU.
  du_hi->start();

  // Ensure the result is saved in the notifier.
  run_until([this]() { return not cu_notifier.last_f1ap_msgs.empty(); });
}

du_high_env_simulator::~du_high_env_simulator()
{
  du_hi->stop();

  // Stop workers before starting to take down other components.
  workers.stop();
}

bool du_high_env_simulator::run_until(unique_function<bool()> condition, unsigned max_slot_count)
{
  for (unsigned count = 0; count != max_slot_count; ++count) {
    if (condition()) {
      return true;
    }
    run_slot();
  }
  return false;
}

bool du_high_env_simulator::add_ue(rnti_t rnti, du_cell_index_t cell_index)
{
  if (ues.count(rnti) > 0) {
    return false;
  }

  cu_notifier.last_f1ap_msgs.clear();

  // Send UL-CCCH message.
  du_hi->get_pdu_handler().handle_rx_data_indication(test_helpers::create_ccch_message(next_slot, rnti, cell_index));

  // Wait for Init UL RRC Message to come out of the F1AP.
  bool ret =
      run_until([this]() { return not cu_notifier.last_f1ap_msgs.empty(); }, 1000 * (next_slot.numerology() + 1));
  if (not ret or not test_helpers::is_init_ul_rrc_msg_transfer_valid(cu_notifier.last_f1ap_msgs.back(), rnti)) {
    return false;
  }

  gnb_du_ue_f1ap_id_t du_ue_id = int_to_gnb_du_ue_f1ap_id(
      cu_notifier.last_f1ap_msgs.back().pdu.init_msg().value.init_ul_rrc_msg_transfer()->gnb_du_ue_f1ap_id);
  gnb_cu_ue_f1ap_id_t cu_ue_id = int_to_gnb_cu_ue_f1ap_id(next_cu_ue_id++);
  EXPECT_TRUE(ues.insert(std::make_pair(rnti, ue_sim_context{rnti, du_ue_id, cu_ue_id, cell_index})).second);

  return ret;
}

bool du_high_env_simulator::run_rrc_setup(rnti_t rnti)
{
  auto it = ues.find(rnti);
  if (it == ues.end()) {
    return false;
  }
  const ue_sim_context& u        = it->second;
  const auto&           phy_cell = phy.cells[u.pcell_index];

  // Send DL RRC Message which contains RRC Setup.
  f1ap_message msg = generate_dl_rrc_message_transfer(
      *u.du_ue_id, *u.cu_ue_id, srb_id_t::srb0, byte_buffer::create({0x1, 0x2, 0x3}).value());
  du_hi->get_f1ap_message_handler().handle_message(msg);

  // Wait for contention resolution to be sent to the PHY.
  bool ret = run_until([&]() {
    if (phy_cell.last_dl_res.has_value() and phy_cell.last_dl_res.value().dl_res != nullptr) {
      auto& dl_res = *phy_cell.last_dl_res.value().dl_res;
      for (const dl_msg_alloc& grant : dl_res.ue_grants) {
        if (grant.pdsch_cfg.rnti == rnti and
            std::any_of(grant.tb_list[0].lc_chs_to_sched.begin(),
                        grant.tb_list[0].lc_chs_to_sched.end(),
                        [](const dl_msg_lc_info& lc_grant) { return lc_grant.lcid == lcid_dl_sch_t::UE_CON_RES_ID; })) {
          return true;
        }
      }
    }
    return false;
  });
  if (not ret) {
    return false;
  }

  // Wait for Msg4 to be ACKed.
  unsigned msg4_k1 = 4;
  for (unsigned i = 0; i != msg4_k1; ++i) {
    run_slot();
  }

  // UE sends RRC Setup Complete. Wait until F1AP forwards UL RRC Message to CU-CP.
  cu_notifier.last_f1ap_msgs.clear();
  du_hi->get_pdu_handler().handle_rx_data_indication(
      test_helpers::create_pdu_with_sdu(next_slot, rnti, lcid_t::LCID_SRB1));
  ret = run_until([this]() { return not cu_notifier.last_f1ap_msgs.empty(); });
  if (not ret or not test_helpers::is_ul_rrc_msg_transfer_valid(cu_notifier.last_f1ap_msgs.back(), srb_id_t::srb1)) {
    return false;
  }
  return true;
}

bool du_high_env_simulator::run_ue_context_setup(rnti_t rnti)
{
  auto it = ues.find(rnti);
  if (it == ues.end()) {
    return false;
  }

  // DU receives UE Context Setup Request.
  cu_notifier.last_f1ap_msgs.clear();
  f1ap_message                            msg = generate_ue_context_setup_request({drb_id_t::drb1});
  asn1::f1ap::ue_context_setup_request_s& cmd = msg.pdu.init_msg().value.ue_context_setup_request();
  cmd->gnb_du_ue_f1ap_id                      = gnb_du_ue_f1ap_id_to_uint(*it->second.du_ue_id);
  cmd->gnb_cu_ue_f1ap_id                      = gnb_cu_ue_f1ap_id_to_uint(*it->second.cu_ue_id);
  cmd->drbs_to_be_setup_list[0]
      .value()
      .drbs_to_be_setup_item()
      .qos_info.choice_ext()
      .value()
      .drb_info()
      .drb_qos.qos_characteristics.non_dyn_5qi()
      .five_qi = 7U;
  cmd->drbs_to_be_setup_list[0].value().drbs_to_be_setup_item().rlc_mode.value =
      asn1::f1ap::rlc_mode_opts::rlc_um_bidirectional;
  this->du_hi->get_f1ap_message_handler().handle_message(msg);

  // Wait until DU sends UE Context Setup Response and the whole RRC container is scheduled.
  const unsigned MAX_SLOT_COUNT   = 1000;
  const unsigned srb1_pdu_size    = cmd->rrc_container.size();
  unsigned       srb1_bytes_sched = 0;
  for (unsigned i = 0; i != MAX_SLOT_COUNT and (srb1_bytes_sched < srb1_pdu_size or cu_notifier.last_f1ap_msgs.empty());
       ++i) {
    run_slot();

    // Sum all the bytes scheduled for SRB1.
    du_cell_index_t     cell_idx = it->second.pcell_index;
    const dl_msg_alloc* pdsch =
        find_ue_pdsch_with_lcid(rnti, LCID_SRB1, phy.cells[cell_idx].last_dl_res.value().dl_res->ue_grants);
    if (pdsch != nullptr) {
      for (const dl_msg_lc_info& lc_grant : pdsch->tb_list[0].lc_chs_to_sched) {
        if (lc_grant.lcid == LCID_SRB1) {
          srb1_bytes_sched += lc_grant.sched_bytes;
        }
      }
    }
  }

  if (cu_notifier.last_f1ap_msgs.size() != 1) {
    // Response not sent back to CU-CP or too many responses were sent.
    test_logger.info("STATUS: No UE Context Setup Response was sent back to the CU-CP");
    return false;
  }
  if (not test_helpers::is_ue_context_setup_response_valid(cu_notifier.last_f1ap_msgs.back())) {
    // Bad response.
    test_logger.error("STATUS: UE Context Setup Response sent back to the CU-CP is not valid");
    return false;
  }
  if (srb1_bytes_sched < srb1_pdu_size) {
    // Not enough SRB1 bytes were scheduled for the RRC container.
    test_logger.error("STATUS: Not enough SRB1 bytes were scheduled for the RRC container ({} < {})",
                      srb1_bytes_sched,
                      srb1_pdu_size);
    return false;
  }

  return true;
}

bool du_high_env_simulator::force_ue_fallback(rnti_t rnti)
{
  auto it = ues.find(rnti);
  if (it == ues.end()) {
    return false;
  }
  const ue_sim_context& u        = it->second;
  const auto&           phy_cell = phy.cells[u.pcell_index];

  // For the UE to transition to non-fallback mode, the GNB needs to receive either an SR or CSI pkus then 2 CRC = OK.
  // In the following, we force 2 SRs, which in turn will 2 PUSCH. We also need to force 2 CRC=OK corresponding to each
  // of the PUSCH.
  for (unsigned crc_cnt = 0; crc_cnt != 2; ++crc_cnt) {
    const unsigned max_slot_count = 100;
    // Run until the slot the SR PUCCH is scheduled for.
    optional<slot_point> slot_sr = nullopt;
    for (unsigned count = 0; count != max_slot_count; ++count) {
      const bool found_sr = phy_cell.last_ul_res.has_value() and phy_cell.last_ul_res.value().ul_res != nullptr and
                            find_ue_pucch_with_sr(rnti, phy_cell.last_ul_res.value().ul_res->pucchs) != nullptr;
      if (found_sr) {
        slot_sr = next_slot;
        break;
      }
      run_slot();
    }

    // Enforce an UCI indication for the SR; this will trigger the SRB1 fallback scheduler to allocate a PUSCH grant.
    if (slot_sr.has_value()) {
      static_vector<pucch_info, 1> pucchs{
          pucch_info{.crnti    = rnti,
                     .format   = pucch_format::FORMAT_1,
                     .format_1 = {.sr_bits = sr_nof_bits::one, .harq_ack_nof_bits = 0}}};
      mac_uci_indication_message uci_ind = test_helpers::create_uci_indication(*slot_sr, pucchs);
    } else {
      return false;
    }

    // Search for the PUSCH grant and force a CRC indication with OK.
    for (unsigned count = 0; count != max_slot_count; ++count) {
      const ul_sched_info* pusch = nullptr;
      if (phy_cell.last_ul_res.has_value() and phy_cell.last_ul_res.value().ul_res != nullptr) {
        pusch = find_ue_pusch(rnti, phy_cell.last_ul_res.value().ul_res->puschs);
        if (pusch != nullptr) {
          du_hi->get_control_info_handler(it->second.pcell_index)
              .handle_crc(test_helpers::create_crc_indication(
                  phy_cell.last_ul_res->slot, rnti, to_harq_id(pusch->pusch_cfg.harq_id)));
          if (crc_cnt == 1) {
            return true;
          }
          break;
        }
      }
      run_slot();
    }
  }
  return false;
}

void du_high_env_simulator::run_slot()
{
  for (unsigned i = 0; i != du_high_cfg.cells.size(); ++i) {
    // Signal slot indication to l2.
    du_hi->get_slot_handler(to_du_cell_index(i)).handle_slot_indication(next_slot);

    // Wait for slot indication to be processed and the l2 results to be sent back to the l1 (in this case, the test
    // main thread).
    const unsigned MAX_COUNT = 100000;
    for (unsigned count = 0; count < MAX_COUNT and phy.cells[i].last_slot_res != next_slot; ++count) {
      // Process tasks dispatched to the test main thread (e.g. L2 slot result)
      workers.test_worker.run_pending_tasks();

      // Wait for tasks to arrive to test thread.
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_EQ(phy.cells[i].last_slot_res, next_slot)
        << fmt::format("Slot={} failed to be processed (last processed slot={}). Is there a deadlock?",
                       next_slot,
                       phy.cells[i].last_slot_res);
    const optional<mac_dl_sched_result>& dl_result = phy.cells[i].last_dl_res;
    if (dl_result.has_value()) {
      EXPECT_TRUE(dl_result->slot == next_slot);
    }

    // Process results.
    handle_slot_results(to_du_cell_index(i));
  }

  // Advance to next slot.
  ++next_slot;
}

void du_high_env_simulator::handle_slot_results(du_cell_index_t cell_index)
{
  auto& phy_cell = phy.cells[cell_index];

  // Auto-generate UCI indications.
  if (phy_cell.last_ul_res.has_value() and phy_cell.last_ul_res->ul_res != nullptr) {
    const ul_sched_result& ul_res = *phy_cell.last_ul_res->ul_res;
    slot_point             sl_rx  = phy_cell.last_ul_res->slot;

    if (not ul_res.pucchs.empty()) {
      mac_uci_indication_message uci_ind = test_helpers::create_uci_indication(sl_rx, ul_res.pucchs);
      this->du_hi->get_control_info_handler(cell_index).handle_uci(uci_ind);
    }
  }
}
