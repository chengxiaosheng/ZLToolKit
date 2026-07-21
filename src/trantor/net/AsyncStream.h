/**
 *
 *  @file AsyncStream.h
 *  @author An Tao
 *
 *  Public header file in trantor lib.
 *
 *  Copyright 2023, An Tao.  All rights reserved.
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the License file.
 *
 *
 */

#pragma once

#include <Util/util.h>
#include <Network/Buffer.h>
#include <memory>

namespace trantor
{
/**
 * @brief This class represents a data stream that can be sent asynchronously.
 * The data is sent in chunks, and the chunks are sent in order, and all the
 * chunks are sent continuously.
 */
class ZLTOOLKIT_EXPORT AsyncStream : public toolkit::noncopyable
{
  public:
    virtual ~AsyncStream() = default;

    virtual bool send(const std::shared_ptr<toolkit::Buffer> &buffer) = 0;

    bool send(const char *data, size_t len) {
        std::shared_ptr<toolkit::BufferRaw> buffer = nullptr;
        if (data && len) {
            buffer = toolkit::BufferRaw::create(len);
            buffer->assign(data, len);
        }
        return send(buffer);
    }


    bool send(const std::string &data)
    {
        return send(data.data(), data.length());
    }
    /**
     * @brief Terminate the stream.
     */
    virtual void close() = 0;
};
using AsyncStreamPtr = std::unique_ptr<AsyncStream>;
}  // namespace trantor