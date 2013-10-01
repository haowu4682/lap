
#ifndef ACCELERATOR_H
#define ACCELERATOR_H

#include <ptlsim.h>
#include <ptl-qemu.h>
#include <machine.h>
#include <statsBuilder.h>
#include <memoryHierarchy.h>

namespace Core {
    enum {
        ACCESS_OK = 0,
        ACCESS_CACHE_MISS,
        ACCESS_FAIL,

        NUM_ACCESS_RESULTS
    };

    struct AcceleratorArg {
        W64 addr;
        W64 rip;
        W64 uuid;
    };

    struct Accelerator : public Statable {
        Accelerator(BaseMachine& machine, const char* name);
        virtual ~Accelerator() {}

        // TODO Pure Abstract function
        virtual void init();
        virtual bool runcycle(void*);
        virtual void update_memory_hierarchy_ptr();
        virtual W64 exec(AcceleratorArg &arg);
        virtual int load(W64 addr, W64& data, W64 rip, W64 uuid);
        virtual int load_blocked(W64 addr, W64& data);
        virtual bool load_cb(void *arg);

        int id;
        const char * name;

        BaseMachine& machine;
        Memory::MemoryHierarchy* memoryHierarchy;
        Context* ctx;
        Signal run_cycle;

        /* Cache Access */
        Signal dcache_signal;

        /* Cache Miss Ready */
        bool cache_ready;
    };
}

#endif // ACCELERATOR_H
