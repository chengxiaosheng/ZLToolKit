#include "NormalResolver.h"

#include <Thread/WorkThreadPool.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <strings.h>  // memset
#endif

using namespace trantor;

std::shared_ptr<Resolver> Resolver::newResolver(
    const std::shared_ptr<toolkit::EventPoller> &,
    size_t timeout)
{
    return std::make_shared<NormalResolver>(timeout);
}

bool Resolver::isCAresUsed()
{
    return false;
}

void NormalResolver::resolve(const std::string &hostname,
                             const Callback &callback)
{
    toolkit::WorkThreadPool::Instance().getExecutor()->async(
        [hostname, callback, timeout = timeout_]() {
            // 阻塞式dns解析放在后台线程执行  [AUTO-TRANSLATED:e54694ea]
            // Blocking DNS resolution is executed in the background thread
            struct sockaddr_storage addr;
            if (toolkit::SockUtil::getDomainIP(hostname.data(),
                                               0,
                                               addr,
                                               AF_INET,
                                               SOCK_STREAM,
                                               IPPROTO_TCP,
                                               timeout))
            {
                if (addr.ss_family == AF_INET)
                {
                    callback(InetAddress(*(sockaddr_in *)&addr));
                }
                else if (addr.ss_family == AF_INET6)
                {
                    callback(InetAddress(*(sockaddr_in6 *)&addr));
                }
                else
                {
                    callback({});
                }
            }
            else
            {
                callback({});
            }
    });
}