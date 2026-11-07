/*!
 * \file
 * \brief file main.cpp
 *
 * main of prj-tg-https-test
 *
 * Copyright of Timo Hannukkala. All rights reserved.
 *
 * \author Timo Hannukkala <timohannukkala@hotmail.com>
 */

#include <chrono>
#include <string>
#include <thread>
#include "prj_tg_https_test.h"
#include <prj_https_log.h>

// downloaded when no url is given on the command line
#define DEFAULT_TEST_URL "https://www.kba.de/SharedDocs/Downloads/DE/Statistik/Fahrzeuge/FZ9/fz9_202511.xlsx?__blob=publicationFile&v=5"

/*!
 * \brief main
 *
 * usage: prj-https-test [https-url]
 *
 * \return 0 if the download succeeded, 1 otherwise
 */
int main(int argc, char *argv[])
{
    std::string url = (argc > 1) ? argv[1] : DEFAULT_TEST_URL;
    PrjTgHttpsTest app;
    PrjTgHttpsTest::PrjTgHttpsTestResult result = PrjTgHttpsTest::PrjTgHttpsTestResult::TestContinues;

    if (!app.testConnectUrl(url)) {
        return 1;
    }
    PRJ_HTTPS_INFO_LOG("Test starts (url):", url);
    while (1) {
        result = app.getCompleted();
        if (result != PrjTgHttpsTest::PrjTgHttpsTestResult::TestContinues) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    switch (result) {
        case PrjTgHttpsTest::PrjTgHttpsTestResult::TestContinues:
        default:
            break;
        case PrjTgHttpsTest::PrjTgHttpsTestResult::TestOk:
            PRJ_HTTPS_INFO_LOG("Test completed (url)");
            break;
        case PrjTgHttpsTest::PrjTgHttpsTestResult::TestFailed:
            PRJ_HTTPS_ERROR_LOG("Test failed (url)");
            break;
    }
    return result == PrjTgHttpsTest::PrjTgHttpsTestResult::TestOk ? 0 : 1;
}
