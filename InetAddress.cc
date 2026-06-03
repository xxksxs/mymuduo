#include "InetAddress.h"
#include <string.h>
#include <strings.h>

InetAddress::InetAddress(uint16_t port, std::string ip)
{
    bzero(&addr_, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr);
}

std::string InetAddress::toIp() const
{
    char buf[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
    return buf;
}

std::string InetAddress::toIpPort() const
{
    char buf[INET_ADDRSTRLEN + 6] = {0};
    inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
    size_t end = strlen(buf);
    uint16_t port = ntohs(addr_.sin_port);
    snprintf(buf + end, sizeof(buf) - end, ":%u", port);
    return buf;
}

std::uint16_t InetAddress::toPort() const
{
    return ntohs(addr_.sin_port);
}

// #include <iostream>
// int main()
// {
//     InetAddress addr(8080, "127.0.0.1");    
//     std::cout << "IP: " << addr.toIp() << std::endl;
//     std::cout << "IP:Port: " << addr.toIpPort() << std::endl;
//     std::cout << "Port: " << addr.toPort() << std::endl;
//     return 0;
// }