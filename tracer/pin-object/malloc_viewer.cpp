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

/*! @file
 *  This tool reads a Champsim trace file and prints out all malloc-related instructions
 */

#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstdint>

#include "../../inc/trace_instruction.h"

using trace_instr_format_t = input_instr;

// Print usage information
void PrintUsage(const char* program_name)
{
  std::cerr << "Usage: " << program_name << " <trace_file> [options]" << std::endl
            << "Options:" << std::endl
            << "  -h, --help          Show this help message" << std::endl
            << "  -t, --type <type>   Filter by malloc type (1=malloc, 2=calloc, 3=realloc, 4=free, 0=all)" << std::endl
            << "  -c, --count         Only print the count of malloc-related instructions" << std::endl
            << std::endl;
}

// Get malloc type string
std::string GetMallocTypeString(unsigned char is_malloc)
{
  switch (is_malloc) {
    case 0: return "NORMAL";
    case 1: return "MALLOC";
    case 2: return "CALLOC";
    case 3: return "REALLOC";
    case 4: return "FREE";
    default: return "UNKNOWN";
  }
}

// Print a single malloc-related instruction in champsim_tracer format
void PrintMallocInstruction(const trace_instr_format_t& instr)
{
  // Print in champsim_tracer format (matching malloc.trace output)
  if (instr.is_malloc == 1) {
    // malloc
    std::cout << "malloc(" << std::dec << instr.source_memory[0] << ")=0x" 
              << std::hex << std::uppercase << instr.destination_memory[0] 
              << std::nouppercase << std::dec << std::endl;
  } else if (instr.is_malloc == 2) {
    // calloc
    std::cout << "calloc(" << std::dec << instr.source_memory[0] << ")=0x" 
              << std::hex << std::uppercase << instr.destination_memory[0] 
              << std::nouppercase << std::dec << std::endl;
  } else if (instr.is_malloc == 3) {
    // realloc
    std::cout << "realloc(" << std::dec << instr.source_memory[0] << ", 0x" 
              << std::hex << std::uppercase << instr.source_memory[1] 
              << std::nouppercase << std::dec << ")=0x"
              << std::hex << std::uppercase << instr.destination_memory[0] 
              << std::nouppercase << std::dec << std::endl;
  } else if (instr.is_malloc == 4) {
    // free
    std::cout << "free(0x" << std::hex << std::uppercase << instr.source_memory[0] 
              << std::nouppercase << std::dec << ")" << std::endl;
  }
}

int main(int argc, char* argv[])
{
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::string trace_file = argv[1];
  unsigned char filter_type = 0; // 0 means all types
  bool count_only = false;

  // Parse command line arguments
  for (int i = 2; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    } else if (arg == "-t" || arg == "--type") {
      if (i + 1 < argc) {
        try {
          int type_val = std::stoi(argv[++i]);
          if (type_val < 0 || type_val > 4) {
            std::cerr << "Error: Type must be between 0 and 4" << std::endl;
            return 1;
          }
          filter_type = static_cast<unsigned char>(type_val);
        } catch (const std::exception& e) {
          std::cerr << "Error: Invalid type value: " << argv[i] << std::endl;
          return 1;
        }
      } else {
        std::cerr << "Error: --type requires a value" << std::endl;
        return 1;
      }
    } else if (arg == "-c" || arg == "--count") {
      count_only = true;
    }
  }

  // Open trace file
  std::ifstream infile(trace_file, std::ios::binary);
  if (!infile) {
    std::cerr << "Error: Cannot open trace file: " << trace_file << std::endl;
    return 1;
  }

  // Process instructions in a single pass without storing them all in memory
  trace_instr_format_t instr;
  uint64_t total_count = 0;
  uint64_t malloc_count = 0;

  while (infile.read(reinterpret_cast<char*>(&instr), sizeof(trace_instr_format_t))) {
    total_count++;
    
    // Check if this is a malloc-related instruction
    if (instr.is_malloc != 0) {
      // Apply filter if specified
      if (filter_type == 0 || instr.is_malloc == filter_type) {
        malloc_count++;
        
        if (!count_only) {
          // Print the malloc instruction in champsim_tracer format
          PrintMallocInstruction(instr);
        }
      }
    }
  }
  
  infile.close();

  if (total_count == 0) {
    std::cerr << "Error: No instructions found in trace file" << std::endl;
    return 1;
  }

  std::cout << std::endl;

  // Print summary
  std::cout << "========================================" << std::endl;
  std::cout << "Summary:" << std::endl;
  std::cout << "  Total instructions: " << total_count << std::endl;
  std::cout << "  Malloc-related instructions: " << malloc_count << std::endl;
  
  if (filter_type != 0) {
    std::cout << "  Filtered by type: " << GetMallocTypeString(filter_type) << std::endl;
  }
  
  return 0;
}