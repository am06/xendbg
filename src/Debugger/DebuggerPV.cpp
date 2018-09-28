//
// Created by Spencer Michaels on 8/28/18.
//

#include <iostream>
#include <stdexcept>
#include <sys/mman.h>

#include <capstone/capstone.h>

#include <Debugger/DebuggerPV.hpp>
#include <Util/overloaded.hpp>

#define X86_INFINITE_LOOP 0xFEEB

using xd::dbg::DebuggerPV;
using xd::dbg::NoSuchSymbolException;
using xd::reg::x86_32::RegistersX86_32;
using xd::reg::x86_64::RegistersX86_64;
using xd::xen::Address;
using xd::xen::DomainPV;
using xd::xen::DomID;

DebuggerPV::DebuggerPV(uvw::Loop &loop, DomainPV &domain)
  : Debugger(loop, domain), _domain(domain)
{
}

void DebuggerPV::continue_() {
  // Single step first to move beyond the current breakpoint;
  // it will be removed during the step and replaced automatically.
  if (check_breakpoint_hit())
    single_step();

  _domain.unpause();
}

Address DebuggerPV::single_step() {
  _domain.pause();
  // If there's already a breakpoint here, remove it temporarily so we can continue
  std::optional<Address> orig_addr;
  if ((orig_addr = check_breakpoint_hit()))
    remove_breakpoint(*orig_addr);

  // For conditional branches, we need to insert EBFEs at both potential locations.
  const auto [dest1_addr, dest2_addr] = get_address_of_next_instruction();

  bool dest1_had_il = dest1_addr && (_infinite_loops.count(*dest1_addr) != 0);
  bool dest2_had_il = dest2_addr && (_infinite_loops.count(*dest2_addr) != 0);

  if (dest1_addr && !dest1_had_il)
    insert_breakpoint(*dest1_addr);
  if (dest2_addr && !dest2_had_il)
    insert_breakpoint(*dest2_addr);

  _domain.unpause();
  std::optional<Address> address_opt;
  while (!(address_opt = check_breakpoint_hit()));
  _domain.pause();

  // Remove each of our two infinite loops unless there is a
  // *manually-inserted* breakpoint at the corresponding address.
  if (dest1_addr && !dest1_had_il)
    remove_breakpoint(*dest1_addr);
  if (dest2_addr && !dest2_had_il)
    remove_breakpoint(*dest2_addr);

  // If there was a BP at the instruction we started at, put it back
  if (orig_addr)
    insert_breakpoint(*orig_addr);

  return *address_opt;
}

std::vector<Address> DebuggerPV::get_breakpoints() {
  std::vector<Address> addresses;
  std::transform(_infinite_loops.begin(), _infinite_loops.end(),
      std::back_inserter(addresses),
      [](const auto &il) {
        return il.first;
      });
  return addresses;
}

void DebuggerPV::insert_breakpoint(Address address) {
  if (_infinite_loops.count(address)) {
    std::cout << "[!]: Tried to insert infinite loop where one already exists." << std::endl;
    return; // TODO?
  }
  //std::cout << "Inserting infinite loop at " << std::hex << address << std::endl;

  const auto mem_handle = _domain.map_memory<char>(address, 2, PROT_READ | PROT_WRITE);
  const auto mem = (uint16_t*)mem_handle.get();

  const auto orig_bytes = *mem;

  _infinite_loops[address] = orig_bytes;
  *mem = X86_INFINITE_LOOP;
}

void DebuggerPV::remove_breakpoint(Address address) {
  if (!_infinite_loops.count(address)) {
    std::cout << "[!]: Tried to remove infinite loop where one does not exist." << std::endl;
    return; // TODO?
  }
  //std::cout << "Removing infinite loop at " << std::hex << address << std::endl;

  const auto mem_handle = _domain.map_memory<char>(address, 2, PROT_WRITE);
  const auto mem = (uint16_t*)mem_handle.get();

  const auto orig_bytes = _infinite_loops.at(address);
  *mem = orig_bytes;

  _infinite_loops.erase(_infinite_loops.find(address));
}

xd::dbg::Debugger::MaskedMemory
DebuggerPV::read_memory_masking_breakpoints(Address address, size_t length) {
  const auto mem_handle = _domain.map_memory<char>(
      address, length, PROT_READ);

  const auto mem_masked = (unsigned char*)malloc(length);
  memcpy(mem_masked, mem_handle.get(), length);

  const auto address_end = address + length;
  for (const auto [il_address, il_orig_bytes] : _infinite_loops) {
    if (il_address >= address && il_address < address_end) {
      const auto dist = il_address - address;
      *((uint16_t*)(mem_masked + dist)) = il_orig_bytes;
    }
  }

  return MaskedMemory(mem_masked);
}

void DebuggerPV::write_memory_retaining_breakpoints(Address address, size_t length, void *data) {
  const auto half_overlap_start_address = address-1;
  const auto half_overlap_end_address = address+length-1;

  const auto length_orig = length;
  if (_infinite_loops.count(half_overlap_start_address)) {
    address -= 1;
    length += 1;
  }
  if (_infinite_loops.count(half_overlap_end_address))
    length += 1;

  std::vector<Address> il_addresses;
  const auto address_end = address + length_orig;
  for (const auto [il_address, _] : _infinite_loops) {
    if (il_address >= address && il_address < address_end) {
      remove_breakpoint(il_address);
      il_addresses.push_back(il_address);
    }
  }

  const auto mem_handle = _domain.map_memory<char>(address, length, PROT_WRITE);
  const auto mem_orig = (char*)mem_handle.get() + (length - length_orig);
  memcpy((void*)mem_orig, data, length_orig);

  std::cout << std::hex << "wrote " << length_orig << " bytes to " << address << std::endl;

  for (const auto &il_address : il_addresses)
    insert_breakpoint(il_address);
}

std::optional<Address> DebuggerPV::check_breakpoint_hit() {
  const auto address = reg::read_register<reg::x86_32::eip, reg::x86_64::rip>(_domain.get_cpu_context());
  const auto mem_handle = _domain.map_memory<char>(address, 2, PROT_READ);
  const auto mem = (uint16_t*)mem_handle.get();

  if (*mem == X86_INFINITE_LOOP && _infinite_loops.count(address))
    return address;
  return std::nullopt;
}
