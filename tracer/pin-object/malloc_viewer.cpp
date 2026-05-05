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
            << "  -n, --no-context    Do not print context instructions (before/after)" << std::endl
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

// Print context instruction (before/after malloc instruction)
void PrintContextInstruction(const trace_instr_format_t& instr, uint64_t instruction_number, const std::string& context_label)
{
  std::cout << "  [" << context_label << "] Instruction #" << instruction_number << std::endl;
  std::cout << "    IP: 0x" << std::hex << std::uppercase << instr.ip << std::nouppercase << std::dec << std::endl;
  std::cout << "    is_branch: " << (instr.is_branch ? "YES" : "NO");
  if (instr.is_branch) {
    std::cout << ", branch_taken: " << (instr.branch_taken ? "TAKEN" : "NOT TAKEN");
  }
  std::cout << std::endl;
}

// Print a single malloc-related instruction with context
void PrintMallocInstruction(const trace_instr_format_t& instr, uint64_t instruction_number,
                           const std::vector<trace_instr_format_t>& prev_instructions,
                           const std::vector<trace_instr_format_t>& next_instructions,
                           bool show_context = true)
{
  std::cout << "========================================" << std::endl;
  std::cout << "Instruction #" << instruction_number << std::endl;
  std::cout << "  Type: " << GetMallocTypeString(instr.is_malloc) << std::endl;
  std::cout << "  IP: 0x" << std::hex << std::uppercase << instr.ip << std::nouppercase << std::dec << std::endl;
  
  if (instr.is_malloc == 1 || instr.is_malloc == 2) {
    // malloc or calloc
    std::cout << "  Size: " << instr.source_memory[0] << " bytes" << std::endl;
    std::cout << "  Return Address: 0x" << std::hex << std::uppercase << instr.destination_memory[0] << std::nouppercase << std::dec << std::endl;
  } else if (instr.is_malloc == 3) {
    // realloc
    std::cout << "  New Size: " << instr.source_memory[0] << " bytes" << std::endl;
    std::cout << "  Old Pointer: 0x" << std::hex << std::uppercase << instr.source_memory[1] << std::nouppercase << std::dec << std::endl;
    std::cout << "  Return Address: 0x" << std::hex << std::uppercase << instr.destination_memory[0] << std::nouppercase << std::dec << std::endl;
  } else if (instr.is_malloc == 4) {
    // free
    std::cout << "  Pointer: 0x" << std::hex << std::uppercase << instr.source_memory[0] << std::nouppercase << std::dec << std::endl;
  }
  
  // Print previous 2 instructions
  if (show_context && !prev_instructions.empty()) {
    std::cout << std::endl;
    std::cout << "  --- Previous Instructions ---" << std::endl;
    for (size_t i = 0; i < prev_instructions.size(); i++) {
      size_t idx = prev_instructions.size() - 1 - i; // Print in reverse order (closest first)
      uint64_t prev_num = instruction_number - (i + 1);
      PrintContextInstruction(prev_instructions[idx], prev_num, "BEFORE-" + std::to_string(i + 1));
    }
  }
  
  // Print next 2 instructions
  if (show_context && !next_instructions.empty()) {
    std::cout << std::endl;
    std::cout << "  --- Next Instructions ---" << std::endl;
    for (size_t i = 0; i < next_instructions.size(); i++) {
      uint64_t next_num = instruction_number + (i + 1);
      PrintContextInstruction(next_instructions[i], next_num, "AFTER-" + std::to_string(i + 1));
    }
  }
  
  std::cout << std::endl;
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
  bool show_context = true;

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
    } else if (arg == "-n" || arg == "--no-context") {
      show_context = false;
    }
  }

  // Open trace file
  std::ifstream infile(trace_file, std::ios::binary);
  if (!infile) {
    std::cerr << "Error: Cannot open trace file: " << trace_file << std::endl;
    return 1;
  }

  // First pass: read all instructions into memory
  std::vector<trace_instr_format_t> all_instructions;
  
  // Get file size for progress indication and validation
  infile.seekg(0, std::ios::end);
  std::streamsize file_size = infile.tellg();
  infile.seekg(0, std::ios::beg);
  
  if (file_size <= 0) {
    std::cerr << "Error: Trace file is empty or invalid" << std::endl;
    return 1;
  }
  
  // Estimate number of instructions and reserve memory
  size_t estimated_count = static_cast<size_t>(file_size / sizeof(trace_instr_format_t));
  all_instructions.reserve(estimated_count);
  
  std::cout << "Reading trace file (" << file_size << " bytes, ~" << estimated_count << " instructions)..." << std::endl;
  
  trace_instr_format_t instr;
  size_t read_count = 0;
  while (infile.read(reinterpret_cast<char*>(&instr), sizeof(trace_instr_format_t))) {
    all_instructions.push_back(instr);
    read_count++;
    
    // Progress indicator for large files (every 10M instructions)
    if (read_count % 10000000 == 0) {
      std::cout << "  Read " << read_count << " instructions..." << std::endl;
    }
  }
  
  // Check if we reached EOF cleanly
  if (!infile.eof()) {
    std::cerr << "Warning: File may be truncated or corrupted" << std::endl;
  }
  
  infile.close();

  std::cout << "Total instructions in trace: " << all_instructions.size() << std::endl;
  
  if (all_instructions.empty()) {
    std::cerr << "Error: No instructions found in trace file" << std::endl;
    return 1;
  }
  
  std::cout << "Processing malloc-related instructions..." << std::endl;
  std::cout << std::endl;

  // Second pass: find and print malloc-related instructions with context
  uint64_t malloc_count = 0;
  
  // Pre-allocate context vectors to avoid repeated allocations
  std::vector<trace_instr_format_t> prev_instructions;
  std::vector<trace_instr_format_t> next_instructions;
  prev_instructions.reserve(2);
  next_instructions.reserve(2);

  for (size_t i = 0; i < all_instructions.size(); i++) {
    const auto& current_instr = all_instructions[i];
    
    // Check if this is a malloc-related instruction
    if (current_instr.is_malloc != 0) {
      // Apply filter if specified
      if (filter_type == 0 || current_instr.is_malloc == filter_type) {
        malloc_count++;
        
        if (!count_only) {
          // Clear and collect previous 2 instructions
          prev_instructions.clear();
          for (int j = 1; j <= 2; j++) {
            if (static_cast<int>(i) - j >= 0) {
              prev_instructions.push_back(all_instructions[i - j]);
            }
          }
          
          // Clear and collect next 2 instructions
          next_instructions.clear();
          for (size_t j = 1; j <= 2; j++) {
            if (i + j < all_instructions.size()) {
              next_instructions.push_back(all_instructions[i + j]);
            }
          }
          
          // Print the malloc instruction with context
          PrintMallocInstruction(current_instr, i + 1, prev_instructions, next_instructions, show_context);
        }
      }
    }
  }

  // Print summary
  std::cout << "========================================" << std::endl;
  std::cout << "Summary:" << std::endl;
  std::cout << "  Total instructions: " << all_instructions.size() << std::endl;
  std::cout << "  Malloc-related instructions: " << malloc_count << std::endl;
  
  if (filter_type != 0) {
    std::cout << "  Filtered by type: " << GetMallocTypeString(filter_type) << std::endl;
  }
  std::cout << "========================================" << std::endl;

  return 0;
}
