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

#pragma once

#include "../ue_manager/ue_manager_impl.h"
#include "srsran/cu_cp/cu_cp_types.h"
#include "srsran/support/async/fifo_async_task_scheduler.h"
#include <unordered_map>

namespace srsran {
namespace srs_cu_cp {

/// \brief Service provided by CU-CP to handle routines.
class cu_cp_routine_manager
{
public:
  explicit cu_cp_routine_manager(ue_manager&                  ue_mng_,
                                 const security_indication_t& default_security_indication_,
                                 srslog::basic_logger&        logger_);
  ~cu_cp_routine_manager() = default;

  bool schedule_async_task(async_task<void> task);

  async_task<cu_cp_pdu_session_resource_setup_response>
  start_pdu_session_resource_setup_routine(const cu_cp_pdu_session_resource_setup_request& setup_msg,
                                           const srsran::security::sec_as_config&          security_cfg,
                                           e1ap_bearer_context_manager&                    e1ap_bearer_ctxt_mng,
                                           f1ap_ue_context_manager&                        f1ap_ue_ctxt_mng,
                                           du_processor_rrc_ue_control_message_notifier&   rrc_ue_ctrl_notifier,
                                           up_resource_manager&                            rrc_ue_up_resource_manager);

  async_task<cu_cp_pdu_session_resource_release_response>
  start_pdu_session_resource_release_routine(const cu_cp_pdu_session_resource_release_command& release_cmd,
                                             e1ap_bearer_context_manager&                      e1ap_bearer_ctxt_mng,
                                             f1ap_ue_context_manager&                          f1ap_ue_ctxt_mng,
                                             ngap_control_message_handler&                     ngap_handler,
                                             du_processor_rrc_ue_control_message_notifier&     rrc_ue_ctrl_notifier,
                                             du_processor_ue_task_scheduler&                   task_sched,
                                             up_resource_manager& rrc_ue_up_resource_manager);

  async_task<cu_cp_pdu_session_resource_modify_response>
  start_pdu_session_resource_modification_routine(const cu_cp_pdu_session_resource_modify_request& modify_msg,
                                                  e1ap_bearer_context_manager&                     e1ap_bearer_ctxt_mng,
                                                  f1ap_ue_context_manager&                         f1ap_ue_ctxt_mng,
                                                  du_processor_rrc_ue_control_message_notifier&    rrc_ue_ctrl_notifier,
                                                  up_resource_manager& rrc_ue_up_resource_manager);

  async_task<cu_cp_ue_context_release_complete>
  start_ue_context_release_routine(const cu_cp_ue_context_release_command& command,
                                   e1ap_bearer_context_manager*            e1ap_bearer_ctxt_mng,
                                   f1ap_ue_context_manager&                f1ap_ue_ctxt_mng,
                                   cu_cp_ue_removal_handler&               ue_removal_handler);

  async_task<bool>
  start_reestablishment_context_modification_routine(ue_index_t                                    ue_index,
                                                     e1ap_bearer_context_manager&                  e1ap_bearer_ctxt_mng,
                                                     f1ap_ue_context_manager&                      f1ap_ue_ctxt_mng,
                                                     du_processor_rrc_ue_control_message_notifier& rrc_ue_ctrl_notifier,
                                                     up_resource_manager& ue_up_resource_manager);

  async_task<cu_cp_inter_du_handover_response>
  start_inter_du_handover_routine(const cu_cp_inter_du_handover_request& request,
                                  const byte_buffer&                     target_cell_sib1,
                                  e1ap_bearer_context_manager&           e1ap_bearer_ctxt_mng,
                                  f1ap_ue_context_manager&               source_f1ap_ue_ctxt_mng,
                                  f1ap_ue_context_manager&               target_f1ap_ue_ctxt_mng,
                                  cu_cp_ue_context_release_handler&      ue_context_release_handler,
                                  cu_cp_ue_removal_handler&              ue_removal_handler,
                                  cu_cp_ue_context_manipulation_handler& cu_cp_handler);

  async_task<ngap_handover_resource_allocation_response>
  start_inter_cu_handover_target_routine(const ngap_handover_request& request_,
                                         e1ap_bearer_context_manager& e1ap_bearer_ctxt_mng,
                                         f1ap_ue_context_manager&     f1ap_ue_ctxt_mng,
                                         cu_cp_ue_removal_handler&    ue_removal_handler);

private:
  ue_manager&                  ue_mng;
  const security_indication_t& default_security_indication;
  srslog::basic_logger&        logger;

  // cu-cp task event loop
  fifo_async_task_scheduler main_ctrl_loop;
};

} // namespace srs_cu_cp
} // namespace srsran
