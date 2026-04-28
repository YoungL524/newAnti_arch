#include "Endpoint.h"

Endpoint::Endpoint(const EndpointConfig& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
{
}
