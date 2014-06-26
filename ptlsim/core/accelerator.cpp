#include <globals.h>
#include <atomcore.h>
#include <machine.h>
#include <accelerator.h>
#include <interconnect.h>
#include <basecore.h>
#include <cpuController.h>
//#include <cacheTypes.h>


#include <hw/irq.h>
#include <hw/isa.h>

using namespace Core;
using namespace Memory;

uint64_t lap_mmio_reg;
uint64_t lap_buf_addr;
#define LAP_MMIO_ADDR 0x200000000

uint64_t cal_cycle_count;
uint64_t max_cal_cycle;

enum AccelState {
    Accel_Idle,
    Accel_Load_header,
    Accel_Load_content_row_major,
    Accel_Load_content_column_major,
    Accel_Load_content_block_major,
    Accel_Cal,
    Accel_Store,

    MAX_Accel_State
};

typedef struct {
    int m;
    int n;
} matrix_header_t;

typedef struct {
    int *A;
    int *B;
    int *C;
} matrix_data_t;

// Temporary variable to be used in testing memory Hierarchy.
AccelState temp_state = Accel_Idle;
W64 temp_virt_addr;
W64 temp_phys_addr;
W64 temp_uuid, temp_rip;

matrix_header_t matrix_header;
matrix_data_t matrix_data;
int *matrix_data_buf;
size_t matrix_data_buf_size;

std::map<W64, bool> cache_ready_map;

Accelerator::Accelerator(BaseMachine& machine, const char* name)
    : Statable(name, &machine)
      , name(name), machine(machine)
{
    // Per cycle event
	stringbuf sg_name;
	sg_name << name << "-run-cycle";
	run_cycle.set_name(sg_name.buf);
	run_cycle.connect(signal_mem_ptr(*this, &Accelerator::runcycle));
	marss_register_per_cycle_event(&run_cycle);

    // Interconnect for the accelarator and CPU controller
    stringbuf sig_name;
    sig_name << "Core" << 1 << "-Th" << 0 << "-dcache-wakeup";
    dcache_signal.set_name(sig_name.buf);
    dcache_signal.connect(signal_mem_ptr(*this, &Accelerator::load_cb));

}

void Accelerator::update_memory_hierarchy_ptr()
{
    memoryHierarchy = machine.memoryHierarchyPtr;
    memoryHierarchy->add_request_pool();

    //machine.setup_interconnects();
    //memoryHierarchy->setup_full_flags();
}

void Accelerator::init()
{
    // TODO: Specify id, instead of a magic number here.
    id = 1;
    cache_ready = false;

    ctx = &machine.contextof(0);

    // XXX Change this during experiment
    max_cal_cycle = 1000000L;

#if 0
    // Initialize ISA and IRQ information
    lap_isa_info.qdev.name = "lap",
    //lap_isa_info.qdev.no_user = 1,
    lap_isa_info.init = lap_init_fn;
    isa_qdev_register(&lap_isa_info);
    this->lap_irq = irq_lap;
#endif


    printf("Initiating Accelerator!\n");

}

// Check the MMIO register for request
bool Accelerator::do_idle(void *nothing)
{
    //W64 reg = 0;
    //reg = ctx->loadphys(LAP_MMIO_ADDR, false, 3); // sizeshift=3 for 64bit-data
    //printf("LAP MMIO register value = %ld", reg);

    //if (reg == 1) {
    if (lap_mmio_reg != 0) {
        cache_ready = true;
        temp_virt_addr = lap_buf_addr;
        // TODO Use actual physical address
        temp_phys_addr = lap_buf_addr;
        temp_rip = 0;
        temp_uuid = 0;
        printf("Inside do_idle: LAP mmio reg: %ld, Virtual Address: %p\n",
                lap_mmio_reg, temp_virt_addr);
        return true;
    }

    //printf("Inside do_idle: Virtual Address: %ld\n", temp_virt_addr);
    return false;
}


// Load the matrix header
bool Accelerator::do_load_header(void *nothing)
{
    int rc;

    printf("before load, size=%lu\n", sizeof(matrix_header));
    rc = load_buf(temp_virt_addr, temp_phys_addr,
            &matrix_header, sizeof(matrix_header), temp_rip, temp_uuid, true);
    if (rc != ACCESS_OK) {
        return false;
    }
    printf("after load\n");

    size_t size = matrix_header.m * matrix_header.n;
    matrix_data_buf_size = sizeof(int) * size * 3;
    matrix_data_buf = (int *) malloc(matrix_data_buf_size);
    matrix_data.A = matrix_data_buf;
    matrix_data.B = matrix_data.A + size;
    matrix_data.C = matrix_data.B + size;

    printf ("m=%d, n=%d\n", matrix_header.m, matrix_header.n);

    return true;
}

// Load the matrix content
bool Accelerator::do_load_content(void *nothing)
{
    int rc;

    rc = load_buf(temp_virt_addr+2*sizeof(int), temp_phys_addr+2*sizeof(int),
            matrix_data_buf, matrix_data_buf_size, temp_rip, temp_uuid, true);
    if (rc != ACCESS_OK) {
        return false;
    }

    return true;
}

// The Maximum number of elements in the experiment, used as array boundary.
#define MAX_SIZE_IN_TEST 100000000
// The row size for block-major access
#define BLOCK_ROW_SIZE 32
// The column size for block-major access
#define BLOCK_COLUMN_SIZE 32
// The row count for block-major access
#define BLOCK_ROW_COUNT 16
// The column count for block-major access
#define BLOCK_COLUMN_COUNT 16


// The maximum number of active requests in memory system.
#define MAX_REQUEST_COUNT 32

// The number of bytes in each memory request
#define MEM_REQ_SIZE 64

// The number of elements in each memory request
#define MEM_REQ_COUNT 16

// The number of words in each memory request
#define MEM_REQ_WORDS 8

// The number of rows that have been processed (used in row-major only).
int row_count;
// The number of columns that have been processed (used in column-major only).
int column_count;
// The number of blocks that have been processed (used in block-major only).
int block_count;

// The array indicates which cache line has been issued a request.
bool requested[MAX_SIZE_IN_TEST];

// wait_count indicates how many requests are still being processed by memory
// system.
int wait_count = 0;

// request_count indicates how many active requests are in the memory system.
int request_count = 0;

// Block-major access. Please read row-major comments first
bool Accelerator::do_load_content_block_major(void *nothing)
{
    int rc;

    W64 cur_virt_addr, cur_phys_addr;
    W64 base_virt_addr, base_phys_addr;
    W64 offset;
    W64 data;

    //base_virt_addr = temp_virt_addr + 2 * sizeof(int);
    //base_phys_addr = temp_phys_addr + 2 * sizeof(int);
    base_virt_addr = temp_virt_addr;
    base_phys_addr = temp_phys_addr;

    //printf("Inside Load Content Block Major.\n");
    // Block-based seek
    for (int i = 0; i < BLOCK_ROW_SIZE * BLOCK_COLUMN_SIZE / MEM_REQ_COUNT; ++i) {
        // Calculate row and column number based on block_count and block_index
        // (i)
        W64 row_offset = (block_count / BLOCK_COLUMN_COUNT) * BLOCK_ROW_SIZE + i / (BLOCK_COLUMN_SIZE/MEM_REQ_COUNT);
        W64 column_offset = (block_count % BLOCK_COLUMN_COUNT) * BLOCK_COLUMN_SIZE + i % (BLOCK_COLUMN_SIZE/MEM_REQ_COUNT) * MEM_REQ_COUNT;

        // Calculate offset using row and column number
        offset = (row_offset*matrix_header.n+column_offset) * sizeof(int);
        cur_virt_addr = base_virt_addr + offset;
        cur_phys_addr = base_phys_addr + offset;

        if unlikely (!requested[i] && request_count < MAX_REQUEST_COUNT) {
            rc = this->load(cur_virt_addr, cur_phys_addr, matrix_data_buf + offset / sizeof(int), temp_rip,
                    temp_uuid, false, 6);
            printf("FIRST ATTEMP to load [%lld, %lld], %p, result = %d, simcycle = %ld\n", row_offset, column_offset, cur_phys_addr, rc, sim_cycle);
            requested[i] = true;
            if (rc == ACCESS_OK) {
                ++wait_count;
            } else {
                cache_ready_map[cur_phys_addr] = false;
                ++request_count;
            }
        } else if unlikely (cache_ready_map[cur_phys_addr]) {
            rc = this->load(cur_virt_addr, cur_phys_addr, matrix_data_buf + offset / sizeof(int), temp_rip,
                    temp_uuid, true, 6);
            printf("SECOND ATTEMP to load [%lld, %lld], %p, result = %d, simcycle = %ld\n", row_offset, column_offset, cur_phys_addr, rc, sim_cycle);
            ++wait_count;
            cache_ready_map[cur_phys_addr] = false;
            --request_count;
        }
    }

    // When all rows have finished, proceed to the next column
    if unlikely (wait_count >= BLOCK_ROW_SIZE * BLOCK_COLUMN_SIZE/MEM_REQ_COUNT) {
        printf("Block_count = %d, Wait_count = %d\n", block_count, wait_count);

        ++block_count;
        wait_count = 0;

        for (int i = 0; i < MAX_SIZE_IN_TEST; ++i) {
            requested[i] = false;
        }
    }

    // When all blocks have finished, return true indicates this stage has
    // finished.
    if (block_count >= BLOCK_ROW_COUNT * BLOCK_COLUMN_COUNT) {
        return true;
    } else {
        return false;
    }
}

// Column-major access. Please read row-major comments first.
bool Accelerator::do_load_content_column_major(void *nothing)
{
    int rc;
    //bool all_ready = false;

    W64 cur_virt_addr, cur_phys_addr;
    W64 base_virt_addr, base_phys_addr;
    W64 offset;
    W64 data;

    //base_virt_addr = temp_virt_addr + 2 * sizeof(int);
    //base_phys_addr = temp_phys_addr + 2 * sizeof(int);
    base_virt_addr = temp_virt_addr;
    base_phys_addr = temp_phys_addr;

    // Column-major seek

    // i indicates the current row number
    for (int i = 0; i < matrix_header.m; ++i) {
        // Calculate offset based on row number (i) and column number
        offset = (i*matrix_header.n+column_count) * sizeof(int);
        cur_virt_addr = base_virt_addr + offset;
        cur_phys_addr = base_phys_addr + offset;

        if unlikely (!requested[i] && request_count < MAX_REQUEST_COUNT) {
            rc = this->load(cur_virt_addr, cur_phys_addr, matrix_data_buf + offset / sizeof(int), temp_rip,
                    temp_uuid, false, 6);
            //printf("FIRST ATTEMP to load %p, result = %d, simcycle = %ld\n", cur_phys_addr, rc, sim_cycle);
            requested[i] = true;
            if (rc == ACCESS_OK) {
                ++wait_count;
            }
            cache_ready_map[cur_phys_addr] = false;
            ++request_count;
        } else if unlikely (cache_ready_map[cur_phys_addr]) {
            rc = this->load(cur_virt_addr, cur_phys_addr, matrix_data_buf + offset / sizeof(int), temp_rip,
                    temp_uuid, true, 6);
            //printf("SECOND ATTEMP to load %p, result = %d, simcycle = %ld\n", cur_phys_addr, rc, sim_cycle);
            ++wait_count;
            cache_ready_map[cur_phys_addr] = false;
            --request_count;
        }
    }

    //printf("wait_count = %d, column_count = %d\n", wait_count, column_count);
    // When all rows have finished, proceed to the next column
    if unlikely (wait_count == matrix_header.m) {
        printf("Column_count = %d, Wait_count = %d\n", column_count, wait_count);

        // Each time, column count increases by the number of elements per cache
        // line.
        column_count += MEM_REQ_COUNT;
        wait_count = 0;

        for (int i = 0; i < matrix_header.m; ++i) {
            requested[i] = false;
        }
    }

    // When all columns have finished, return true indicates this stage has
    // finished.
    if (column_count >= matrix_header.n) {
        return true;
    } else {
        return false;
    }
}

// Load the content of matrix based on row-major order
bool Accelerator::do_load_content_row_major(void *nothing)
{
    int rc;
    //bool all_ready = false;

    // The address of current cache line.
    W64 cur_virt_addr, cur_phys_addr;

    // The address of the first cache line (start of matrix)
    W64 base_virt_addr, base_phys_addr;

    // The difference between cur_addr and base_addr
    W64 offset;

    // The data
    W64 data[MEM_REQ_COUNT];

    //base_virt_addr = temp_virt_addr + 2 * sizeof(int);
    //base_phys_addr = temp_phys_addr + 2 * sizeof(int);
    base_virt_addr = temp_virt_addr;
    base_phys_addr = temp_phys_addr;

    // Row-major seek
    // j is the number of cache line within a row
    for (int j = 0; j < matrix_header.n / MEM_REQ_COUNT; ++j) {
        // Calculate the offset based on row and column count
        offset = (row_count*matrix_header.n + j * MEM_REQ_COUNT) * sizeof(int);
        cur_virt_addr = base_virt_addr + offset;
        cur_phys_addr = base_phys_addr + offset;

        // If the memory address has not been requested
        if unlikely (!requested[j] && request_count < MAX_REQUEST_COUNT) {
            // Magic number 6 indicates 2^6=64 bytes
            // Send out request
            rc = this->load(cur_virt_addr, cur_phys_addr, matrix_data_buf + offset / sizeof(int), temp_rip,
                    temp_uuid, false, 6);
            printf("FIRST ATTEMP to load %p, result = %d, simcycle = %ld\n", cur_phys_addr, rc, sim_cycle);
            requested[j] = true;

            // If cache hit (impossible for no cache case), directly store it
            if (rc == ACCESS_OK) {
                ++wait_count;
            }

            // Set cache ready to false to wait for the result
            cache_ready_map[cur_phys_addr] = false;
            ++request_count;
        }
        // If the cache line is ready from the memory system
        else if unlikely (cache_ready_map[cur_phys_addr]) {
            // Magic number 6 indicates 2^6=64 bytes
            // Read the data
            rc = this->load(cur_virt_addr, cur_phys_addr,
                    matrix_data_buf + offset / sizeof(int), temp_rip,
                    temp_uuid, true, 6);
            printf("SECOND ATTEMP to load %p, result = %d, simcycle = %ld\n", cur_phys_addr, rc, sim_cycle);
            printf("data = %d, offset = %d\n", matrix_data_buf[offset/sizeof(int)], offset);
            ++wait_count;

            // Set cache ready to false indicates no longer need to read the
            // data
            cache_ready_map[cur_phys_addr] = false;
            --request_count;
        }
    }

    //printf("wait_count = %d, row_count = %d\n", wait_count, row_count);
    // When all elements in the same row finishes, proceed to the next row.
    if unlikely (wait_count == matrix_header.n / MEM_REQ_COUNT) {
        printf("Row_count = %d, Wait_count = %d\n", row_count, wait_count);

        ++row_count;
        wait_count = 0;

        for (int j = 0; j < matrix_header.n; ++j) {
            requested[j] = false;
        }
    }

    // When all rows have been processed, return true indicates this stage has
    // finished.
    if (row_count >= matrix_header.m) {
        return true;
    } else {
        return false;
    }
}

void print_matrix(int *A) {
    for (int i = 0; i < matrix_header.m; ++i) {
        for (int j = 0; j < matrix_header.n; ++j) {
            printf("%d ", A[i*matrix_header.n+j]);
        }
        printf("\n");
    }
}

bool Accelerator::do_calculate(void *nothing)
{
    // Debugging: print out A, B
    //print_matrix(matrix_data.A);
    //print_matrix(matrix_data.B);

    // XXX Current dummy operation: C = A + B
#if 0
    for (int i = 0; i < matrix_header.m; ++i) {
        for (int j = 0; j < matrix_header.n; ++j) {
            matrix_data.C[i*matrix_header.n+j] =
                matrix_data.A[i*matrix_header.n+j] +
                matrix_data.B[i*matrix_header.n+j];
        }
    }
#endif

    //print_matrix(matrix_data.C);

    //return ((++cal_cycle_count) >= max_cal_cycle);
    return true;
}

bool Accelerator::do_store(void *nothing)
{
    int rc = ACCESS_OK;

    // DEBUG Print out data
#if 0
    for (int i = 0; i < matrix_data_buf_size/3; ++i) {
        printf("%d ", matrix_data_buf + i * sizeof(int));
    }
    printf("\n");
#endif

    rc = store(temp_virt_addr+2*sizeof(int), temp_phys_addr+2*sizeof(int),
            matrix_data_buf, matrix_data_buf_size, temp_rip, temp_uuid, true);
    printf("matrix_data_buf_size = %lu\n", matrix_data_buf_size);
    if (rc != ACCESS_OK) {
        return false;
    }

    printf("Going to send finish signal\n");

    // XXX Memory leak, left for test right now
    //free(matrix_data_buf);

    //int bytemask = ((1 << (1 << 3))-1);
    //W64 reg = ctx->storemask(LAP_MMIO_ADDR, 0, bytemask); // sizeshift=3 for 64bit-data
    lap_mmio_reg = 0;

    // Testing with a delay here, see if the delay causes the bug.
    //int delay = 100;
    //marss_add_event(core_wakeup_signal, delay, NULL);


    // Send an interrupt to the CPU
    send_interrupt();
    return true;
}

void Accelerator::send_interrupt()
{
    //ctx->handle_interrupt = 1;
    //ctx->exception_is_int = 0;
    //ctx->interrupt_request |= CPU_INTERRUPT_HARD;

    lap_finish();
#if 0
    qemu_set_irq(lap_irq, 1);
    qemu_set_irq(lap_irq, 0);
#endif
}

// Currently it only tests loading in the memory Hierarchy.
// Return true if exit to QEMU is requested.
bool Accelerator::runcycle(void *nothing)
{
    switch (temp_state) {
        case Accel_Load_header:
            if (cache_ready) {
                if (do_load_header(nothing)) {
                    printf("Load Header complete!\n");
                    row_count = 0;
                    column_count = 0;
                    block_count = 0;
                    request_count = 0;
                    wait_count = 0;
                    for (int i = 0; i < MAX_SIZE_IN_TEST; ++i)
                        requested[i] = false;
                    temp_state = Accel_Load_content_row_major;
                    printf("Current cycle: %ld\n", sim_cycle);
                }
            }
            break;

        case Accel_Load_content_row_major:
            if (do_load_content_row_major(nothing)) {
                printf("Load Content row-major complete!\n");
                column_count = 0;
                row_count = 0;
                block_count = 0;
                request_count = 0;
                wait_count = 0;
                for (int i = 0; i < MAX_SIZE_IN_TEST; ++i)
                    requested[i] = false;
                temp_state = Accel_Load_content_column_major;
                //temp_state = Accel_Cal;
                printf("Current cycle: %ld\n", sim_cycle);
            }
            break;

        case Accel_Load_content_column_major:
            if (do_load_content_column_major(nothing)) {
                printf("Load Content column-major complete!\n");
                column_count = 0;
                row_count = 0;
                block_count = 0;
                request_count = 0;
                wait_count = 0;
                for (int i = 0; i < MAX_SIZE_IN_TEST; ++i)
                    requested[i] = false;
                temp_state = Accel_Load_content_block_major;
                //temp_state = Accel_Cal;
                printf("Current cycle: %ld\n", sim_cycle);
            }
            break;

        case Accel_Load_content_block_major:
            if (do_load_content_block_major(nothing)) {
                printf("Load Content block-major complete!\n");
                column_count = 0;
                row_count = 0;
                block_count = 0;
                request_count = 0;
                wait_count = 0;
                for (int i = 0; i < MAX_SIZE_IN_TEST; ++i)
                    requested[i] = false;
                temp_state = Accel_Cal;
                printf("Current cycle: %ld\n", sim_cycle);
            }
            break;

        case Accel_Cal:
            if (do_calculate(nothing)) {
                printf("Cal complete!\n");
                temp_state = Accel_Store;
            }
            break;

        case Accel_Store:
            printf("storing!\n");
            if (do_store(nothing)) {
                printf("Storing complete!\n");
                temp_state = Accel_Idle;
            }
            break;

        default:
        case Accel_Idle:
            if (do_idle(nothing)) {
                printf("LAP Request Received! switching to load_header.");
                temp_state = Accel_Load_header;
            }
            break;

    }

    return false;
}

int Accelerator::load(W64 virt_addr, W64 phys_addr, void* data, W64 rip, W64 uuid, bool is_requested, int sizeshift)
{
    bool hit;

    Memory::MemoryRequest *request = memoryHierarchy->get_free_request(id);
    assert(request != NULL);

    request->init(id, 0, phys_addr, 0, sim_cycle,
            false, rip /* What should be the RIP here? */,
            uuid /* What should be the UUID here? */,
            Memory::MEMORY_OP_READ);
    request->set_coreSignal(&dcache_signal);

    if (!is_requested) {
        hit = memoryHierarchy->access_cache(request);

        if (!hit) {
            // Handle Cache Miss
            cache_ready = false;
            return ACCESS_CACHE_MISS;
        }
    }

    // On cache hit, retrieve data from the memory location.

    if (sizeshift <= 3) {
        bool old_kernel_mode = ctx->kernel_mode;
        ctx->kernel_mode = true;
        *((W64*) data) = ctx->loadvirt(virt_addr, sizeshift); // sizeshift=3 for 64bit-data
        ctx->kernel_mode = old_kernel_mode;
    } else {
        W64 count = 1 << (sizeshift - 3);

        bool old_kernel_mode = ctx->kernel_mode;
        ctx->kernel_mode = true;
        for (int i = 0; i < count; ++i) {
            // Load 8 byte at a time
            *(((W64*)data) + i) = ctx->loadvirt(virt_addr + i * 8, 3);
        }
        ctx->kernel_mode = old_kernel_mode;
    }

    return ACCESS_OK;
}

int Accelerator::store(W64 virt_addr, W64 phys_addr, W64& data, W64 rip, W64 uuid, bool is_requested, int sizeshift)
{
    ATOM_CORE_MODEL::StoreBufferEntry buf;

    buf.data = data;
    buf.addr = phys_addr;
    buf.virtaddr = virt_addr;
    // (1<<UOP_SIZE) is the number of bytes in the data
#define UOP_SIZE sizeshift
    buf.bytemask = ((1 << (1 << UOP_SIZE))-1);
    buf.size = UOP_SIZE;
    // Be careful not to use buf.op below.
    buf.op = NULL;
    buf.mmio = ctx->is_mmio_addr(virt_addr, true);

    Memory::MemoryRequest *request = memoryHierarchy->get_free_request(id);
    //printf("id = %d\n", id);
    assert(request != NULL);

    request->init(id, 0, phys_addr, 0, sim_cycle,
            false, rip /* What should be the RIP here? */,
            uuid /* What should be the UUID here? */,
            Memory::MEMORY_OP_WRITE);
    request->set_coreSignal(&dcache_signal);

    //memoryHierarchy->access_cache(request);

    //printf("Writing to RAM: virtaddr = %llu, data = %llu, bytemask = %d, size = %d\n",
    //        buf.virtaddr, buf.data, buf.bytemask, buf.size);

    //printf("STORE virtaddr=%llx, data=%ld, sizeshift=%d\n", virt_addr, data, sizeshift);
    bool old_kernel_mode = ctx->kernel_mode;
    ctx->kernel_mode = true;
    buf.write_to_ram(*ctx);
    ctx->kernel_mode = old_kernel_mode;
    //ctx->storemask(buf.addr, buf.data, buf.bytemask);

    return ACCESS_OK;
}

int Accelerator::store(W64 virt_addr, W64 phys_addr, void *data, size_t size, W64 rip, W64 uuid, bool is_requested)
{
    int rc;
    int sizeshift;
    W64 word;
    int ret = ACCESS_OK;
    W64 cur_virt_addr, cur_phys_addr;

    for (int i = 0; i < size; i += (1<<sizeshift)) {
        if (size - i >= 8) {
            sizeshift = 3;
        } else if (size-i >= 4){
            sizeshift = 2;
        } else if (size-i >= 2){
            sizeshift = 1;
        } else if (size-i >= 1){
            sizeshift = 0;
        } else {
            assert(0);
        }

        // Page table?
        cur_virt_addr = virt_addr + i;
        cur_phys_addr = phys_addr + i;

        switch (sizeshift) {
            case 0: {
                byte *b = (byte *)((byte *)data + i);
                word = *b;
                break;
                    }

            case 1: {
                W16 *b = (W16 *)((byte *)data + i);
                word = *b;
                break;
                    }

            case 2: {
                W32 *b = (W32 *)((byte *)data + i);
                word = *b;
                break;
                    }

            case 3: {
                W64 *b = (W64 *)((byte *)data + i);
                word = *b;
                break;
                    }

            default:
                printf("Unsupported sizeshift: %d", sizeshift);
                break;
        }

        rc = store(cur_virt_addr, cur_phys_addr, word, rip, uuid, is_requested, sizeshift);
        if (rc < 0) {
            ret = ACCESS_CACHE_MISS;
        }
    }

    return ret;
}

int Accelerator::load_buf(W64 virt_addr, W64 phys_addr, void *data, size_t size, W64 rip, W64 uuid, bool is_requested)
{
    int rc;
    int sizeshift;
    W64 word;
    int ret = ACCESS_OK;
    W64 cur_virt_addr, cur_phys_addr;

    for (int i = 0; i < size; i += (1<<sizeshift)) {
        if (size - i >= 8) {
            sizeshift = 3;
        } else if (size-i >= 4){
            sizeshift = 2;
        } else if (size-i >= 2){
            sizeshift = 1;
        } else if (size-i >= 1){
            sizeshift = 0;
        } else {
            assert(0);
        }

        cur_virt_addr = virt_addr + i;
        cur_phys_addr = phys_addr + i;

        //printf("cur_virt_addr = %llu\n", cur_virt_addr);
        rc = load(cur_virt_addr, cur_phys_addr, &word, rip, uuid, is_requested, sizeshift);
        if (rc < 0) {
            ret = ACCESS_CACHE_MISS;
        } else {
            switch (sizeshift) {
                case 0: {
                    byte *b = (byte *)((byte *)data + i);
                    *b = word;
                    break;
                        }

                case 1: {
                    W16 *b = (W16 *)((byte *)data + i);
                    *b = word;
                    break;
                        }

                case 2: {
                    W32 *b = (W32 *)((byte *)data + i);
                    *b = word;
                    break;
                        }

                case 3: {
                    W64 *b = (W64 *)((byte *)data + i);
                    *b = word;
                    break;
                        }

                default:
                    printf("Unsupported sizeshift: %d", sizeshift);
                    break;
            }
        }


    }

    return ret;
}

// Handle data path when a load request finishes.
bool Accelerator::load_cb(void *arg)
{
    Memory::MemoryRequest* req = (Memory::MemoryRequest*)arg;
    // TODO: Set the flag correlate to the requested memory
    //printf("Inside Accelerator dcache callback.\n");
    cache_ready = true;
    cache_ready_map[req->get_physical_address()] = true;
    //printf("Ready for physical address: %p\n", req->get_physical_address());

    return true;
}

W64 Accelerator::exec(AcceleratorArg& arg)
{

    // The following are commands to be executed.
    printf("Caught an accelerator exec!\n");
    printf("arg = %llu\n", arg.virt_addr);
    temp_virt_addr = arg.virt_addr;
    temp_phys_addr = arg.phys_addr;
    temp_rip = arg.rip;
    temp_uuid = arg.uuid;

    printf("virt_addr = %llu\n", arg.virt_addr);
    printf("phys_addr = %llu\n", arg.phys_addr);

    cache_ready = true;
    temp_state = Accel_Load_header;

    return 0;
}

