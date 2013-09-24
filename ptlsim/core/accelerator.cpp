#include <globals.h>
#include <accelerator.h>

using namespace Core;

Accelerator::Accelerator(BaseMachine& machine, const char* name)
    : Statable(name, &machine)
      , machine(machine)
{
}

int Accelerator::exec(int arg)
{
    printf("Caught an accelerator exec!\n");
    printf("arg + 10 = %d\n", arg + 10);
}

