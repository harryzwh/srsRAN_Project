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

#include "du_processor_repository.h"
#include "du_processor_config.h"
#include "du_processor_factory.h"
#include "srsran/cu_cp/cu_cp_configuration.h"
#include "srsran/support/executors/sync_task_executor.h"
#include <thread>

using namespace srsran;
using namespace srs_cu_cp;

namespace {

/// Notifier used to forward Rx F1AP messages from F1-C to CU-CP.
class f1ap_rx_pdu_notifier final : public f1ap_message_notifier
{
public:
  f1ap_rx_pdu_notifier(cu_cp_f1c_handler& parent_, du_index_t du_index_) :
    parent(&parent_), du_index(du_index_), cached_msg_handler(parent->get_du(du_index).get_message_handler())
  {
  }

  ~f1ap_rx_pdu_notifier() override
  {
    if (parent != nullptr) {
      parent->handle_du_remove_request(du_index);
    }
  }

  void on_new_message(const f1ap_message& msg) override { cached_msg_handler.handle_message(msg); }

private:
  cu_cp_f1c_handler*    parent;
  du_index_t            du_index;
  f1ap_message_handler& cached_msg_handler;
};

} // namespace

du_processor_repository::du_processor_repository(du_repository_config cfg_) : cfg(cfg_), logger(cfg.logger)
{
  f1ap_ev_notifier.connect_du_repository(*this);
}

void du_processor_repository::stop()
{
  if (running.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  while (not du_db.empty()) {
    du_index_t du_idx = du_db.begin()->first;
    remove_du_impl(du_idx);
  }
}

std::unique_ptr<f1ap_message_notifier>
du_processor_repository::handle_new_du_connection(std::unique_ptr<f1ap_message_notifier> f1ap_tx_pdu_notifier)
{
  du_index_t du_index = add_du(std::move(f1ap_tx_pdu_notifier));
  if (du_index == du_index_t::invalid) {
    logger.warning("Rejecting new DU connection. Cause: Failed to create a new DU.");
    return nullptr;
  }

  logger.info("Added TNL connection to DU {}", du_index);
  return std::make_unique<f1ap_rx_pdu_notifier>(*this, du_index);
}

void du_processor_repository::handle_du_remove_request(du_index_t du_index)
{
  if (not running.load(std::memory_order_acquire)) {
    return;
  }
  force_blocking_execute(
      *cfg.cu_cp.cu_cp_executor,
      [this, du_index]() { remove_du_impl(du_index); },
      [&]() {
        logger.warning("Failed to schedule DU removal task. Retrying...");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      });
}

du_index_t du_processor_repository::add_du(std::unique_ptr<f1ap_message_notifier> f1ap_tx_pdu_notifier)
{
  du_index_t du_index = get_next_du_index();
  if (du_index == du_index_t::invalid) {
    logger.warning("DU connection failed - maximum number of DUs connected ({})", cfg.cu_cp.max_nof_dus);
    return du_index_t::invalid;
  }

  // Create DU object
  auto it = du_db.insert(std::make_pair(du_index, du_context{}));
  srsran_assert(it.second, "Unable to insert DU in map");
  du_context& du_ctxt = it.first->second;
  du_ctxt.du_to_cu_cp_notifier.connect_cu_cp(cfg.cu_cp_du_handler, cfg.ue_removal_handler, cfg.ue_context_handler);
  du_ctxt.f1ap_tx_pdu_notifier = std::move(f1ap_tx_pdu_notifier);

  // TODO: use real config
  du_processor_config_t du_cfg       = {};
  du_cfg.du_index                    = du_index;
  du_cfg.rrc_cfg                     = cfg.cu_cp.rrc_config;
  du_cfg.default_security_indication = cfg.cu_cp.default_security_indication;
  du_cfg.du_setup_notif              = &cfg.du_conn_notif;
  du_cfg.f1ap_cfg                    = cfg.cu_cp.f1ap_config;

  std::unique_ptr<du_processor> du = create_du_processor(du_cfg,
                                                         du_ctxt.du_to_cu_cp_notifier,
                                                         f1ap_ev_notifier,
                                                         *du_ctxt.f1ap_tx_pdu_notifier,
                                                         cfg.ue_nas_pdu_notifier,
                                                         cfg.ue_ngap_ctrl_notifier,
                                                         cfg.meas_config_notifier,
                                                         cfg.ue_task_sched,
                                                         cfg.ue_manager,
                                                         *cfg.cu_cp.cu_cp_executor);

  srsran_assert(du != nullptr, "Failed to create DU processor");
  du_ctxt.processor = std::move(du);

  return du_index;
}

du_index_t du_processor_repository::get_next_du_index()
{
  for (unsigned du_idx_int = du_index_to_uint(du_index_t::min); du_idx_int < cfg.cu_cp.max_nof_dus; du_idx_int++) {
    du_index_t du_idx = uint_to_du_index(du_idx_int);
    if (du_db.find(du_idx) == du_db.end()) {
      return du_idx;
    }
  }
  return du_index_t::invalid;
}

void du_processor_repository::remove_du_impl(du_index_t du_index)
{
  srsran_assert(du_index != du_index_t::invalid, "Invalid du_index={}", du_index);
  logger.debug("Removing DU {}...", du_index);

  // Remove DU
  auto du_it = du_db.find(du_index);
  if (du_it == du_db.end()) {
    logger.warning("Remove DU called for inexistent du_index={}", du_index);
    return;
  }

  // Remove DU
  du_db.erase(du_index);
  logger.info("Removed DU {}", du_index);
}

du_processor& du_processor_repository::find_du(du_index_t du_index)
{
  srsran_assert(du_index != du_index_t::invalid, "Invalid du_index={}", du_index);
  srsran_assert(du_db.find(du_index) != du_db.end(), "DU not found du_index={}", du_index);
  return *du_db.at(du_index).processor;
}

du_index_t du_processor_repository::find_du(pci_t pci)
{
  du_index_t index = du_index_t::invalid;
  for (const auto& du : du_db) {
    if (du.second.processor->has_cell(pci))
      return du.first;
  }

  return index;
}

du_processor& du_processor_repository::get_du_processor(du_index_t du_index)
{
  srsran_assert(du_index != du_index_t::invalid, "Invalid du_index={}", du_index);
  srsran_assert(du_db.find(du_index) != du_db.end(), "DU not found du_index={}", du_index);
  return *du_db.at(du_index).processor;
}

du_f1c_handler& du_processor_repository::get_du(du_index_t du_index)
{
  srsran_assert(du_index != du_index_t::invalid, "Invalid du_index={}", du_index);
  srsran_assert(du_db.find(du_index) != du_db.end(), "DU not found du_index={}", du_index);
  return du_db.at(du_index).processor->get_f1ap_interface();
}

void du_processor_repository::handle_paging_message(cu_cp_paging_message& msg)
{
  // Forward paging message to all DU processors
  for (auto& du : du_db) {
    du.second.processor->get_paging_handler().handle_paging_message(msg);
  }
}

ue_index_t du_processor_repository::handle_ue_index_allocation_request(const nr_cell_global_id_t& cgi)
{
  for (auto& du : du_db) {
    if (du.second.processor->has_cell(cgi)) {
      return du.second.processor->get_ngap_interface().allocate_new_ue_index();
    }
  }
  logger.debug("No DU with plmn={} and cell_id={} found.", cgi.plmn, cgi.nci);
  return ue_index_t::invalid;
}

void du_processor_repository::handle_inactivity_notification(du_index_t                           du_index,
                                                             const cu_cp_inactivity_notification& msg)
{
  // Forward message to DU processor
  du_db.at(du_index).processor->get_inactivity_handler().handle_inactivity_notification(msg);
}

std::vector<metrics_report::du_info> du_processor_repository::handle_du_metrics_report_request() const
{
  std::vector<metrics_report::du_info> du_reports;
  for (auto& du : du_db) {
    du_reports.emplace_back(du.second.processor->get_metrics_handler().handle_du_metrics_report_request());
  }
  return du_reports;
}

size_t du_processor_repository::get_nof_f1ap_ues()
{
  size_t nof_ues = 0;
  for (auto& du : du_db) {
    nof_ues += du.second.processor->get_f1ap_interface().get_f1ap_statistics_handler().get_nof_ues();
  }
  return nof_ues;
}
