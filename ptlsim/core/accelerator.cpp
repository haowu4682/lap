#include <globals.h>
#include <accelerator.h>

using namespace Core;

Accelerator::Accelerator(BaseMachine& machine, const char* name)
    : Statable(name, &machine)
      , machine(machine)
{
}

