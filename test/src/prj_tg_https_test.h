/*!
 * \file
 * \brief file prj_tg_https_test.h
 *
 * PrjTgHttpsLib library's test class
 *
 * Copyright of Timo Hannukkala. All rights reserved.
 *
 * \author Timo Hannukkala <timohannukkala@hotmail.com>
 */

#ifndef PRJ_TG_HTTPS_TEST_H
#define PRJ_TG_HTTPS_TEST_H

#include <prj_tg_https_lib.h>
#include <atomic>

class PrjTgHttpsTest
{
public:
    enum PrjTgHttpsTestResult {
        TestContinues,
        TestOk,
        TestFailed
    };

    bool testConnectUrl(const std::string &url);
    PrjTgHttpsTestResult getCompleted();

private:
    // written from the download thread, read from the main thread
    std::atomic<PrjTgHttpsTestResult> m_connectCompleted{PrjTgHttpsTestResult::TestContinues};
    // last member: destroyed first, so no callback can run after the members above are gone
    PrjTgHttpsLib m_httpsLib;

    void onHttpsCallback(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData);
};

#endif