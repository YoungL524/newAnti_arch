#include "BusEnvelope.h"

thread_local const BusEnvelope* BusEnvelope::s_dispatching = nullptr;

void BusEnvelope::setDispatchContext(const BusEnvelope* env)
{
    s_dispatching = env;
}

void BusEnvelope::clearDispatchContext()
{
    s_dispatching = nullptr;
}
