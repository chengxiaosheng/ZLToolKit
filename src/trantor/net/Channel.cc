/**
 *
 *  Channel.cc
 *  An Tao
 *
 *  Public header file in trantor lib.
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the License file.
 *
 *
 */

#include "Channel.h"
#include <Poller/EventPoller.h>
#ifdef _WIN32
#include "Wepoll.h"
#define POLLIN EPOLLIN
#define POLLPRI EPOLLPRI
#define POLLOUT EPOLLOUT
#define POLLHUP EPOLLHUP
#define POLLNVAL 0
#define POLLERR EPOLLERR
#else
#include <poll.h>
#endif
#include <iostream>

namespace trantor
{
const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(const std::shared_ptr<toolkit::EventPoller> &loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), index_(-1), tied_(false)
{
}

Channel::~Channel()
{
    if (addedToLoop_)
    {
        loop_->delEvent(fd_);
        addedToLoop_ = false;
    }
}

int Channel::toPollerEvents(int pollEvents) const
{
    int poller = 0;
    if (pollEvents & (POLLIN | POLLPRI))
        poller |= toolkit::EventPoller::Event_Read;
    if (pollEvents & POLLOUT)
        poller |= toolkit::EventPoller::Event_Write;
    return poller;
}

int Channel::fromPollerEvents(int pollerEvents) const
{
    int poll = 0;
    if (pollerEvents & toolkit::EventPoller::Event_Read)
        poll |= POLLIN;
    if (pollerEvents & toolkit::EventPoller::Event_Write)
        poll |= POLLOUT;
    if (pollerEvents & toolkit::EventPoller::Event_Error)
        poll |= POLLERR | POLLHUP;
    return poll;
}

void Channel::onPollerEvent(int pollerEvents)
{
    revents_ = fromPollerEvents(pollerEvents);
    handleEvent();
}

void Channel::remove()
{
    assert(events_ == kNoneEvent);
    if (addedToLoop_)
    {
        loop_->delEvent(fd_);
        addedToLoop_ = false;
    }
}

void Channel::update()
{
    int pollerEvents = toPollerEvents(events_);
    if (!addedToLoop_)
    {
        if (pollerEvents != 0)
        {
            loop_->addEvent(fd_, pollerEvents,
                             [this](int events) { onPollerEvent(events); });
            addedToLoop_ = true;
        }
    }
    else
    {
        if (pollerEvents == 0)
        {
            loop_->delEvent(fd_);
            addedToLoop_ = false;
        }
        else
        {
            loop_->modifyEvent(fd_, pollerEvents);
        }
    }
}

void Channel::handleEvent()
{
    if (events_ == kNoneEvent)
        return;
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handleEventSafely();
        }
    }
    else
    {
        handleEventSafely();
    }
}

void Channel::handleEventSafely()
{
    if (eventCallback_)
    {
        eventCallback_();
        return;
    }
    if ((revents_ & POLLHUP) && !(revents_ & POLLIN))
    {
        if (closeCallback_)
            closeCallback_();
    }
    if (revents_ & (POLLNVAL | POLLERR))
    {
        if (errorCallback_)
            errorCallback_();
    }
#ifdef __linux__
    if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
#else
    if (revents_ & (POLLIN | POLLPRI))
#endif
    {
        if (readCallback_)
            readCallback_();
    }
#ifdef _WIN32
    if ((revents_ & POLLOUT) && !(revents_ & POLLHUP))
#else
    if (revents_ & POLLOUT)
#endif
    {
        if (writeCallback_)
            writeCallback_();
    }
}

}  // namespace trantor
