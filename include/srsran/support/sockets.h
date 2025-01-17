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

#include "srsran/gateways/sctp_network_gateway.h"
#include "srsran/support/io/unique_fd.h"
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/sctp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace srsran {

/// Modify SCTP default parameters for quicker detection of broken links.
/// Changes to the maximum re-transmission timeout (rto_max).
inline bool sctp_set_rto_opts(const unique_fd&      fd,
                              optional<int>         rto_initial,
                              optional<int>         rto_min,
                              optional<int>         rto_max,
                              srslog::basic_logger& logger)
{
  if (not rto_initial.has_value() && not rto_min.has_value() && not rto_max.has_value()) {
    // no need to set RTO
    return true;
  }

  // Set RTO_MAX to quickly detect broken links.
  sctp_rtoinfo rto_opts  = {};
  socklen_t    rto_sz    = sizeof(sctp_rtoinfo);
  rto_opts.srto_assoc_id = 0;
  if (getsockopt(fd.value(), SOL_SCTP, SCTP_RTOINFO, &rto_opts, &rto_sz) < 0) {
    logger.error("Error getting RTO_INFO sockopts. errono={}", strerror(errno));
    return false; // Responsibility of closing the socket is on the caller
  }

  if (rto_initial.has_value()) {
    rto_opts.srto_initial = rto_initial.value();
  }
  if (rto_min.has_value()) {
    rto_opts.srto_min = rto_min.value();
  }
  if (rto_max.has_value()) {
    rto_opts.srto_max = rto_max.value();
  }

  logger.debug(
      "Setting RTO_INFO options on SCTP socket. Association {}, Initial RTO {}, Minimum RTO {}, Maximum RTO {}",
      rto_opts.srto_assoc_id,
      rto_opts.srto_initial,
      rto_opts.srto_min,
      rto_opts.srto_max);

  if (::setsockopt(fd.value(), SOL_SCTP, SCTP_RTOINFO, &rto_opts, rto_sz) < 0) {
    logger.error("Error setting RTO_INFO sockopts. errno={}", strerror(errno));
    return false; // Responsibility of closing the socket is on the caller
  }
  return true;
}

/// Modify SCTP default parameters for quicker detection of broken links.
/// Changes to the SCTP_INITMSG parameters (to control the timeout of the connect() syscall)
inline bool sctp_set_init_msg_opts(int                   fd,
                                   optional<int>         init_max_attempts,
                                   optional<int>         max_init_timeo,
                                   srslog::basic_logger& logger)
{
  if (not init_max_attempts.has_value() && not max_init_timeo.has_value()) {
    // No value set for init max attempts or max init_timeo,
    // no need to call set_sockopts()
    return true;
  }

  // Set SCTP INITMSG options to reduce blocking timeout of connect()
  sctp_initmsg init_opts = {};
  socklen_t    init_sz   = sizeof(sctp_initmsg);
  if (getsockopt(fd, SOL_SCTP, SCTP_INITMSG, &init_opts, &init_sz) < 0) {
    logger.error("Error getting sockopts. errno={}", strerror(errno));
    return false; // Responsibility of closing the socket is on the caller
  }

  if (init_max_attempts.has_value()) {
    init_opts.sinit_max_attempts = init_max_attempts.value();
  }
  if (max_init_timeo.has_value()) {
    init_opts.sinit_max_init_timeo = max_init_timeo.value();
  }

  logger.debug("Setting SCTP_INITMSG options on SCTP socket. Max attempts {}, Max init attempts timeout {}",
               init_opts.sinit_max_attempts,
               init_opts.sinit_max_init_timeo);
  if (::setsockopt(fd, SOL_SCTP, SCTP_INITMSG, &init_opts, init_sz) < 0) {
    logger.error("Error setting SCTP_INITMSG sockopts. errno={}\n", strerror(errno));
    return false; // Responsibility of closing the socket is on the caller
  }
  return true;
}

/// Set or unset SCTP_NODELAY. With NODELAY enabled, SCTP messages are sent as soon as possible with no unnecessary
/// delay, at the cost of transmitting more packets over the network. Otherwise their transmission might be delayed and
/// concatenated with subsequent messages in order to transmit them in one big PDU.
///
/// Note: If the local interface supports jumbo frames (MTU size > 1500) but not the receiver, then the receiver might
/// discard big PDUs and the stream might get stuck.
inline bool sctp_set_nodelay(int fd, optional<bool> nodelay, srslog::basic_logger& logger)
{
  if (not nodelay.has_value()) {
    // no need to change anything
    return true;
  }

  int optval = nodelay.value() == true ? 1 : 0;
  if (::setsockopt(fd, IPPROTO_SCTP, SCTP_NODELAY, &optval, sizeof(optval)) != 0) {
    logger.error("Could not set SCTP_NODELAY. optval={} error={}", optval, strerror(errno));
    return false;
  }
  return true;
}

inline bool bind_to_interface(const unique_fd& fd, std::string& interface, srslog::basic_logger& logger)
{
  if (interface.empty() || interface == "auto") {
    // no need to change anything
    return true;
  }

  ifreq ifr;
  std::strncpy(ifr.ifr_ifrn.ifrn_name, interface.c_str(), IFNAMSIZ);
  ifr.ifr_ifrn.ifrn_name[IFNAMSIZ - 1] = 0; // ensure null termination in case input exceeds maximum length

  if (setsockopt(fd.value(), SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) < 0) {
    logger.error("Could not bind socket to interface. interface={} error={}", ifr.ifr_ifrn.ifrn_name, strerror(errno));
    return false;
  }
  return true;
}

inline bool sockaddr_to_ip_str(const sockaddr* addr, std::string& ip_address, srslog::basic_logger& logger)
{
  char addr_str[INET6_ADDRSTRLEN] = {};
  if (addr->sa_family == AF_INET) {
    if (inet_ntop(AF_INET, &((sockaddr_in*)addr)->sin_addr, addr_str, INET6_ADDRSTRLEN) == nullptr) {
      logger.error("Could not convert sockaddr_in to string. errno={}", strerror(errno));
      return false;
    }
  } else if (addr->sa_family == AF_INET6) {
    if (inet_ntop(AF_INET6, &((sockaddr_in6*)addr)->sin6_addr, addr_str, INET6_ADDRSTRLEN) == nullptr) {
      logger.error("Could not convert sockaddr_in6 to string. errno={}", strerror(errno));
      return false;
    }
  } else {
    logger.error("Unhandled address family.");
    return false;
  }

  ip_address = addr_str;
  logger.debug("Read bind port of UDP network gateway: {}", ip_address);
  return true;
}

inline std::string sock_type_to_str(int type)
{
  switch (type) {
    case SOCK_STREAM:
      return "SOCK_STREAM";
    case SOCK_DGRAM:
      return "SOCK_DGRAM";
    case SOCK_RAW:
      return "SOCK_RAW";
    case SOCK_RDM:
      return "SOCK_RDM";
    case SOCK_SEQPACKET:
      return "SOCK_SEQPACKET";
    case SOCK_DCCP:
      return "SOCK_DCCP";
    case SOCK_PACKET:
      return "SOCK_PACKET";
  }
  return "unknown type";
}
} // namespace srsran
