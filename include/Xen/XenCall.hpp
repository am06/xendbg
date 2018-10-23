//
// Created by Spencer Michaels on 8/17/18.
//

#ifndef XENDBG_XENCALL_HPP
#define XENDBG_XENCALL_HPP

#include <cstring>
#include <cstddef>
#include <functional>
#include <optional>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <type_traits>
#include <utility>

#include <errno.h>

#include "Common.hpp"
#include "XenException.hpp"

#include "BridgeHeaders/domctl.h"
#include "BridgeHeaders/xencall.h"

namespace xd::xen {

  class Domain;

  class XenCall {
  private:
    static xen_domctl _dummy_domctl;

  public:
    using DomctlUnion = decltype(XenCall::_dummy_domctl.u);
    using InitFn = std::function<void(DomctlUnion&)>;
    using CleanupFn = std::function<void()>;

    explicit XenCall(std::shared_ptr<xc_interface> xenctrl);

    DomctlUnion do_domctl(const Domain &domain, uint32_t command, InitFn init = {}, CleanupFn cleanup = {}) const;

  private:
    std::shared_ptr<xc_interface> _xenctrl;
    std::unique_ptr<xencall_handle, decltype(&xencall_close)> _xencall;
  };

}

#endif //XENDBG_XENCALL_HPP
