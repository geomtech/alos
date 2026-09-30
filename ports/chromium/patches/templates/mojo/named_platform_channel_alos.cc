// Copyright 2026 The ALOS Authors
// SPDX-License-Identifier: BSD-3-Clause

#include "mojo/public/cpp/platform/named_platform_channel.h"
#include <errno.h>
#include "base/logging.h"

namespace mojo {

PlatformChannelServerEndpoint NamedPlatformChannel::CreateServerEndpoint(
    const Options&, ServerName* server_name) {
  server_name->clear();
  errno = ENOTSUP;
  PLOG(ERROR) << "ALOS supports unnamed channel pairs, not named Unix sockets";
  return {};
}

PlatformChannelEndpoint NamedPlatformChannel::CreateClientEndpoint(const Options&) {
  errno = ENOTSUP;
  PLOG(ERROR) << "ALOS supports unnamed channel pairs, not named Unix sockets";
  return {};
}

}  // namespace mojo
