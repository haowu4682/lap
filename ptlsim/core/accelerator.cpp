#include <globals.h>
#include <machine.h>
#include <accelerator.h>
#include <interconnect.h>
#include <basecore.h>
#include <cpuController.h>
//#include <cacheTypes.h>

using namespace Core;
using namespace Memory;

// Temporary variable to be used in testing memory Hierarchy.
bool temp_need_load = false;
W64 temp_virt_addr;
W64 temp_phys_addr;
W64 temp_uuid, temp_rip;

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
    sig_name << "Core" << id << "-Th" << 0 << "-dcache-wakeup";
    dcache_signal.set_name(sig_name.buf);
    dcache_signal.connect(signal_mem_ptr(*this, &Accelerator::load_cb));

}

void Accelerator::update_memory_hierarchy_ptr()
{
    memoryHierarchy = machine.memoryHierarchyPtr;
    memoryHierarchy->add_request_pool();

    machine.setup_interconnects();
    memoryHierarchy->setup_full_flags();
}

void Accelerator::init()
{
    // TODO: Specify id, instead of a magic number here.
    id = 1;
    cache_ready = false;

    printf("Initiating Accelerator!\n");
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
#if 0
    connDef = machine.get_new_connection_def("p2p",
            "p2p_L2_0_L1_D_0", 1);


    stringbuf L2_0;
    L2_0 << "L2_0";
    machine.add_new_connection(connDef, L2_0.buf, INTERCONN_TYPE_UPPER2);

    stringbuf L1_D_0;
    L1_D_0 << "L1_D_0";
    machine.add_new_connection(connDef, L1_D_0.buf, INTERCONN_TYPE_LOWER);
#endif

    machine.setup_interconnects();
    machine.memoryHierarchyPtr->setup_full_flags();

    printf("Accelerator Initialization finished!\n");
}

// Currently it only tests loading in the memory Hierarchy.
bool Accelerator::runcycle(void *nothing)
{
    W64 data;
    int rc;

    if (cache_ready && temp_need_load) {
        rc = load(temp_virt_addr, temp_phys_addr, data, temp_rip, temp_uuid, true);
        if (rc == ACCESS_OK) {
            printf("Loading data succeeded! data=%llu\n", data);
            temp_need_load = false;
        }
    }
    return true;
}

int Accelerator::load(W64 virt_addr, W64 phys_addr, W64& data, W64 rip, W64 uuid, bool is_requested)
{
    bool hit;

    Memory::MemoryRequest *request = memoryHierarchy->get_free_request(id);
    //printf("id = %d\n", id);
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

    printf("Accelerator Cache Hit!\n");
    // On cache hit, retrieve data from the memory location.
    // TODO: use PHYSICAL address here.
    data = ctx->loadvirt(virt_addr, 3); // sizeshift=3 for 64bit-data

    return ACCESS_OK;
}

// Handle data path when a load request finishes.
bool Accelerator::load_cb(void *arg)
{
    //Memory::MemoryRequest* req = (Memory::MemoryRequest*)arg;
    // TODO: Set the flag correlate to the requested memory
    printf("Inside Accelerator dcache callback.\n");
    cache_ready = true;


    return true;
}

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

W64 Accelerator::exec(AcceleratorArg& arg)
{
    // XXX Testing with a constant delay here.
    int delay = 1000000;
    marss_add_event(core_wakeup_signal, delay, NULL);

    // The following are commands to be executed.
    printf("Caught an accelerator exec!\n");
    printf("arg = %llu\n", arg.virt_addr);
    W64 data;
    temp_virt_addr = arg.virt_addr;
    temp_phys_addr = arg.phys_addr;
    temp_rip = arg.rip;
    temp_uuid = arg.uuid;

    printf("virt_addr = %llu\n", arg.virt_addr);
    printf("phys_addr = %llu\n", arg.phys_addr);

    int rc = load(arg.virt_addr, arg.phys_addr, data, arg.rip, arg.uuid, false);
    printf("Load finished!\n");
    if (rc == ACCESS_OK) {
        printf("Loaded data = %llu\n", data);
        return data;
    } else if (rc == ACCESS_CACHE_MISS) {
        temp_need_load = true;
        printf("Memory load encounters a cache miss!\n");

        return arg.virt_addr;
    } else {
        printf("Memory load failed!\n");
        return arg.virt_addr;
    }
}

