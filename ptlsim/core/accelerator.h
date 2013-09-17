
#ifndef ACCELERATOR_H
#define ACCELERATOR_H

#include <ptlsim.h>
#include <ptl-qemu.h>
#include <machine.h>
#include <statsBuilder.h>
#include <memoryHierarchy.h>

namespace Core {
    struct Accelerator : public Statable {
        Accelerator(BaseMachine& machine, const char* name);
        virtual ~Accelerator() {}

        // TODO Pure Abstract function
        virtual int exec() {
            printf("Inside Accelerator");
            return 0;
        }

        BaseMachine& machine;
        Memory::MemoryHierarchy* memoryHierarchy;
    };
}

#endif // ACCELERATOR_H
