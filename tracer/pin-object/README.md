# Intel PIN tracer

The included PIN tool `champsim_tracer.cpp` can be used to generate new traces.
It has been tested (April 2022) using PIN 3.22.

## Download and install PIN

Download the source of PIN from Intel's website, then build it in a location of your choice.

    wget https://software.intel.com/sites/landingpage/pintool/downloads/pin-3.22-98547-g7a303a835-gcc-linux.tar.gz
    tar zxf pin-3.22-98547-g7a303a835-gcc-linux.tar.gz
    cd pin-3.22-98547-g7a303a835-gcc-linux/source/tools
    make
    export PIN_ROOT=/your/path/to/pin

## Building the tracer

The provided makefile will generate `obj-intel64/champsim_tracer.so`.

    make
    $PIN_ROOT/pin -t obj-intel64/champsim_tracer.so -- <your program here>

The tracer has several options you can set:
```
-o
Specify the output file for your trace.
The default is default_trace.champsim

-m
Specify the output file for memory allocation trace (malloc/free/mmap).
The default is malloc.trace

-s <number>
Specify the number of instructions to skip in the program before tracing begins.
The default value is 0.

-t <number>
The number of instructions to trace, after -s instructions have been skipped.
The default value is 1,000,000.

-k <size>
Specify the minimum memory allocation size to trace (in bytes).
Only malloc/calloc/realloc/mmap calls with size >= this threshold will be traced.
Additionally, only free()/munmap() calls that correspond to previously tracked allocations will be traced.
The default value is 0 (trace all memory operations).
```

For example, you could trace 200,000 instructions of the program ls, after skipping the first 100,000 instructions, with this command:

    pin -t obj/champsim_tracer.so -o traces/ls_trace.champsim -s 100000 -t 200000 -- ls

To trace only memory allocations of 4KB or larger:

    pin -t obj/champsim_tracer.so -o traces/ls_trace.champsim -k 4096 -- ls

This will significantly reduce trace size by filtering out small allocations while maintaining consistency between allocation and deallocation operations.

Traces created with the champsim_tracer.so are approximately 64 bytes per instruction, but they generally compress down to less than a byte per instruction using xz compression.

## Memory Allocation Tracking

### Overview

The tracer includes comprehensive tracking of memory allocation operations:
- **Heap allocations**: malloc, calloc, realloc, free
- **Memory mappings**: mmap, munmap, mremap

### Features

1. **Size-based Filtering**: Only track memory allocations above a configurable threshold (`-k` option)
2. **Consistent Deallocation Tracking**: Only trace free()/munmap() calls for allocations that were previously tracked
3. **Application-level Only**: Only tracks allocations made by the application code, not standard library internal calls
4. **Full-program Tracing**: Tracks all memory operations throughout the entire program execution (not affected by -s/-t flags)
5. **Single-threaded Optimization**: No locking overhead for better performance in single-threaded applications

### Log Format

Memory allocation events are logged in a standardized format:
- `malloc(size)=0xaddress`
- `calloc(size)=0xaddress`
- `realloc(size, 0xold_ptr)=0xnew_address`
- `free(0xptr)`
- `mmap(size)=0xaddress`
- `munmap(0xptr, size)`
- `mremap(new_size, 0xold_addr, old_size)=0xnew_address`

### How It Works

#### Data Structures

- `tracked_malloc_addresses`: A hash set storing addresses of all tracked heap allocations
- `tracked_mmap_addresses`: A hash set storing addresses of all tracked memory mappings
- `pending_malloc_events`: A stack ensuring proper Before/After callback pairing for heap operations
- `pending_mmap_events`: A stack ensuring proper Before/After callback pairing for memory mapping operations

#### Workflow

**For malloc/calloc/realloc:**
1. **Before callback**: Check size threshold → If meets threshold, save event info to pending stack
2. **After callback**: Retrieve from pending stack → Add return value → Write to trace → Record address in history

**For mmap/mremap:**
1. **Before callback**: Check size threshold → If meets threshold, save event info to pending stack
2. **After callback**: Retrieve from pending stack → Check for MAP_FAILED → Add return value → Write to trace → Record address in history

**For free:**
1. **Before callback**: Check if pointer exists in tracked addresses → If yes, write to trace → Remove from history

**For munmap:**
1. **Before callback**: Check if address exists in tracked mmap addresses → If yes, write to trace → Remove from history

#### Application-level Filtering

The tracer uses function-level (RTN) instrumentation for memory allocation functions to generate human-readable logs in `malloc.trace`:
- Captures malloc/calloc/realloc/free/mmap/munmap/mremap calls at function entry/exit points
- Generates standardized log entries like `malloc(4096)=0x7f8b4c0008c0`

**Instruction Trace Completeness**: 
- All instructions, including those within memory allocation functions, are fully traced into `champsim.trace`
- This ensures complete instruction flow and maintains trace integrity for ChampSim simulation
- The function-level instrumentation is **additional** to instruction tracing, not a replacement

This dual-layer approach provides both:
1. Complete instruction traces for accurate cache behavior simulation
2. Human-readable memory operation logs for object lifecycle analysis

#### Full-program Tracing

**Important**: Memory allocation tracking operates independently from instruction tracing:
- The `-s` (skip instructions) and `-t` (trace count) flags **do not affect** malloc/mmap tracking
- All memory operations are traced throughout the entire program execution
- This ensures complete object lifecycle information even when instruction tracing is limited

If you need to synchronize memory tracking with instruction windows, you would need to modify the Before callbacks to check `ShouldWrite()`.

### Example Usage

Trace a program with mmap tracking enabled (threshold 4KB):

    pin -t obj-intel64/champsim_tracer.so -o app.trace -m alloc.log -k 4096 -- ./my_application

The `alloc.log` file will contain entries like:

    malloc(1024)=0x7f8b4c0008c0
    mmap(65536)=0x7f8b4c010000
    free(0x7f8b4c0008c0)
    munmap(0x7f8b4c010000, 65536)
