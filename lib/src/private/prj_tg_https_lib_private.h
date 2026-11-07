/*!
 * \file
 * \brief file prj_tg_https_lib_private.h
 *
 * library's private functionality
 *
 * Copyright of Timo Hannukkala. All rights reserved.
 *
 * \author Timo Hannukkala <timohannukkala@hotmail.com>
 */

#ifndef PRJ_TG_HTTPS_LIB_PRIVATE_H
#define PRJ_TG_HTTPS_LIB_PRIVATE_H

#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include "../prj_tg_https_lib.h"
#include "download_data_handler.h"
#include "remove_chunk.h"

/*!
 * \brief PrjTgHttpsLibPrivate
 *
 * Every connect runs in its own thread. The destructor cancels running
 * downloads and waits until all threads have finished; no callbacks are
 * called after destruction has started.
 */
class PrjTgHttpsLibPrivate
{
public:
    PrjTgHttpsLibPrivate();
    ~PrjTgHttpsLibPrivate();
    PrjTgHttpsLibPrivate(const PrjTgHttpsLibPrivate &) = delete;
    PrjTgHttpsLibPrivate &operator=(const PrjTgHttpsLibPrivate &) = delete;

    uint32_t setConnect(std::string host, std::string request,
                    std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> callback,
                    bool urlIsUsed = false);

private:
    enum ReadResult {
        ReadGotData,         // data received
        ReadWouldBlock,      // receive timeout expired without data, connection still open
        ReadConnectionClosed,// peer closed the connection
        ReadError            // fatal SSL / socket error
    };

    // what has been parsed from the response header so far
    struct ResponseInfo {
        uint64_t headerSize = __UINT64_MAX__;   // including the empty line, = body offset
        uint64_t contentSize = __UINT64_MAX__;
        DownloadDataHandler::Chunked chunked = DownloadDataHandler::Chunked::ContentIsNotFullyReceived;
    };

    static std::atomic<uint32_t> m_currentId;

    SSL_CTX *m_sslCtx;
    std::atomic<bool> m_stopping;
    std::mutex m_threadMutex;
    std::condition_variable m_threadsFinished;
    int m_activeThreads;

    void runDownload(uint32_t id, std::string host, std::string request,
                     std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> callback,
                     bool urlIsUsed);
    void download(uint32_t id, const std::string &host, const std::string &request,
                  const std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> &callback,
                  bool urlIsUsed);
    void threadFinished();
    void report(const std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> &callback,
                uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &data);
    int createConnect(uint32_t id, const std::string &host, bool hostIsIp,
                      const std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> &callback);
    SSL *createSsl(int s, const std::string &verifyHost);
    bool receivePackage(SSL *ssl, uint32_t id, const std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> &callback);
    PrjTgHttpsLib::PrjTgHttpsLibState downloadResponse(SSL *ssl, uint32_t id, std::string &resultData, bool &sslUsable);

    static SSL_CTX *createSslContext();
    static PrjTgHttpsLib::PrjTgHttpsLibState completedState(int statusCode);
    static bool connectWithTimeout(int s, const struct sockaddr *address, socklen_t addressLength, int timeoutSeconds);
    static void setSocketTimeouts(int s, int timeoutSeconds);
    static bool isIpAddress(const std::string &host);
    static std::string getHostFromRequest(const std::string &request);
    static int sendRequest(SSL *ssl, const std::string &request);
    static ReadResult readPackage(SSL *ssl, DownloadDataHandler *receivedData);
    static ReadResult classifyReadError(SSL *ssl, int receivedCount);
    static void parseHeader(const DownloadDataHandler &receivedData, ResponseInfo &info);
    static bool contentLengthReached(const DownloadDataHandler &receivedData, const ResponseInfo &info);
    static RemoveChunk::Result parseChunked(const DownloadDataHandler &receivedData, const ResponseInfo &info, RemoveChunk &chunkDecoder);
};

#endif // PRJ_TG_HTTPS_LIB_PRIVATE_H
