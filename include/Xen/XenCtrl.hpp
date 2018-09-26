//
// Created by Spencer Michaels on 8/13/18.
//

#ifndef XENDBG_XENCTRL_HPP
#define XENDBG_XENCTRL_HPP

#include <cstring>
#include <fcntl.h>
#include <memory>
#include <sys/ioctl.h>

#include "../../src/Xen/BridgeHeaders/xenctrl.h"
#include "../../src/Xen/BridgeHeaders/xenguest.h"

#include "Common.hpp"
#include "../../src/Xen/MemoryPermissions.hpp"
#include "../../src/Registers/RegistersX86Any.hpp"

namespace xd::xen {

  class Domain;

  class XenCtrl {
  private:
    struct XenVersion {
      int major, minor;
    };

  public:
    XenCtrl();

    XenVersion get_xen_version() const;

    DomInfo get_domain_info(const Domain &domain) const;
    xd::reg::RegistersX86Any get_domain_cpu_context(const Domain &domain,
        VCPU_ID vcpu_id = 0) const;
    void set_domain_cpu_context(const Domain &domain,
        const xd::reg::RegistersX86Any& regs, VCPU_ID vcpu_id = 0) const;
    WordSize get_domain_word_size(const Domain &domain) const;

    MemInfo map_domain_meminfo(const Domain &domain) const;
    MemoryPermissions get_domain_memory_permissions(
        const Domain &domain, Address address) const;

    void set_domain_debugging(const Domain &domain, bool enable,
        VCPU_ID vcpu_id) const;
    void set_domain_single_step(const Domain &domain, bool enable,
        VCPU_ID vcpu_id) const;

    void pause_domain(const Domain &domain) const;
    void unpause_domain(const Domain &domain) const;

    void destroy_domain(const Domain &domain) const;
    void shutdown_domain(const Domain &domain, int reason) const;

  private:
    struct hvm_hw_cpu get_domain_cpu_context_hvm(const Domain &domain, VCPU_ID vcpu_id) const;
    vcpu_guest_context_any_t get_domain_cpu_context_pv(const Domain &domain, VCPU_ID vcpu_id) const;
    void set_domain_cpu_context_hvm(const Domain &domain, struct hvm_hw_cpu context,
        VCPU_ID vcpu_id) const;
    void set_domain_cpu_context_pv(const Domain &domain, vcpu_guest_context_any_t context,
        VCPU_ID vcpu_id) const;

  private:
    std::shared_ptr<xc_interface> _xenctrl;
  };

}

#endif //XENDBG_XENCTRL_HPP