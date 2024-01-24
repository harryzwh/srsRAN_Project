/*
 *
 * Copyright 2021-2024 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#pragma once

#include "cell_group_config.h"
#include "srsran/f1ap/du/f1ap_du_ue_context_update.h"

namespace srsran {
namespace srs_du {

/// \brief Outcome report of an DU UE Resource allocation request.
struct du_ue_resource_update_response {
  /// \brief Defines whether the UE release is required due to an error during the update procedure.
  /// If \c procedure_error doesn't contain any error string, then the UE resource update was successful.
  error_type<std::string>        procedure_error = {};
  std::vector<srb_id_t>          failed_srbs;
  std::vector<drb_id_t>          failed_drbs;
  std::vector<serv_cell_index_t> failed_scells;

  bool release_required() const { return procedure_error.is_error(); }
};

/// \brief This class manages the PHY (e.g. RB and symbols used for PUCCH), MAC (e.g. LCIDs) and RLC resources used
/// by an UE. It provides an API to update the UE resources on arrival of new UE Context Update Requests, and
/// returns resources back to the DU RAN Resource Manager when the UE is destroyed.
class ue_ran_resource_configurator
{
public:
  /// \brief Interface used to update the UE Resources on Reconfiguration and return the resources back to the pool,
  /// on UE deletion.
  struct resource_updater {
    virtual ~resource_updater()                                                                  = default;
    virtual du_ue_resource_update_response update(du_cell_index_t                       pcell_index,
                                                  const f1ap_ue_context_update_request& upd_req) = 0;
    virtual const cell_group_config&       get()                                                 = 0;
  };

  explicit ue_ran_resource_configurator(std::unique_ptr<resource_updater> ue_res_, std::string error = {}) :
    ue_res_impl(std::move(ue_res_)),
    cached_res(ue_res_impl != nullptr ? &ue_res_impl->get() : nullptr),
    configurator_error(ue_res_impl != nullptr ? std::string{} : error)
  {
  }

  /// \brief Initiates the update of the resources (PCell, SCells, Bearers) used by the UE.
  ///
  /// \param pcell_index DU Cell Index of the UE's PCell.
  /// \param upd_req UE Context Update Request for a given UE.
  /// \return Outcome of the configuration.
  du_ue_resource_update_response update(du_cell_index_t pcell_index, const f1ap_ue_context_update_request& upd_req)
  {
    return ue_res_impl->update(pcell_index, upd_req);
  }

  /// \brief Checks whether the UE resources have been correctly allocated.
  bool empty() const { return ue_res_impl == nullptr; }

  /// \brief Returns the configurator error, which non-empty string only if the procedure failed.
  std::string get_error() const { return empty() ? configurator_error : std::string{}; }

  const cell_group_config& value() const { return *cached_res; }
  const cell_group_config& operator*() const { return *cached_res; }
  const cell_group_config* operator->() const { return cached_res; }

private:
  std::unique_ptr<resource_updater> ue_res_impl;
  const cell_group_config*          cached_res;
  std::string                       configurator_error;
};

/// \brief This class creates new UE resource configs (PHY, MAC and RLC), using a specific pool of DU resources.
class du_ran_resource_manager
{
public:
  virtual ~du_ran_resource_manager() = default;

  /// \brief Create a new UE resource allocation config object.
  virtual ue_ran_resource_configurator create_ue_resource_configurator(du_ue_index_t   ue_index,
                                                                       du_cell_index_t pcell_index) = 0;
};

} // namespace srs_du
} // namespace srsran
