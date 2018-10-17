#include <csignal>
#include <iostream>

#include <spdlog/spdlog.h>

#include <Globals.hpp>

#include "DebugSession.hpp"
#include "ServerModeController.hpp"

using xd::ServerModeController;
using xd::DebugSession;
using xd::xen::get_domains;

ServerModeController::ServerModeController(uint16_t base_port)
  : _loop(uvw::Loop::getDefault()),
    _signal(_loop->resource<uvw::SignalHandle>()),
    _poll(_loop->resource<uvw::PollHandle>(_xenstore.get_fileno())),
    _next_port(base_port)
{
}

void ServerModeController::run_single(const std::string &name) {
  const auto domains = get_domains(_privcmd, _xenevtchn, _xenctrl, _xenforeignmemory, _xenstore);

  auto found = std::find_if(domains.begin(), domains.end(), [&](const auto &domain) {
    return domain->get_name() == name;
  });

  if (found == domains.end())
    throw std::runtime_error("No such domain!");

  run_single((*found)->get_domid());
}

void ServerModeController::run_single(xen::DomID domid) {
  auto &watch_release = _xenstore.add_watch();
  watch_release.add_path("@releaseDomain");

  _poll->on<uvw::PollEvent>([&](const auto &event, auto &handle) {
    if (watch_release.check())
      if (prune_instances())
        handle.loop().stop();
  });

  _poll->start(uvw::PollHandle::Event::READABLE);

  auto domain = xen::init_domain(domid, _privcmd, _xenevtchn, _xenctrl,
      _xenforeignmemory, _xenstore);

  add_instance(std::move(domain));

  run();
}

void ServerModeController::run_multi() {
  auto &watch_introduce = _xenstore.add_watch();
  watch_introduce.add_path("@introduceDomain");

  auto &watch_release = _xenstore.add_watch();
  watch_release.add_path("@releaseDomain");

  _poll->on<uvw::PollEvent>([&](const auto&, auto&) {
    if (watch_introduce.check())
      add_new_instances();
    else if (watch_release.check())
      prune_instances();
  });

  _poll->start(uvw::PollHandle::Event::READABLE);

  run();
}

void ServerModeController::run() {
  _signal->once<uvw::SignalEvent>([this](const auto &event, auto &handle) {
    handle.loop().walk([](auto &handle) {
      handle.close();
    });
    handle.loop().run();
    _instances.clear();
  });
  
  _signal->start(SIGINT);

  _loop->run();
}

size_t ServerModeController::add_new_instances() {
  const auto domains = get_domains(_privcmd, _xenevtchn, _xenctrl, _xenforeignmemory, _xenstore);

  size_t num_added = 0;
  for (const auto &domain : domains) {
    const auto domid = domain->get_domid();
    if (!_instances.count(domid)) {
      add_instance(domain);
      ++num_added;
    }
  }

  return num_added;
}

size_t ServerModeController::prune_instances() {
  const auto domains = get_domains(_privcmd, _xenevtchn, _xenctrl, _xenforeignmemory, _xenstore);

  size_t num_removed = 0;
  auto it = _instances.begin();
  while (it != _instances.end()) {
    if (std::none_of(domains.begin(), domains.end(),
      [&](const auto &domain) {
        return domain->get_domid() == it->first;
      }))
    {
      spdlog::get(LOGNAME_CONSOLE)->info(
          "DOWN: Domain {0:d}", it->first);
      it = _instances.erase(it);
      ++num_removed;
    } else {
      ++it;
    }
  }

  return num_removed;
}

void ServerModeController::add_instance(std::shared_ptr<xen::Domain> domain) {
  const auto domid = domain->get_domid();

  if (_instances.count(domid))
    throw DomainAlreadyAddedException(domid);

  spdlog::get(LOGNAME_CONSOLE)->info(
      "UP: Domain {0:d} @ port {1:d}", domid, _next_port);

  auto [kv, _] = _instances.emplace(domid, std::make_unique<DebugSession>(*_loop, std::move(domain)));
  kv->second->run("127.0.0.1", _next_port++);
}
