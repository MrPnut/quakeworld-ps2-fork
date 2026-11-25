#include "ps2/common.h"
#include "ps2/system/iop_boot.h"
#include "ps2/system/sys.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <kernel.h>

extern "C" {
    #include <netman.h>
    #include <ps2ip.h>
    #include <fcntl.h>
    #include "quake/quakedef.h"
}

namespace {
    constexpr int kMaxLoopPackets = 16;
    constexpr int kLoopMask       = kMaxLoopPackets - 1;

    struct LoopMsg
    {
        byte data[MAX_MSGLEN];
        int  dataLen;
    };

    struct LoopbackQueue
    {
        LoopMsg msgs[kMaxLoopPackets];
        int     get;
        int     send;
    };

    static LoopbackQueue s_loopback;

    void Loopback_Clear()
    {
        s_loopback.get  = 0;
        s_loopback.send = 0;
    }

    bool Loopback_GetPacket(sizebuf_t * msg, netadr_t * from)
    {
        if (s_loopback.send - s_loopback.get > kMaxLoopPackets)
        {
            s_loopback.get = s_loopback.send - kMaxLoopPackets;
        }

        if (s_loopback.get >= s_loopback.send)
        {
            return false;
        }

        const int i = s_loopback.get & kLoopMask;
        ++s_loopback.get;

        const int len = s_loopback.msgs[i].dataLen;
        if (len > msg->maxsize)
        {
            Con_Printf("Loopback_GetPacket: oversized packet (%d > %d)\n", len, msg->maxsize);

            return false;
        }

        std::memcpy(msg->data, s_loopback.msgs[i].data, static_cast<size_t>(len));
        msg->cursize = len;

        std::memset(from, 0, sizeof(*from));
        from->ip.b[0] = 127;
        from->ip.b[1] = 0;
        from->ip.b[2] = 0;
        from->ip.b[3] = 1;
        from->port    = net_local_adr.port;

        return true;
    }

    void Loopback_SendPacket(int length, const void * data)
    {
        if (length <= 0 || length > MAX_MSGLEN)
        {
            Con_Printf("Loopback_SendPacket: invalid packet length %d\n", length);

            return;
        }

        const int i = s_loopback.send & kLoopMask;
        ++s_loopback.send;

        std::memcpy(s_loopback.msgs[i].data, data, static_cast<size_t>(length));
        s_loopback.msgs[i].dataLen = length;
    }

    static bool s_netmanInitialized = false;
    static bool s_ps2ipInitialized  = false;
    static int  s_udpSocket         = -1;

    static cvar_t net_dhcp    = { "net_dhcp",    "1",             CVAR_NONE, 0.0f, nullptr, nullptr, nullptr };
    static cvar_t net_ip      = { "net_ip",      "10.0.2.15",     CVAR_NONE, 0.0f, nullptr, nullptr, nullptr };
    static cvar_t net_netmask = { "net_netmask", "255.255.255.0", CVAR_NONE, 0.0f, nullptr, nullptr, nullptr };
    static cvar_t net_gateway = { "net_gateway", "10.0.2.2",      CVAR_NONE, 0.0f, nullptr, nullptr, nullptr };

    bool IsLoopbackIp(const netadr_t & adr)
    {
        return (adr.ip.b[0] == 127);
    }

    void SockadrToNetadr(const struct sockaddr_in & s, netadr_t * a)
    {
        std::memset(a, 0, sizeof(*a));
        a->ip.l = s.sin_addr.s_addr;
        a->port = s.sin_port;
    }

    void NetadrToSockadr(const netadr_t & a, struct sockaddr_in * s)
    {
        std::memset(s, 0, sizeof(*s));
        s->sin_len    = sizeof(*s);
        s->sin_family = AF_INET;
        s->sin_port   = a.port;

        if (a.ip.l == 0xFFFFFFFFu)
        {
            s->sin_addr.s_addr = INADDR_BROADCAST;
        }
        else
        {
            s->sin_addr.s_addr = a.ip.l;
        }
    }

    bool WaitForEthernetLink(int timeoutMsec)
    {
        const int deadline = ps2::sys::Milliseconds() + timeoutMsec;
        while (ps2::sys::Milliseconds() < deadline)
        {
            if (NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, nullptr, 0, nullptr, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP)
            {
                return true;
            }
            Sys_Sleep(50);
        }

        return false;
    }

    bool ConfigureIpInterface(bool useDhcp)
    {
        if (!WaitForEthernetLink(10000))
        {
            Con_Printf("NET_Init: Ethernet link down (loopback only)\n");

            return false;
        }

        ip4_addr_t ip      = {};
        ip4_addr_t netmask = {};
        ip4_addr_t gateway = {};

        if (useDhcp)
        {
            ip4_addr_set_zero(&ip);
            ip4_addr_set_zero(&netmask);
            ip4_addr_set_zero(&gateway);
        }
        else
        {
            const u32 parsedIp      = inet_addr(net_ip.string);
            const u32 parsedNetmask = inet_addr(net_netmask.string);
            const u32 parsedGateway = inet_addr(net_gateway.string);
            if (parsedIp == INADDR_NONE || parsedNetmask == INADDR_NONE || parsedGateway == INADDR_NONE)
            {
                Con_Printf("NET_Init: invalid static IP cvars; falling back to 10.0.2.15\n");
                IP4_ADDR(&ip, 10, 0, 2, 15);
                IP4_ADDR(&netmask, 255, 255, 255, 0);
                IP4_ADDR(&gateway, 10, 0, 2, 2);
            }
            else
            {
                ip.addr      = parsedIp;
                netmask.addr = parsedNetmask;
                gateway.addr = parsedGateway;
            }
        }

        if (!s_ps2ipInitialized)
        {
            if (ps2ipInit(&ip, &netmask, &gateway) < 0)
            {
                Con_Printf("NET_Init: ps2ipInit failed\n");

                return false;
            }
            s_ps2ipInitialized = true;
        }

        char ifName[] = "sm0";
        t_ip_info ipInfo = {};
        if (ps2ip_getconfig(ifName, &ipInfo) < 0)
        {
            Con_Printf("NET_Init: ps2ip_getconfig(sm0) failed\n");

            return false;
        }

        const u32 wantDhcp = useDhcp ? 1u : 0u;
        if (wantDhcp != ipInfo.dhcp_enabled ||
            (!useDhcp &&
             (ip.addr != ipInfo.ipaddr.s_addr ||
              netmask.addr != ipInfo.netmask.s_addr ||
              gateway.addr != ipInfo.gw.s_addr)))
        {
            if (useDhcp)
            {
                ipInfo.dhcp_enabled = 1;
            }
            else
            {
                ipInfo.dhcp_enabled   = 0;
                ipInfo.ipaddr.s_addr  = ip.addr;
                ipInfo.netmask.s_addr = netmask.addr;
                ipInfo.gw.s_addr      = gateway.addr;
            }
            ps2ip_setconfig(&ipInfo);
        }

        if (useDhcp)
        {
            Con_Printf("NET_Init: waiting for DHCP lease...\n");
            const int deadline = ps2::sys::Milliseconds() + 10000;
            while (ps2::sys::Milliseconds() < deadline)
            {
                if (ps2ip_getconfig(ifName, &ipInfo) >= 0 &&
                    ipInfo.dhcp_enabled &&
                    (ipInfo.dhcp_status == DHCP_STATE_BOUND || ipInfo.dhcp_status == DHCP_STATE_OFF) &&
                    ipInfo.ipaddr.s_addr != 0)
                {
                    net_local_adr.ip.l = ipInfo.ipaddr.s_addr;

                    return true;
                }
                Sys_Sleep(100);
            }

            Con_Printf("NET_Init: DHCP timed out (status=%u, ip=%s); falling back to static IP (%s)\n",
                static_cast<unsigned>(ipInfo.dhcp_status),
                inet_ntoa(ipInfo.ipaddr),
                net_ip.string);

            ipInfo.dhcp_enabled = 0;
            const in_addr_t fallbackIp      = inet_addr(net_ip.string);
            const in_addr_t fallbackNetmask = inet_addr(net_netmask.string);
            const in_addr_t fallbackGw      = inet_addr(net_gateway.string);
            if (fallbackIp != INADDR_NONE && fallbackNetmask != INADDR_NONE && fallbackGw != INADDR_NONE)
            {
                ipInfo.ipaddr.s_addr  = fallbackIp;
                ipInfo.netmask.s_addr = fallbackNetmask;
                ipInfo.gw.s_addr      = fallbackGw;
            }
            ps2ip_setconfig(&ipInfo);
        }

        if (ps2ip_getconfig(ifName, &ipInfo) >= 0)
        {
            net_local_adr.ip.l = ipInfo.ipaddr.s_addr;
        }

        return true;
    }

    int OpenUdpSocket(int port)
    {
        const int sock = lwip_socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0)
        {
            Con_Printf("NET_Init: socket() failed\n");

            return -1;
        }

        if (lwip_fcntl(sock, F_SETFL, O_NONBLOCK) < 0)
        {
            Con_Printf("NET_Init: fcntl(O_NONBLOCK) failed\n");
            lwip_close(sock);

            return -1;
        }

        int broadcast = 1;
        lwip_setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

        struct sockaddr_in addr = {};
        addr.sin_len         = sizeof(addr);
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = (port == PORT_ANY) ? 0 : htons(static_cast<u16>(port));

        if (lwip_bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
        {
            Con_Printf("NET_Init: bind(port %d) failed\n", port);
            lwip_close(sock);

            return -1;
        }

        socklen_t addrLen = sizeof(addr);
        if (lwip_getsockname(sock, reinterpret_cast<struct sockaddr *>(&addr), &addrLen) == 0)
        {
            net_local_adr.port = addr.sin_port;
        }
        else
        {
            net_local_adr.port = (port == PORT_ANY) ? 0 : htons(static_cast<u16>(port));
        }

        return sock;
    }
}

extern "C" {
    netadr_t  net_local_adr;
    netadr_t  net_from;
    sizebuf_t net_message;

    alignas(16) static byte net_message_buffer[MAX_MSGLEN];

    void NET_Init(int port)
    {
        Cvar_RegisterVariable(&net_dhcp);
        Cvar_RegisterVariable(&net_ip);
        Cvar_RegisterVariable(&net_netmask);
        Cvar_RegisterVariable(&net_gateway);

        net_message.maxsize = sizeof(net_message_buffer);
        net_message.data    = net_message_buffer;
        net_message.cursize = 0;

        Loopback_Clear();

        std::memset(&net_local_adr, 0, sizeof(net_local_adr));
        net_local_adr.ip.b[0] = 127;
        net_local_adr.ip.b[1] = 0;
        net_local_adr.ip.b[2] = 0;
        net_local_adr.ip.b[3] = 1;
        net_local_adr.port    = (port == PORT_ANY) ? 0 : htons(static_cast<u16>(port));

        if (!ps2::sys::StartNetworkDriver())
        {
            Con_Printf("NET_Init: DEV9/NETMAN/SMAP IOP drivers unavailable (loopback only)\n");

            return;
        }

         if (!s_netmanInitialized)
        {
            if (NetManInit() < 0)
            {
                Con_Printf("NET_Init: NetManInit failed (loopback only)\n");
                
                return;
            }
            NetManSetLinkMode(NETMAN_NETIF_ETH_LINK_MODE_AUTO);
            s_netmanInitialized = true;
        }

        if (!ConfigureIpInterface(net_dhcp.value != 0.0f))
        {
            return;
        }

        s_udpSocket = OpenUdpSocket(port);
        if (s_udpSocket < 0)
        {
            return;
        }

        Con_Printf("UDP Initialized (%s)\n", NET_AdrToString(net_local_adr));
    }

    void NET_Shutdown(void)
    {
        if (s_udpSocket >= 0)
        {
            lwip_close(s_udpSocket);
            s_udpSocket = -1;
        }

        if (s_ps2ipInitialized)
        {
            ps2ipDeinit();
            s_ps2ipInitialized = false;
        }

        if (s_netmanInitialized)
        {
            NetManDeinit();
            s_netmanInitialized = false;
        }

        Loopback_Clear();
    }

    qboolean NET_GetPacket(void)
    {
        if (Loopback_GetPacket(&net_message, &net_from))
        {
            return true;
        }

        if (s_udpSocket < 0)
        {
            return false;
        }

        struct sockaddr_in fromAddr = {};
        socklen_t fromLen = sizeof(fromAddr);
        const int ret = lwip_recvfrom(
            s_udpSocket,
            net_message_buffer,
            sizeof(net_message_buffer),
            MSG_DONTWAIT,
            reinterpret_cast<struct sockaddr *>(&fromAddr),
            &fromLen
        );

        if (ret < 0)
        {
            if (errno != EWOULDBLOCK)
            {
                Con_Printf("NET_GetPacket: recvfrom error ret=%d errno=%d\n", ret, errno);
            }

            return false;
        }

        if (ret == 0)
        {
            return false;
        }

        if (ret > net_message.maxsize)
        {
            Con_Printf("NET_GetPacket: oversized packet (%d > %d) from %s\n",
                ret, net_message.maxsize, inet_ntoa(fromAddr.sin_addr));
            
                return false;
        }

        SockadrToNetadr(fromAddr, &net_from);
        net_message.cursize = ret;
        
        return true;
    }

    void NET_SendPacket(int length, void * data, netadr_t to)
    {
        if (IsLoopbackIp(to))
        {
            Loopback_SendPacket(length, data);
            
            return;
        }

        if (s_udpSocket < 0)
        {
            return;
        }

        struct sockaddr_in destAddr = {};
        NetadrToSockadr(to, &destAddr);
        const int ret = lwip_sendto(
            s_udpSocket,
            data,
            static_cast<size_t>(length),
            0,
            reinterpret_cast<const struct sockaddr *>(&destAddr),
            sizeof(destAddr)
        );
        if (ret < 0)
        {
            Con_Printf("NET_SendPacket: sendto(%s) failed\n", NET_AdrToString(to));
        }
    }

    qboolean NET_CompareAdr(netadr_t a, netadr_t b)
    {
        return (a.ip.l == b.ip.l && a.port == b.port);
    }
    qboolean NET_CompareBaseAdr(netadr_t a, netadr_t b)
    {
        return (a.ip.l == b.ip.l);
    }
    qboolean NET_IsLocalAddress(netadr_t adr)
    {
        return (adr.ip.l == net_local_adr.ip.l) || IsLoopbackIp(adr);
    }

    const char * NET_AdrToString(netadr_t a)
    {
        enum { ADR_STR_LEN = 64 };
        static char s[ADR_STR_LEN];
        const byte * b = a.ip.b;
        const unsigned short port = ntohs(a.port);
        q_snprintf(s, sizeof(s), "%u.%u.%u.%u:%u",
            static_cast<unsigned>(b[0]),
            static_cast<unsigned>(b[1]),
            static_cast<unsigned>(b[2]),
            static_cast<unsigned>(b[3]),
            static_cast<unsigned>(port));
        
        return s;
    }

    const char * NET_BaseAdrToString(netadr_t a)
    {
        enum { ADR_STR_LEN = 64 };
        static char s[ADR_STR_LEN];
        const byte * b = a.ip.b;
        q_snprintf(s, sizeof(s), "%u.%u.%u.%u",
            static_cast<unsigned>(b[0]),
            static_cast<unsigned>(b[1]),
            static_cast<unsigned>(b[2]),
            static_cast<unsigned>(b[3]));
        
        return s;
    }

    qboolean NET_StringToAdr(const char * s, netadr_t * a)
    {
        if (s == nullptr || a == nullptr || s[0] == '\0')
        {
            return false;
        }
        std::memset(a, 0, sizeof(*a));
        if (!Q_strcmp(s, "local") || !Q_strcmp(s, "localhost") || !Q_strcmp(s, "loopback"))
        {
            a->ip.b[0] = 127;
            a->ip.b[1] = 0;
            a->ip.b[2] = 0;
            a->ip.b[3] = 1;
            a->port    = net_local_adr.port;
        
            return true;
        }
        
        char copy[128];
        q_strlcpy(copy, s, sizeof(copy));
        u16 port = 0;
        char * colon = std::strchr(copy, ':');
        if (colon != nullptr)
        {
            *colon = '\0';
            const long parsedPort = std::strtol(colon + 1, nullptr, 10);

            if (parsedPort > 0 && parsedPort <= 65535)
            {
                port = htons(static_cast<u16>(parsedPort));
            }

        }

        const u32 parsedIp = inet_addr(copy);
        if (parsedIp == INADDR_NONE && Q_strcmp(copy, "255.255.255.255") != 0)
        {
            return false;
        }

        a->ip.l = parsedIp;
        a->port = port;

        return true;
    }

    void NET_Sleep(int msec)
    {
        if (s_udpSocket < 0 || msec <= 0)
        {
            return;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s_udpSocket, &readSet);
        struct timeval tv = {};
        tv.tv_sec  = msec / 1000;
        tv.tv_usec = (msec % 1000) * 1000;
        lwip_select(s_udpSocket + 1, &readSet, nullptr, nullptr, &tv);
    }
}
