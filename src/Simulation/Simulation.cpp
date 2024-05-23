#include "Simulation/Simulation.hpp"
#include "Tools/Signal_handler/Signal_handler.hpp"

using namespace aff3ct;
using namespace aff3ct::simulation;

Simulation
::Simulation()
: simu_error(false)
{
	tools::Signal_handler::init();
}

bool Simulation
::is_error() const
{
	return this->simu_error;
}
