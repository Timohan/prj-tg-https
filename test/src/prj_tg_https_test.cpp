/*!
 * \file
 * \brief file prj_tg_https_test.cpp
 *
 * PrjTgHttpsLib library's test class
 *
 * Copyright of Timo Hannukkala. All rights reserved.
 *
 * \author Timo Hannukkala <timohannukkala@hotmail.com>
 */

#include "prj_tg_https_test.h"
#include <prj_https_log.h>

/**
 * @brief PrjTgHttpsTest::testConnectUrl
 * starts downloading url
 *
 * @param url https url, e.g. "https://www.kba.de/DE/index.html"
 * @return false if url is not a valid https url
 */
bool PrjTgHttpsTest::testConnectUrl(const std::string &url)
{
    std::string host = PrjTgHttpsLib::getDomain(url);
    if (host.empty()) {
        PRJ_HTTPS_ERROR_LOG("Not an https url:", url);
        return false;
    }
    // path and query: everything after the host part, without fragment
    std::string path = "/";
    size_t pathStart = url.find_first_of("/?#", 8);
    if (pathStart != std::string::npos) {
        path = url.substr(pathStart);
        path = path.substr(0, path.find('#'));
        if (path.empty() || path[0] != '/') {
            path = "/" + path;
        }
    }
    std::string request = PrjTgHttpsLib::generateRequest(host, path, "curl/8.5.0");
    m_connectCompleted = PrjTgHttpsTest::PrjTgHttpsTestResult::TestContinues;
    m_httpsLib.connectUrl(host,
        request,
        std::bind(&PrjTgHttpsTest::onHttpsCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    return true;
}

/**
 * @brief PrjTgHttpsTest::getCompleted
 *
 * @return PrjTgHttpsTest::PrjTgHttpsTestResult
 */
PrjTgHttpsTest::PrjTgHttpsTestResult PrjTgHttpsTest::getCompleted()
{
    return m_connectCompleted;
}

/**
 * @brief PrjTgHttpsTest::onHttpsCallback
 * https callback
 * @param id connect id returned by connectUrl()
 * @param state current https connect state
 * @param receivedData received data
 */
void PrjTgHttpsTest::onHttpsCallback(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)
{
    switch (state) {
        case PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_Connecting:
        case PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_Connected:
        default:
            break;
        case PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_Completed: {
            PRJ_HTTPS_INFO_LOG("id:", id, "DataSize:", receivedData.size());
            m_connectCompleted = PrjTgHttpsTest::PrjTgHttpsTestResult::TestOk;
            break;
        }
        case PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_Redirected: {
            std::string location;
            PrjTgHttpsLib::getHeaderField(receivedData, "Location", location);
            PRJ_HTTPS_ERROR_LOG("id:", id, "redirected to:", location);
            m_connectCompleted = PrjTgHttpsTest::PrjTgHttpsTestResult::TestFailed;
            break;
        }
        case PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToCreateSocket:
        case PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToConnect:
        case PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToCreateSSL:
        case PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToSSLConnect:
        case PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToSendSSLRequest:
        case PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedErrorOnDownloading:
            PRJ_HTTPS_ERROR_LOG("id:", id, "failed, state:", static_cast<int>(state));
            m_connectCompleted = PrjTgHttpsTest::PrjTgHttpsTestResult::TestFailed;
            break;
    }
}

