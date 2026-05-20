/*
 *    Copyright 2023 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TRACE_INSTRUCTION_H
#define TRACE_INSTRUCTION_H

#include <limits>

// special registers that help us identify branches
namespace champsim
{
constexpr char REG_STACK_POINTER = 6;
constexpr char REG_FLAGS = 25;
constexpr char REG_INSTRUCTION_POINTER = 26;
} // namespace champsim

// instruction format
constexpr std::size_t NUM_INSTR_DESTINATIONS_SPARC = 4;
constexpr std::size_t NUM_INSTR_DESTINATIONS = 2;
constexpr std::size_t NUM_INSTR_SOURCES = 4;

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): These classes are deliberately trivial
struct input_instr {
  // instruction pointer or PC (Program Counter)
  unsigned long long ip;

  // branch info
  unsigned char is_branch;
  unsigned char branch_taken;

  // memory object info
  unsigned char is_malloc; // 0: is not object; 1: malloc, 2: calloc, 3: realloc, 4: free, 5: mmap, 6: munmap, 7: mremap

  unsigned char destination_registers[NUM_INSTR_DESTINATIONS]; // output registers
  unsigned char source_registers[NUM_INSTR_SOURCES];           // input registers

  unsigned long long destination_memory[NUM_INSTR_DESTINATIONS]; // output memory; for malloc, this is return value
  unsigned long long source_memory[NUM_INSTR_SOURCES];           // input memory
                                                                 //   for malloc/calloc: [0] = size
                                                                 //   for free: [0] = pointer
                                                                 //   for realloc: [0] = new_size, [1] = old_ptr
                                                                 //   for mmap: [0] = length
                                                                 //   for munmap: [0] = addr, [1] = length
                                                                 //   for mremap: [0] = new_size, [1] = old_addr, [2] = old_size

  unsigned char asid[2];
};

struct cloudsuite_instr {
  // instruction pointer or PC (Program Counter)
  unsigned long long ip;

  // branch info
  unsigned char is_branch;
  unsigned char branch_taken;

  // memory object info
  unsigned char is_malloc; // 0: is not object; 1: malloc, 2: calloc, 3: realloc, 4: free, 5: mmap, 6: munmap, 7: mremap

  unsigned char destination_registers[NUM_INSTR_DESTINATIONS]; // output registers
  unsigned char source_registers[NUM_INSTR_SOURCES];           // input registers

  unsigned long long destination_memory[NUM_INSTR_DESTINATIONS]; // output memory; for malloc, this is return value
  unsigned long long source_memory[NUM_INSTR_SOURCES];           // input memory
                                                                 //   for malloc/calloc: [0] = size
                                                                 //   for free: [0] = pointer
                                                                 //   for realloc: [0] = new_size, [1] = old_ptr
                                                                 //   for mmap: [0] = length
                                                                 //   for munmap: [0] = addr, [1] = length
                                                                 //   for mremap: [0] = new_size, [1] = old_addr, [2] = old_size
                                                                 //   for brk: [0] = new_break_address
                                                                 //   for sbrk: [0] = increment, [1] = old_break, destination[0] = new_break

  unsigned char asid[2];