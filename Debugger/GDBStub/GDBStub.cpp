//
// Created by Spencer Michaels on 9/5/18.
//

#include <iostream>
#include <stdexcept>
#include <sstream>

#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <numeric>
#include <signal.h>
#include <unistd.h>

#include "../Debugger.hpp"
#include "GDBPacketIO.hpp"
#include "GDBStub.hpp"
#include "../../Util/overloaded.hpp"

using reg::x86_32::RegistersX86_32;
using reg::x86_64::RegistersX86_64;
using xd::dbg::gdbstub::GDBPacketIO;
using xd::dbg::gdbstub::GDBStub;
using xd::util::overloaded;

int tcp_socket_open(in_addr_t addr, int port);
int tcp_socket_accept(int sock_fd);
int receive_packet(void *buffer);

GDBStub::GDBStub(int port)
  : _address(INADDR_LOOPBACK), _port(port)
{
}

GDBStub::GDBStub(in_addr_t address, int port)
  : _address(address), _port(port)
{
}

void GDBStub::run(Debugger &dbg) {
  int listen_fd = tcp_socket_open(_address, _port);
  int remote_fd = tcp_socket_accept(listen_fd);

  // TODO: clean this up
  char ack;
  const auto bytes_read = recv(remote_fd, &ack, sizeof(ack), 0);
  if (bytes_read <= 0)
    std::runtime_error("Didn't get an ack from remote!");
  if (ack != '+')
    std::runtime_error("Unexpected value.");
  //send(remote_fd, &ack, sizeof(ack), 0);

  GDBPacketIO io(remote_fd);

  bool running = true;
  while (running) {
    try {
      const auto packet = io.read_packet();
      const auto visitor = util::overloaded {
        [&io](const pkt::StartNoAckModeRequest &req) {
          //io.write_packet(pkt::NotSupportedResponse());

          /* TODO: Turning off acks is recommended to "increase throughput",
           * but in practice makes LLDB send responses much more slowly.
           * Not sure yet if this is an issue with xendbg, or with LLDB.
           *
           * See: https://github.com/llvm-mirror/lldb/blob/master/docs/lldb-gdb-remote.txt#L756
           */
          io.write_packet(pkt::OKResponse());
          io.set_ack_enabled(false);
        },
        [&io](const pkt::QuerySupportedRequest &req) {
          io.write_packet(pkt::QuerySupportedResponse({
            "PacketSize=20000",
            "QStartNoAckMode+",
            "QThreadSuffixSupported+",
            "QListThreadsInStopReplySupported+",
          }));
        },
        [&io](const pkt::QueryThreadSuffixSupportedRequest &req) {
          io.write_packet(pkt::OKResponse());
        },
        [&io](const pkt::QueryListThreadsInStopReplySupportedRequest &req) {
          io.write_packet(pkt::OKResponse());
        },
        [&io, &dbg](const pkt::QueryHostInfoRequest &req) {
          const auto domain = dbg.get_current_domain();
          const auto name = domain->get_name();
          const auto word_size = domain->get_word_size();
          io.write_packet(pkt::QueryHostInfoResponse(word_size, name));
        },
        [&io, &dbg](const pkt::QueryRegisterInfoRequest &req) {
          const auto id = req.get_register_id();
          const auto word_size = dbg.get_current_domain()->get_word_size();

          if (word_size == sizeof(uint64_t)) {
            RegistersX86_64::find_metadata_by_id(id, [&io, id](const auto &md) {
              io.write_packet(pkt::QueryRegisterInfoResponse(
                    md.name, 8*md.width, md.offset, md.gcc_id));
              }, [&io]() {
                io.write_packet(pkt::ErrorResponse(0x45));
              });
          } else if (word_size == sizeof(uint32_t)) {
            RegistersX86_32::find_metadata_by_id(id, [&io, id](const auto &md) {
              io.write_packet(pkt::QueryRegisterInfoResponse(
                    md.name, 8*md.width, md.offset, md.gcc_id));
              }, [&io]() {
                io.write_packet(pkt::ErrorResponse(0x45));
              });
          } else {
            throw std::runtime_error("Unsupported word size!");
          }
        },
        [&io](const pkt::QueryProcessInfoRequest &req) {
          // TODO
          io.write_packet(pkt::QueryProcessInfoResponse(1));
        },
        [&io](const pkt::QueryCurrentThreadIDRequest &req) {
          // TODO
          io.write_packet(pkt::QueryCurrentThreadIDResponse(1));
        },
        [&io](const pkt::QueryThreadInfoStartRequest &req) {
          io.write_packet(pkt::QueryThreadInfoResponse({1}));
        },
        [&io](const pkt::QueryThreadInfoContinuingRequest &req) {
          io.write_packet(pkt::QueryThreadInfoEndResponse());
        },
        [&io](const pkt::StopReasonRequest &req) {
          //io.write_packet(pkt::OKResponse());
          io.write_packet(pkt::StopReasonSignalResponse(0x13, 1)); // TODO
        },
        [&io](const pkt::SetThreadRequest &req) {
          io.write_packet(pkt::OKResponse());
        },
        [&io, &dbg](const pkt::RegisterReadRequest &req) {
          const auto id = req.get_register_id();
          const auto thread_id = req.get_thread_id();
          const auto regs = dbg.get_current_domain()->get_cpu_context(thread_id-1);

          std::visit(util::overloaded {
            [&](const auto &regs) {
              regs.find_by_id(id, [&io](const auto& md, const auto &reg) {
                io.write_packet(pkt::RegisterReadResponse(reg));
              }, [&io]() {
                io.write_packet(pkt::ErrorResponse(0x45)); // TODO
              });
            }
          }, regs);
        },
        [&io, &dbg](const pkt::RegisterWriteRequest &req) {
          const auto id = req.get_register_id();
          const auto value = req.get_value();

          auto regs = dbg.get_current_domain()->get_cpu_context();
          std::visit(util::overloaded {
            [&](auto &regs) {
              regs.find_by_id(id, [&value](const auto&, auto &reg) {
                reg = value;
              }, [&io]() {
                io.write_packet(pkt::ErrorResponse(0x45)); // TODO
              });
            }
          }, regs);
          dbg.get_current_domain()->set_cpu_context(regs);
          io.write_packet(pkt::OKResponse());
        },
        [&io, &dbg](const pkt::GeneralRegistersBatchReadRequest &req) {
          //TODO:regs
          /*
          const auto regs = dbg.get_current_domain()->get_cpu_context();
          std::visit(util::overloaded {
            [&io](const RegistersX86_32 &regs) {
              io.write_packet(pkt::GeneralRegistersBatchReadResponse(regs));
            },
            [&io](const RegistersX86_64 &regs) {
              io.write_packet(pkt::GeneralRegistersBatchReadResponse(regs));
            }
          }, regs);
          */
          io.write_packet(pkt::NotSupportedResponse());
        },
        [&io, &dbg](const pkt::GeneralRegistersBatchWriteRequest &req) {
          //TODO:regs
          /*
          const auto orig_regs = dbg.get_current_domain()->get_cpu_context();
          const auto req_regs = req.get_registers();

          // TODO: set new_regs.x only if req_regs.flags.x == 1
          std::visit(util::overloaded {
            [&](const xen::Registers32 &regs) {
              const auto new_regs = convert_gp_registers_32(
                  std::get<GDBRegisters32>(req_regs).values,
                  std::get<xen::Registers32>(orig_regs));
              dbg.get_current_domain()->set_cpu_context(new_regs);
            },
            [&](const xen::Registers64 &regs) {
              const auto new_regs = convert_gp_registers_64(
                  std::get<GDBRegisters64>(req_regs).values,
                  std::get<xen::Registers64>(orig_regs));
              dbg.get_current_domain()->set_cpu_context(new_regs);
            }
          }, orig_regs);
          */
          io.write_packet(pkt::OKResponse());
        },
        [&io, &dbg](const pkt::MemoryReadRequest &req) {
          const auto address = req.get_address();
          const auto length = req.get_length();
          const auto mem_handle = dbg.get_current_domain()->map_memory(
              address, length, PROT_READ);

          io.write_packet(pkt::MemoryReadResponse(
                (char*)mem_handle.get(), length));
        },
        [&io, &dbg](const pkt::MemoryWriteRequest &req) {
          const auto address = req.get_address();
          const auto length = req.get_length();
          const auto data = req.get_data();

          std::cout << std::hex << "Writing " << length << " bytes to " << address << std::endl;

          const auto mem_handle = dbg.get_current_domain()->map_memory(
              address, length, PROT_WRITE);

          std::cout << "Got mem handle" << std::endl;

          memcpy((void*)data, (void*)mem_handle.get(), length);
          io.write_packet(pkt::OKResponse());
        },
        [&io, &dbg](const pkt::ContinueRequest &req) {
          dbg.continue_until_breakpoint();
          io.write_packet(pkt::OKResponse());
        },
        [&io, &dbg](const pkt::ContinueSignalRequest &req) {
          // TODO
          dbg.continue_until_breakpoint();
          io.write_packet(pkt::OKResponse());
        },
        [&io, &dbg](const pkt::StepRequest &req) {
          dbg.single_step();
          io.write_packet(pkt::OKResponse());
        },
        [&io, &dbg](const pkt::StepSignalRequest &req) {
          // TODO
          dbg.single_step();
          io.write_packet(pkt::OKResponse());
        },
        [&io](const pkt::BreakpointInsertRequest &req) {
          io.write_packet(pkt::NotSupportedResponse());
        },
        [&io](const pkt::BreakpointRemoveRequest &req) {
          io.write_packet(pkt::NotSupportedResponse());
        },
        [&io](const pkt::RestartRequest &req) {
          io.write_packet(pkt::NotSupportedResponse());
        },
        [&io](const pkt::DetachRequest &req) {
          io.write_packet(pkt::OKResponse());
        },
      };

      std::visit(visitor, packet);

    } catch (const UnknownPacketTypeException &e) {
      std::cout << "[!] Unrecognized packet: ";
      std::cout << e.what() << std::endl;
      io.write_packet(pkt::NotSupportedResponse());
    }
  }
}

int GDBStub::tcp_socket_open(in_addr_t addr, int port) {
  int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (sock_fd < 0)
    throw std::runtime_error("Failed to open socket!");

  socklen_t tmp;
  setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&tmp, sizeof(tmp));

  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_family = PF_INET;
  sockaddr.sin_port = htons(port);
  sockaddr.sin_addr.s_addr = htonl(addr);

  if (bind(sock_fd, (struct sockaddr*)&sockaddr, sizeof(sockaddr))) {
    close(sock_fd);
    throw std::runtime_error("Failed to bind socket!");
  }
  if (listen(sock_fd, 1)) {
    close(sock_fd);
    throw std::runtime_error("Failed to listen on socket!");
  }

  return sock_fd;
}


int GDBStub::tcp_socket_accept(int sock_fd) {
  if (sock_fd < 0)
    throw std::runtime_error("Invalid socket FD!");

  struct sockaddr_in sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));

  socklen_t tmp;
  int remote_fd = accept(sock_fd, (struct sockaddr*)&sockaddr, &tmp);
  if (remote_fd < 0) {
    close(sock_fd);
    throw std::runtime_error("Failed to accept!");
  }
  close(sock_fd);

  // Instruct TCP not to delay small packets. This improves interactivity.
  tmp = 1;
  setsockopt(remote_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&tmp, sizeof(tmp));

  // Don't exit automatically when the remote side does
  signal(SIGPIPE, SIG_IGN);

  return remote_fd;
}