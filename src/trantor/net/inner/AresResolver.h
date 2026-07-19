// Copyright 2016, Tao An.  All rights reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Tao An

#pragma once
#include <trantor/net/Resolver.h>
#include <Util/util.h>
#include <map>
#include <memory>
#include <string.h>

extern "C"
{
    struct ares_addrinfo;
    struct ares_channeldata;
    struct ares_addrinfo_hints;
    using ares_channel = struct ares_channeldata*;
}
namespace trantor
{
class Channel;
class AresResolver : public Resolver,
                     public toolkit::noncopyable,
                     public std::enable_shared_from_this<AresResolver>
{
  public:
    AresResolver(const std::shared_ptr<toolkit::EventPoller>& loop, size_t timeout);
    ~AresResolver();

    virtual void resolve(const std::string& hostname,
                         const Callback& cb) override
    {
        bool cached = false;
        InetAddress inet;
        {
            std::lock_guard<std::mutex> lock(globalMutex());
            auto iter = globalCache().find(hostname);
            if (iter != globalCache().end())
            {
                auto& cachedAddr = iter->second;
                if (timeout_ == 0 ||
                    cachedAddr.second.after(timeout_) > trantor::Date::date())
                {
                    inet = (*cachedAddr.first)[0];
                    cached = true;
                }
            }
        }
        if (cached)
        {
            cb(inet);
            return;
        }
        if (loop_->isCurrentThread())
        {
            resolveInLoop(hostname,
                          [cb](const std::vector<trantor::InetAddress>& inets) {
                              cb(inets[0]);
                          });
        }
        else
        {
            loop_->async([thisPtr = shared_from_this(), hostname, cb]() {
                thisPtr->resolveInLoop(
                    hostname,
                    [cb](const std::vector<trantor::InetAddress>& inets) {
                        cb(inets[0]);
                    });
            });
        }
    }

    virtual void resolve(const std::string& hostname,
                         const ResolverResultsCallback& cb) override
    {
        std::shared_ptr<std::vector<trantor::InetAddress>> inets_ptr{nullptr};
        {
            std::lock_guard<std::mutex> lock(globalMutex());
            auto iter = globalCache().find(hostname);
            if (iter != globalCache().end())
            {
                auto& cachedAddr = iter->second;
                if (timeout_ == 0 ||
                    cachedAddr.second.after(timeout_) > trantor::Date::date())
                {
                    inets_ptr = cachedAddr.first;
                }
            }
        }
        if (inets_ptr)
        {
            cb(*inets_ptr);
            return;
        }
        if (loop_->isCurrentThread())
        {
            resolveInLoop(hostname, cb);
        }
        else
        {
            loop_->async([thisPtr = shared_from_this(), hostname, cb]() {
                thisPtr->resolveInLoop(hostname, cb);
            });
        }
    }

  private:
    struct QueryData
    {
        AresResolver* owner_;
        ResolverResultsCallback callback_;
        std::string hostname_;
        QueryData(AresResolver* o,
                  const ResolverResultsCallback& cb,
                  const std::string& hostname)
            : owner_(o), callback_(cb), hostname_(hostname)
        {
        }
    };
    void resolveInLoop(const std::string& hostname,
                       const ResolverResultsCallback& cb);
    void init();
    std::shared_ptr<toolkit::EventPoller> loop_;
    std::shared_ptr<bool> loopValid_;
    ares_channel ctx_{nullptr};
    bool timerActive_{false};
    using ChannelList = std::map<int, std::unique_ptr<trantor::Channel>>;
    ChannelList channels_;
    static std::unordered_map<
        std::string,
        std::pair<std::shared_ptr<std::vector<trantor::InetAddress>>,
                  trantor::Date>>&
    globalCache()
    {
        static std::unordered_map<
            std::string,
            std::pair<std::shared_ptr<std::vector<trantor::InetAddress>>,
                      trantor::Date>>
            dnsCache;
        return dnsCache;
    }
    static std::mutex& globalMutex()
    {
        static std::mutex mutex_;
        return mutex_;
    }
    const size_t timeout_{60};

    void onRead(int sockfd);
    void onTimer();
    void onQueryResult(int status,
                       struct ares_addrinfo* result,
                       const std::string& hostname,
                       const ResolverResultsCallback& callback);
    void onSockCreate(int sockfd, int type);
    void onSockStateChange(int sockfd, bool read, bool write);

    static void ares_hostcallback_(void* data,
                                   int status,
                                   int timeouts,
                                   struct ares_addrinfo* hostent);
#ifdef _WIN32
    static int ares_sock_createcallback_(SOCKET sockfd, int type, void* data);
#else
    static int ares_sock_createcallback_(int sockfd, int type, void* data);
#endif
    static void ares_sock_statecallback_(void* data,
#ifdef _WIN32
                                         SOCKET sockfd,
#else
                                         int sockfd,
#endif
                                         int read,
                                         int write);
    struct LibraryInitializer
    {
        LibraryInitializer();
        ~LibraryInitializer();
        ares_addrinfo_hints* hints_;
    };
    static LibraryInitializer libraryInitializer_;
};
}  // namespace trantor
