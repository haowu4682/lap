#include <globals.h>
#include <atomcore.h>
#include <machine.h>
#include <accelerator.h>
#include <interconnect.h>
#include <basecore.h>
#include <cpuController.h>
//#include <cacheTypes.h>

using namespace Core;
using namespace Memory;

enum AccelState {
    Accel_Idle,
    Accel_Load_header,
    Accel_Load_content,
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

    printf("Initiating Accelerator!\n");

#if 0
    //Memory::CPUController *cpu_controller =
    //    new Memory::CPUController(id, name, memoryHierarchy);

    // Initialize Cache Hierarchy
    // TODO Move this using config files
    // Hard-coded shared-L2 module

    // CPU Controller
    ControllerBuilder::add_new_cont(machine, 1, "core_", "cpu", 0);

    // XXX This may be useless for a LAP but we leave it here to be consistent
    // L1 insn cache
    // TODO Replace the magic number with a built-in config script.
    machine.add_option("L1_I_", 1, "last_private", true);
    machine.add_option("L1_I_", 1, "private", true);
    ControllerBuilder::add_new_cont(machine, 1, "L1_I_", "mesi_cache", 6/*L1_128K_MESI*/);

    // L1 data cache
    // TODO Replace the magic number with a built-in config script.
    machine.add_option("L1_D_", 1, "last_private", true);
    machine.add_option("L1_D_", 1, "private", true);
    ControllerBuilder::add_new_cont(machine, 1, "L1_D_", "mesi_cache", 6/*L1_128K_MESI*/);

    foreach(i, 1) {
        // L1 insn cache connection
        ConnectionDef* connDef = machine.get_new_connection_def("p2p",
                    "p2p_core_L1_I_", 1);
        stringbuf core_;
        core_ << "core_" << 1;
        machine.add_new_connection(connDef, core_.buf, INTERCONN_TYPE_I);

        stringbuf L1_I_;
        L1_I_ << "L1_I_" << 1;
        machine.add_new_connection(connDef, L1_I_.buf, INTERCONN_TYPE_UPPER);

        Controller** cont = machine.controller_hash.get(core_);
        assert(cont);
        CPUController* cpuCont = (CPUController*)((*cont));
        cpuCont->set_dcacheLineBits(log2(64));
    }

    foreach(i, 1) {
        // L1 data cache connection
        ConnectionDef* connDef = machine.get_new_connection_def("p2p",
                    "p2p_core_L1_D_", 1);
        stringbuf core_;
        core_ << "core_" << 1;
        machine.add_new_connection(connDef, core_.buf, INTERCONN_TYPE_D);

        stringbuf L1_D_;
        L1_D_ << "L1_D_" << 1;
        machine.add_new_connection(connDef, L1_D_.buf, INTERCONN_TYPE_UPPER);

        Controller** cont = machine.controller_hash.get(core_);
        assert(cont);
        CPUController* cpuCont = (CPUController*)((*cont));
        cpuCont->set_dcacheLineBits(log2(64));
    }

    // L1-L2 connection
    foreach(i, 1) {
        ConnectionDef* connDef = machine.get_new_connection_def("split_bus",
                "split_bus_0", i);

        foreach(j, 2) {
            stringbuf L1_I_;
            L1_I_ << "L1_I_" << j;
            machine.add_new_connection(connDef, L1_I_.buf, INTERCONN_TYPE_LOWER);
        }


        stringbuf L2_0;
        L2_0 << "L2_0";
        machine.add_new_connection(connDef, L2_0.buf, INTERCONN_TYPE_UPPER);


        foreach(j, 2) {
            stringbuf L1_D_;
            L1_D_ << "L1_D_" << j;
            machine.add_new_connection(connDef, L1_D_.buf, INTERCONN_TYPE_LOWER);
        }
    }
    connDef = machine.get_new_connection_def("p2p",
            "p2p_L2_0_L1_D_0", 1);


    stringbuf L2_0;
    L2_0 << "L2_0";
    machine.add_new_connection(connDef, L2_0.buf, INTERCONN_TYPE_UPPER2);

    stringbuf L1_D_0;
    L1_D_0 << "L1_D_0";
    machine.add_new_connection(connDef, L1_D_0.buf, INTERCONN_TYPE_LOWER);

    machine.setup_interconnects();
    machine.memoryHierarchyPtr->setup_full_flags();

    printf("Accelerator Initialization finished!\n");
#endif
}

// Load the matrix header
bool Accelerator::do_load_header(void *nothing)
{
    int rc;

    printf("before load, size=%d\n", sizeof(matrix_header));
    rc = load(temp_virt_addr, temp_phys_addr,
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

    rc = load(temp_virt_addr+2*sizeof(int), temp_phys_addr+2*sizeof(int),
            matrix_data_buf, matrix_data_buf_size, temp_rip, temp_uuid, true);
    if (rc != ACCESS_OK) {
        return false;
    }

    return true;
}

void print_matrix(int *A) {
    for (int i = 0; i < matrix_header.m; ++i) {
        for (int j = 0; j < matrix_header.n; ++j) {
            printf("%d ", A[i*matrix_header.m+j]);
        }
        printf("\n");
    }
}

bool Accelerator::do_calculate(void *nothing)
{
    // Debugging: print out A, B
    print_matrix(matrix_data.A);
    print_matrix(matrix_data.B);

    // XXX Current dummy operation: C = A + B
    for (int i = 0; i < matrix_header.m; ++i) {
        for (int j = 0; j < matrix_header.n; ++j) {
            matrix_data.C[i*matrix_header.m+j] =
                matrix_data.A[i*matrix_header.m+j] +
                matrix_data.B[i*matrix_header.m+j];
        }
    }

    return true;
}

bool Accelerator::do_store(void *nothing)
{
    // XXX Magic number to be stored to the location.
    int rc;

    rc = store(temp_virt_addr+2*sizeof(int), temp_phys_addr+2*sizeof(int),
            matrix_data_buf, matrix_data_buf_size, temp_rip, temp_uuid, true);
    if (rc != ACCESS_OK) {
        return false;
    }

    // XXX Memory leak
    //free(matrix_data_buf);

    // XXX Testing with a delay here.
    int delay = 1;
    marss_add_event(core_wakeup_signal, delay, NULL);
    return true;
}

// Currently it only tests loading in the memory Hierarchy.
// Return true if exit to QEMU is requested.
bool Accelerator::runcycle(void *nothing)
{
    switch (temp_state) {
        case Accel_Load_header:
            printf("Loading header!\n");
            if (cache_ready) {
                if (do_load_header(nothing)) {
                    temp_state = Accel_Load_content;
                }
            }
            break;

        case Accel_Load_content:
            printf("Loading content!\n");
            if (cache_ready) {
                if (do_load_content(nothing)) {
                    temp_state = Accel_Cal;
                }
            }
            break;

        case Accel_Cal:
            printf("calculating!\n");
            if (do_calculate(nothing)) {
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
            // do nothing
            break;
    }

    return false;
}

int Accelerator::load(W64 virt_addr, W64 phys_addr, W64& data, W64 rip, W64 uuid, bool is_requested, int sizeshift)
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
            printf("Accelerator Cache Miss!\n");
            cache_ready = false;
            return ACCESS_CACHE_MISS;
        }
    }

    //printf("Accelerator Cache Hit!\n");
    // On cache hit, retrieve data from the memory location.
    // TODO: use PHYSICAL address here.
    printf("virtaddr=%llu, sizeshift=%d\n", virt_addr, sizeshift);
    data = ctx->loadvirt(virt_addr, sizeshift); // sizeshift=3 for 64bit-data

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

    memoryHierarchy->access_cache(request);

    //printf("Writing to RAM: virtaddr = %llu, data = %llu, bytemask = %d, size = %d\n",
    //        buf.virtaddr, buf.data, buf.bytemask, buf.size);

    buf.write_to_ram(*ctx);

    return ACCESS_OK;
}

int Accelerator::store(W64 virt_addr, W64 phys_addr, void *data, size_t size, W64 rip, W64 uuid, bool is_requested)
{
    int rc;
    int sizeshift;
    W64 word;
    int ret = ACCESS_OK;
    W64 cur_virt_addr, cur_phys_addr;

    for (int i = 0; i < size; i += 3) {
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

int Accelerator::load(W64 virt_addr, W64 phys_addr, void *data, size_t size, W64 rip, W64 uuid, bool is_requested)
{
    int rc;
    int sizeshift;
    W64 word;
    int ret = ACCESS_OK;
    W64 cur_virt_addr, cur_phys_addr;

    for (int i = 0; i < size; i += 8) {
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

        printf("cur_virt_addr = %llu\n", cur_virt_addr);
        rc = load(cur_virt_addr, cur_phys_addr, word, rip, uuid, is_requested, sizeshift);
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
    //Memory::MemoryRequest* req = (Memory::MemoryRequest*)arg;
    // TODO: Set the flag correlate to the requested memory
    //printf("Inside Accelerator dcache callback.\n");
    cache_ready = true;


    return true;
}

#if 0
int Accelerator::load_blocked(W64 addr, W64& data)
{
    int rc = ACCESS_OK;

    printf("Trying to load.\n");
    cache_ready = true;
    do {
        if (cache_ready) {
            //rc = load(addr, data);
        }
        if (rc != ACCESS_OK && rc != ACCESS_CACHE_MISS) {
            return rc;
        }
    } while (rc != ACCESS_OK);

    return ACCESS_OK;
}
#endif

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
#if 0
    W64 data;
    int rc = load(arg.virt_addr, arg.phys_addr, data, arg.rip, arg.uuid, false);
    printf("Load finished!\n");
    if (rc == ACCESS_OK) {
        printf("Loaded data = %llu\n", data);
        return data;
    } else if (rc == ACCESS_CACHE_MISS) {
        printf("Memory load encounters a cache miss!\n");

        return arg.virt_addr;
    } else {
        printf("Memory load failed!\n");
        return arg.virt_addr;
    }
#endif
    return 0;
}

