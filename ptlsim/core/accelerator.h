
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
        W64 virt_addr;
        W64 phys_addr;
        W64 rip;
        W64 uuid;
    };

    struct Accelerator : public Statable {
        Accelerator(BaseMachine& machine, const char* name);
        virtual ~Accelerator() {}

        // TODO Pure Abstract function
        virtual void init();
        virtual bool runcycle(void*);

        virtual bool do_idle(void*);
        virtual bool do_load_header(void*);
        virtual bool do_load_content(void*);
        virtual bool do_load_content_row_major(void*);
        virtual bool do_load_content_column_major(void*);
        virtual bool do_load_content_block_major(void*);
        virtual bool do_store(void*);
        virtual bool do_calculate(void*);
        virtual void send_interrupt();

        virtual void update_memory_hierarchy_ptr();
        virtual W64 exec(AcceleratorArg &arg);

        virtual int load(W64 virt_addr, W64 phys_addr, W64& data, W64 rip, W64 uuid, bool is_requested, int sizeshift = 3);
        virtual int load_buf(W64 virt_addr, W64 phys_addr, void *data, size_t size, W64 rip, W64 uuid, bool is_requested);
        virtual int store(W64 virt_addr, W64 phys_addr, W64& data, W64 rip, W64 uuid, bool is_requested, int sizeshift = 3);
        virtual int store(W64 virt_addr, W64 phys_addr, void *data, size_t size, W64 rip, W64 uuid, bool is_requested);

        //virtual int load_blocked(W64 addr, W64& data);
        virtual bool load_cb(void *arg);

        int id;
        const char * name;

        BaseMachine& machine;
        Memory::MemoryHierarchy* memoryHierarchy;
        Context* ctx;
        Signal run_cycle;
        /* Core wakeup signal */
        Signal *core_wakeup_signal;

        /* Cache Access */
        Signal dcache_signal;

        /* Cache Miss Ready */
        bool cache_ready;


        /* MMIO control register value */
        W64 rc0;

        /* QEMU IRQ value */
        qemu_irq lap_irq;
    };
}

#endif // ACCELERATOR_H
