/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#pragma once

#include "cell/cell_configuration.h"
#include "cell/resource_grid.h"
#include "common_scheduling/csi_rs_scheduler.h"
#include "common_scheduling/paging_scheduler.h"
#include "common_scheduling/prach_scheduler.h"
#include "common_scheduling/ra_scheduler.h"
#include "common_scheduling/si_message_scheduler.h"
#include "common_scheduling/sib_scheduler.h"
#include "common_scheduling/ssb_scheduler.h"
#include "logging/scheduler_result_logger.h"
#include "pdcch_scheduling/pdcch_resource_allocator_impl.h"
#include "pucch_scheduling/pucch_allocator_impl.h"
#include "pucch_scheduling/pucch_guardbands_scheduler.h"
#include "uci_scheduling/uci_allocator_impl.h"
#include "ue_scheduling/ue_scheduler.h"
#include "srsran/scheduler/config/scheduler_config.h"

namespace srsran {

class scheduler_metrics_handler;

/// \brief This class holds all the resources that are specific to a cell.
/// This includes the SIB and RA scheduler objects, PDCCH scheduler object, the cell resource grid, etc.
class cell_scheduler
{
public:
  explicit cell_scheduler(const scheduler_expert_config&                  sched_cfg,
                          const sched_cell_configuration_request_message& msg,
                          ue_scheduler&                                   ue_sched,
                          scheduler_event_logger&                         ev_logger,
                          scheduler_metrics_handler&                      metrics);

  void run_slot(slot_point sl_tx);

  const sched_result& last_result() const { return res_grid[0].result; }

  void handle_rach_indication(const rach_indication_message& msg) { ra_sch.handle_rach_indication(msg); }

  void handle_crc_indication(const ul_crc_indication& crc_ind);

  void handle_paging_information(const sched_paging_information& pi) { pg_sch.handle_paging_information(pi); }

  const cell_configuration cell_cfg;

  /// Reference to UE scheduler whose DU cell group contains this cell.
  ue_scheduler& ue_sched;

private:
  /// Resource grid of this cell.
  cell_resource_allocator res_grid;

  /// Logger of cell events and scheduling results.
  scheduler_event_logger&    event_logger;
  scheduler_metrics_handler& metrics;
  scheduler_result_logger    result_logger;
  srslog::basic_logger&      logger;

  ssb_scheduler                 ssb_sch;
  pdcch_resource_allocator_impl pdcch_sch;
  csi_rs_scheduler              csi_sch;
  ra_scheduler                  ra_sch;
  prach_scheduler               prach_sch;
  pucch_allocator_impl          pucch_alloc;
  uci_allocator_impl            uci_alloc;
  sib1_scheduler                sib1_sch;
  si_message_scheduler          si_msg_sch;
  pucch_guardbands_scheduler    pucch_guard_sch;
  paging_scheduler              pg_sch;
};

} // namespace srsran
