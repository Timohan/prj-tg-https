/*!
 * \file
 * \brief file prj_tg_https_lib.h
 *
 * library's public functionality
 *
 * Copyright of Timo Hannukkala. All rights reserved.
 *
 * \author Timo Hannukkala <timohannukkala@hotmail.com>
 */

#ifndef PRJ_TG_HTTPS_LIB_H
#define PRJ_TG_HTTPS_LIB_H

#include <string>
#include <cstdint>
#include <functional>
#include <memory>

class PrjTgHttpsLibPrivate;

#ifndef TG_HTTPS_LIBRARY_EXPORT
#define TG_HTTPS_LIBRARY_EXPORT
#endif

/*!
 * \brief PrjTgHttpsLib
 *
 * Downloads run in background threads and report progress through the callback
 * (called from the download thread). The server certificate is verified.
 *
 * The destructor cancels running downloads and waits for their threads (up to a
 * few seconds); no callbacks are called after destruction has started. Destroy
 * this object before the objects its callbacks use (e.g. declare it as the last
 * member of a class whose methods are used as callbacks).
 */
class TG_HTTPS_LIBRARY_EXPORT PrjTgHttpsLib
{
public:
    explicit PrjTgHttpsLib();
    ~PrjTgHttpsLib();
    PrjTgHttpsLib(const PrjTgHttpsLib &) = delete;
    PrjTgHttpsLib &operator=(const PrjTgHttpsLib &) = delete;

    enum PrjTgHttpsLibState {
        PrjTgHttpsLibState_Connecting,
        PrjTgHttpsLibState_Connected,
        PrjTgHttpsLibState_Completed,
        PrjTgHttpsLibState_FailedToCreateSocket,
        PrjTgHttpsLibState_FailedToConnect,
        PrjTgHttpsLibState_FailedToCreateSSL,
        PrjTgHttpsLibState_FailedToSSLConnect,
        PrjTgHttpsLibState_FailedToSendSSLRequest,
        PrjTgHttpsLibState_FailedErrorOnDownloading,
        // 3xx response (not followed): receivedData is the full response,
        // the new address is in its "Location" header, see getHeaderField()
        PrjTgHttpsLibState_Redirected
    };

    uint32_t connectIp(std::string ip, std::string request,
            std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> callback);
    uint32_t connectUrl(std::string url, std::string request,
            std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> callback);
    static std::string getDomain(const std::string &url);
    static std::string generateRequest(const std::string &host, const std::string &urlRequest, const std::string &userAgent);
    static bool getContentData(const std::string &data, std::string &contentData);
    static bool getHeaderField(const std::string &data, const std::string &name, std::string &value);
private:
    std::unique_ptr<PrjTgHttpsLibPrivate> m_private;
};

#endif // PRJ_TG_HTTPS_LIB_H
