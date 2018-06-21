/*
	This file is part of cpp-ethereum.
	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/// @file
/// UPnP port forwarding support.

#include "nat.h"
// #include "libUtils/Logger.h"
#include <arpa/inet.h> //INET6_ADDRSTRLEN
#include <cstring>
#include <iostream>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>

using namespace std;

namespace
{
    const int DISCOVERY_TIME_OUT = 2000;
    const int TTL  = 2;   
}

NAT::NAT()
{
    m_urls = make_shared<UPNPUrls>();
    m_data = make_shared<IGDdatas>();

    memset(m_urls.get(), 0, sizeof(struct UPNPUrls));
    memset(m_data.get(), 0, sizeof(struct IGDdatas));

    m_initialized = false;

    shared_ptr<struct UPNPDev> devlist;
    int error = 0;
    
#if MINIUPNPC_API_VERSION >= 14
        devlist.reset(upnpDiscover(DISCOVERY_TIME_OUT, NULL/*multicast interface*/, NULL/*minissdpd socket path*/, 0/*sameport*/, 0/*ipv6*/, TTL, &error));
#else
        devlist.reset(upnpDiscover(DISCOVERY_TIME_OUT, NULL/*multicast interface*/, NULL/*minissdpd socket path*/, 0/*sameport*/, 0/*ipv6*/, &error));
#endif

    if (devlist.get() == NULL || error != 0)
    {
        return;
    }

    char lan_address[INET6_ADDRSTRLEN];

    int status = UPNP_GetValidIGD(devlist.get(), m_urls.get(), m_data.get(),
                                  lan_address, sizeof(lan_address));

    if (status != 1)
    {
        return;
    }

    m_lanAddress = lan_address;
    m_initialized = true;
}

NAT::~NAT()
{
    auto r = m_reg;
    for (auto i : r)
    {
        removeRedirect(i);
    }
}

string NAT::externalIP()
{
    if (!m_initialized)
    {
        return "0.0.0.0";
    }

    char addr[16];

    if (!UPNP_GetExternalIPAddress(m_urls->controlURL,
                                   m_data->first.servicetype, addr))
    {
        return addr;
    }
    else
    {
        return "0.0.0.0";
    }
}

int NAT::addRedirect(int _port)
{
    if (!m_initialized)
    {
        return -1;
    }

    if (_port < 0 && _port > 65535)
    {
        return -1;
    }

	// 1) Try direct mapping first (port external, port internal).
    string port_str = to_string(_port);

    int error = UPNP_AddPortMapping(
        m_urls->controlURL, m_data->first.servicetype, port_str.c_str(), port_str.c_str(),
        m_lanAddress.c_str(), "zilliqa", "TCP", NULL, NULL);

    if (!error)
    {
        m_reg.insert(_port);
        return _port;
    }

    // 2) Failed - now try (random external, port internal) and cycle up to 10 times.
	srand(static_cast<unsigned int>(time(nullptr)));
	for (unsigned i = 0; i < 10; ++i)
	{
		_port = rand() % (32768 - 1024) + 1024;
		string ext_port_str = to_string(_port);
		if (!UPNP_AddPortMapping(m_urls->controlURL, m_data->first.servicetype, ext_port_str.c_str(), port_str.c_str(), m_lanAddress.c_str(), "ethereum", "TCP", NULL, NULL))
        {
			return _port;
        }
	}

	// 3) Failed. Try asking the router to give us a free external port.
    // This may not works on some routers.
    if (UPNP_AddPortMapping(m_urls->controlURL, m_data->first.servicetype,
                            port_str.c_str(), NULL, m_lanAddress.c_str(), "zilliqa",
                            "TCP", NULL, NULL))
    {
        return 0;
    }

    unsigned int num = 0;

	// We got mapped, but we don't know which ports we got mapped to. Now to find...
    UPNP_GetPortMappingNumberOfEntries(m_urls->controlURL,
                                       m_data->first.servicetype, &num);
    for (unsigned int i = 0; i < num; ++i)
    {
        char extPort[16];
        char intClient[16];
        char intPort[6];
        char protocol[4];
        char desc[80];
        char enabled[4];
        char rHost[64];
        char duration[16];
        UPNP_GetGenericPortMappingEntry(
            m_urls->controlURL, m_data->first.servicetype, to_string(i).c_str(),
            extPort, intClient, intPort, protocol, desc, enabled, rHost,
            duration);
        if (string("zilliqa") == desc)
        {
            m_reg.insert(atoi(extPort));
            return atoi(extPort);
        }
    }

    return -1;
}

void NAT::removeRedirect(int _port)
{
    if (!m_initialized)
    {
        return;
    }
    
    if (_port < 0 && _port > 65535)
    {
        return;
    }

    string port_str = to_string(_port);
    UPNP_DeletePortMapping(m_urls->controlURL, m_data->first.servicetype,
                           port_str.c_str(), "TCP", NULL);
    m_reg.erase(_port);
}
