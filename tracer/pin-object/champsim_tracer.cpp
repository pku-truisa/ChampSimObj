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
 *  This is an example of the PIN tool that demonstrates some basic PIN APIs
 *  and could serve as the starting point for developing your first PIN tool
 */

#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <unordered_set>

#include "../../inc/trace_instruction.h"
#include "pin.H"

using trace_instr_format_t = input_instr;

/* ================================================================== */
// Global variables
/* ================================================================== */

UINT64 instrCount = 0;

std::ofstream outfile;
std::ofstream malloc_outfile;

std::vector<trace_instr_format_t> instr_buffer;
const size_t INSTR_BUFFER_SIZE = 16 * 1024 * 1024; // 16MB buffer

trace_instr_format_t curr_instr;
trace_instr_format_t event_instr;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<std::string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "champsim.trace", "specify file name for Champsim tracer output");

KNOB<std::string> KnobMallocOutputFile(KNOB_MODE_WRITEONCE, "pintool", "m", "malloc.trace", "specify file name for malloc trace output");

KNOB<UINT64> KnobSkipInstructions(KNOB_MODE_WRITEONCE, "pintool", "s", "0", "How many instructions to skip before tracing begins");

KNOB<UINT64> KnobTraceInstructions(KNOB_MODE_WRITEONCE, "pintool", "t", "0", "How many instructions to trace (0 for unlimited)");

KNOB<UINT64> KnobMallocSizeThreshold(KNOB_MODE_WRITEONCE, "pintool", "k", "0", "Minimum malloc size to trace (0 to trace all)");

/* ===================================================================== */
// Malloc tracking state
/* ===================================================================== */
std::unordered_set<ADDRINT> tracked_malloc_addresses; // Set of malloc addresses that were traced
std::vector<trace_instr_format_t> pending_malloc_events; // Stack to track pending malloc/calloc/realloc before-after pairs

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
  std::cerr << "This tool creates a register and memory access trace" << std::endl
            << "Specify the output trace file with -o" << std::endl
            << "Specify the malloc output trace file with -m" << std::endl
            << "Specify the number of instructions to skip before tracing with -s" << std::endl
            << "Specify the number of instructions to trace with -t" << std::endl
            << "Specify minimum malloc size to trace with -h (0 to trace all mallocs)" << std::endl
            << std::endl;

  std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;

  return -1;
}

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

void ResetCurrentInstruction(VOID* ip)
{
  curr_instr = {};
  curr_instr.ip = (unsigned long long int)ip;
}

BOOL ShouldWrite()
{
  ++instrCount;
  if (KnobTraceInstructions.Value() == 0) {
    return instrCount > KnobSkipInstructions.Value();
  }
  return (instrCount > KnobSkipInstructions.Value()) && (instrCount <= (KnobTraceInstructions.Value() + KnobSkipInstructions.Value()));
}

void WriteCurrentInstruction()
{
  instr_buffer.push_back(curr_instr);

  if (instr_buffer.size() * sizeof(trace_instr_format_t) >= INSTR_BUFFER_SIZE) {
    outfile.write(reinterpret_cast<const char*>(instr_buffer.data()), 
                 instr_buffer.size() * sizeof(trace_instr_format_t));
    instr_buffer.clear();
  }
}

void WriteEventInstruction()
{
  instr_buffer.push_back(event_instr);

  if (instr_buffer.size() * sizeof(trace_instr_format_t) >= INSTR_BUFFER_SIZE) {
    outfile.write(reinterpret_cast<const char*>(instr_buffer.data()), 
                 instr_buffer.size() * sizeof(trace_instr_format_t));
    instr_buffer.clear();
  }
}

void BranchOrNot(UINT32 taken)
{
  curr_instr.is_branch = 1;
  curr_instr.branch_taken = taken;
}

template <typename T>
void WriteToSet(T* begin, T* end, UINT32 r)
{
  auto set_end = std::find(begin, end, 0);
  auto found_reg = std::find(begin, set_end, r); // check to see if this register is already in the list
  *found_reg = r;
}

// Malloc tracking functions
VOID MallocBefore(ADDRINT size, ADDRINT ip)
{
  // Check if size meets threshold
  if (size < KnobMallocSizeThreshold.Value()) {
    return; // Skip small mallocs
  }
  
  malloc_outfile << "MALLOC_SIZE " << size << std::endl;
  
  trace_instr_format_t instr = {};
  instr.ip = (unsigned long long int)ip;
  instr.is_malloc = 1; // 1: malloc
  instr.source_memory[0] = size;
  pending_malloc_events.push_back(instr);
}

VOID MallocAfter(ADDRINT ret)
{
  // Check if there's a pending event
  if (pending_malloc_events.empty()) {
    return;
  }
  
  // Get the pending event from stack
  trace_instr_format_t instr = pending_malloc_events.back();
  pending_malloc_events.pop_back();
  
  // Verify this is a malloc event
  if (instr.is_malloc != 1) {
    return;
  }
  
  malloc_outfile << "MALLOC_RET 0x" << std::hex << ret << std::dec << std::endl;
  instr.destination_memory[0] = ret;
  
  event_instr = instr;
  WriteEventInstruction();
  
  // Record this address in history for future free checks
  tracked_malloc_addresses.insert(ret);
}

VOID FreeBefore(ADDRINT ptr, ADDRINT ip)
{
  // Check if this free corresponds to a tracked malloc
  bool is_tracked = tracked_malloc_addresses.find(ptr) != tracked_malloc_addresses.end();
  
  if (!is_tracked) {
    return; // Skip frees of untracked mallocs
  }
  
  malloc_outfile << "FREE 0x" << std::hex << ptr << std::dec << std::endl;
  
  event_instr = {};
  event_instr.ip = (unsigned long long int)ip;
  event_instr.is_malloc = 4; // 4: free
  event_instr.source_memory[0] = ptr;
  WriteEventInstruction();
  
  // Remove from history after freeing
  tracked_malloc_addresses.erase(ptr);
}

VOID CallocBefore(ADDRINT nmemb, ADDRINT size, ADDRINT ip)
{
  ADDRINT total_size = nmemb * size;
  
  // Check if size meets threshold
  if (total_size < KnobMallocSizeThreshold.Value()) {
    return; // Skip small callocs
  }
  
  malloc_outfile << "CALLOC_SIZE " << nmemb << " " << size << std::endl;
  
  trace_instr_format_t instr = {};
  instr.ip = (unsigned long long int)ip;
  instr.is_malloc = 2; // 2: calloc
  instr.source_memory[0] = total_size;
  pending_malloc_events.push_back(instr);
}

VOID CallocAfter(ADDRINT ret)
{
  // Check if there's a pending event
  if (pending_malloc_events.empty()) {
    return;
  }
  
  // Get the pending event from stack
  trace_instr_format_t instr = pending_malloc_events.back();
  pending_malloc_events.pop_back();
  
  // Verify this is a calloc event
  if (instr.is_malloc != 2) {
    return;
  }
  
  malloc_outfile << "CALLOC_RET 0x" << std::hex << ret << std::dec << std::endl;
  instr.destination_memory[0] = ret;
  
  event_instr = instr;
  WriteEventInstruction();
  
  // Record this address in history for future free checks
  tracked_malloc_addresses.insert(ret);
}

VOID ReallocBefore(ADDRINT ptr, ADDRINT size, ADDRINT ip)
{
  // Check if size meets threshold
  if (size < KnobMallocSizeThreshold.Value()) {
    return; // Skip small reallocs
  }
  
  malloc_outfile << "REALLOC_SIZE" << std::dec << " " << size << " REALLOC_PTR 0x" << std::hex << ptr << std::dec << std::endl;
  
  trace_instr_format_t instr = {};
  instr.ip = (unsigned long long int)ip;
  instr.is_malloc = 3; // 3: realloc
  instr.source_memory[0] = size;
  instr.source_memory[1] = ptr;
  pending_malloc_events.push_back(instr);
}

VOID ReallocAfter(ADDRINT ret)
{
  // Check if there's a pending event
  if (pending_malloc_events.empty()) {
    return;
  }
  
  // Get the pending event from stack
  trace_instr_format_t instr = pending_malloc_events.back();
  pending_malloc_events.pop_back();
  
  // Verify this is a realloc event
  if (instr.is_malloc != 3) {
    return;
  }
  
  malloc_outfile << "REALLOC_RET 0x" << std::hex << ret << std::dec << std::endl;
  instr.destination_memory[0] = ret;
  
  event_instr = instr;
  WriteEventInstruction();
  
  // Update history: remove old address, add new address
  ADDRINT old_ptr = instr.source_memory[1];
  tracked_malloc_addresses.erase(old_ptr);
  tracked_malloc_addresses.insert(ret);
}

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

// RTN instrumentation for malloc/free/calloc/realloc
VOID ImageLoad(IMG img, VOID* v)
{
  RTN rtn = RTN_FindByName(img, "malloc");
  if (RTN_Valid(rtn)) {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)MallocBefore, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_INST_PTR, IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)MallocAfter, IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
    RTN_Close(rtn);
  }

  rtn = RTN_FindByName(img, "free");
  if (RTN_Valid(rtn)) {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)FreeBefore, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_INST_PTR, IARG_END);
    RTN_Close(rtn);
  }

  rtn = RTN_FindByName(img, "calloc");
  if (RTN_Valid(rtn)) {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)CallocBefore, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_INST_PTR, IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)CallocAfter, IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
    RTN_Close(rtn);
  }

  rtn = RTN_FindByName(img, "realloc");
  if (RTN_Valid(rtn)) {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)ReallocBefore, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_INST_PTR, IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)ReallocAfter, IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
    RTN_Close(rtn);
  }
}

// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID* v)
{
  // begin each instruction with this function
  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)ResetCurrentInstruction, IARG_INST_PTR, IARG_END);

  // instrument branch instructions
  if (INS_IsBranch(ins))
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)BranchOrNot, IARG_BRANCH_TAKEN, IARG_END);

  // instrument register reads
  UINT32 readRegCount = INS_MaxNumRRegs(ins);
  for (UINT32 i = 0; i < readRegCount; i++) {
    UINT32 regNum = INS_RegR(ins, i);
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteToSet<unsigned char>, IARG_PTR, curr_instr.source_registers, IARG_PTR,
                   curr_instr.source_registers + NUM_INSTR_SOURCES, IARG_UINT32, regNum, IARG_END);
  }

  // instrument register writes
  UINT32 writeRegCount = INS_MaxNumWRegs(ins);
  for (UINT32 i = 0; i < writeRegCount; i++) {
    UINT32 regNum = INS_RegW(ins, i);
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteToSet<unsigned char>, IARG_PTR, curr_instr.destination_registers, IARG_PTR,
                   curr_instr.destination_registers + NUM_INSTR_DESTINATIONS, IARG_UINT32, regNum, IARG_END);
  }

  // instrument memory reads and writes
  UINT32 memOperands = INS_MemoryOperandCount(ins);

  // Iterate over each memory operand of the instruction.
  for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
    if (INS_MemoryOperandIsRead(ins, memOp))
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteToSet<unsigned long long int>, IARG_PTR, curr_instr.source_memory, IARG_PTR,
                     curr_instr.source_memory + NUM_INSTR_SOURCES, IARG_MEMORYOP_EA, memOp, IARG_END);
    if (INS_MemoryOperandIsWritten(ins, memOp))
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteToSet<unsigned long long int>, IARG_PTR, curr_instr.destination_memory, IARG_PTR,
                     curr_instr.destination_memory + NUM_INSTR_DESTINATIONS, IARG_MEMORYOP_EA, memOp, IARG_END);
  }

  // finalize each instruction with this function
  INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)ShouldWrite, IARG_END);
  INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteCurrentInstruction, IARG_END);
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID* v) {
  if (!instr_buffer.empty()) {
    outfile.write(reinterpret_cast<const char*>(instr_buffer.data()), 
                 instr_buffer.size() * sizeof(trace_instr_format_t));
    instr_buffer.clear();
  }
  outfile.close();
  malloc_outfile.close();
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments,
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char* argv[])
{
  // Initialize PIN library. Print help message if -h(elp) is specified
  // in the command line or the command line is invalid
  PIN_InitSymbols();
  if (PIN_Init(argc, argv))
    return Usage();

  outfile.open(KnobOutputFile.Value().c_str(), std::ios_base::binary | std::ios_base::trunc);
  if (!outfile) {
    std::cout << "Couldn't open output trace file. Exiting." << std::endl;
    exit(1);
  }

  instr_buffer.reserve(INSTR_BUFFER_SIZE / sizeof(trace_instr_format_t));

  malloc_outfile.open(KnobMallocOutputFile.Value().c_str(), std::ios_base::out);
  if (!malloc_outfile) {
    std::cout << "Couldn't open malloc trace file. Exiting." << std::endl;
    exit(1);
  }

  // Register function to be called to instrument instructions
  INS_AddInstrumentFunction(Instruction, 0);

  // Register image load callback to instrument malloc/free/calloc/realloc
  IMG_AddInstrumentFunction(ImageLoad, 0);

  // Register function to be called when the application exits
  PIN_AddFiniFunction(Fini, 0);

  // Start the program, never returns
  PIN_StartProgram();

  return 0;
}
