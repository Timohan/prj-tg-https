#include "prj_tg_https_lib.h"
#include "private/prj_tg_https_lib_private.h"
#include "prj_https_log.h"
#include <strings.h>

PrjTgHttpsLib::PrjTgHttpsLib()
    : m_private(new PrjTgHttpsLibPrivate())
{

}
PrjTgHttpsLib::~PrjTgHttpsLib() = default;

/**
 * @brief PrjTgHttpsLib::connectIp
 *
 * connects to https port (443) with async
 *
 * @param ip IPv4 or IPv6 address, for example "216.58.211.228"
 * @param request for example generateRequest("about.google", "/", "curl/8.5.0").
 * The certificate is verified for the Host header of the request, or for the
 * ip address if the request has no Host header.
 * @param callback callback results, even connects and getting request is async,
 * callback is will block that connect functionality
 * @return uint32_t connect id, is same as id in callback
 */
uint32_t PrjTgHttpsLib::connectIp(std::string ip, std::string request,
        std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> callback)
{
    return m_private->setConnect(ip, request, callback);
}

/**
 * @brief PrjTgHttpsLib::connectUrl
 *
 * like connectIp(), but resolves host name (IPv4 and IPv6)
 *
 * @param url host name, for example "www.kba.de" (see getDomain())
 */
uint32_t PrjTgHttpsLib::connectUrl(std::string url, std::string request,
            std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> callback)
{
    return m_private->setConnect(url, request, callback, true);
}

/**
 * returns host name of an https url, for example
 * "https://www.kba.de:443/DE/index.html?x=1" -> "www.kba.de"
 *
 * @return host name without port, "" if url is not an https url
 */
std::string PrjTgHttpsLib::getDomain(const std::string &url)
{
    if (url.size() < 8 || strncasecmp(url.c_str(), "https://", 8) != 0) {
        return "";
    }
    std::string host = url.substr(8);
    // authority ends at path, query or fragment
    size_t end = host.find_first_of("/?#");
    if (end != std::string::npos) {
        host.resize(end);
    }
    // remove user info ("user:password@")
    size_t at = host.rfind('@');
    if (at != std::string::npos) {
        host = host.substr(at + 1);
    }
    if (!host.empty() && host[0] == '[') {
        // IPv6 address, e.g. "[::1]:443"
        size_t close = host.find(']');
        if (close == std::string::npos) {
            return "";
        }
        return host.substr(1, close - 1);
    }
    // remove port
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        host.resize(colon);
    }
    return host;
}

/**
 * generates GET request
 *
 * @param host host name, for example "www.kba.de"
 * @param urlRequest path, for example "/index.html"
 * @param userAgent for example "curl/8.5.0" ("User-Agent: curl/8.5.0" is accepted too)
 * @return request, "" if a parameter contains '\r', '\n' or '\0' (would inject
 * header lines); connecting with an empty request fails with FailedToSendSSLRequest
 */
std::string PrjTgHttpsLib::generateRequest(const std::string &host, const std::string &urlRequest, const std::string &userAgent)
{
    const std::string invalidCharacters("\r\n\0", 3);
    if (host.find_first_of(invalidCharacters) != std::string::npos
        || urlRequest.find_first_of(invalidCharacters) != std::string::npos
        || userAgent.find_first_of(invalidCharacters) != std::string::npos) {
        PRJ_HTTPS_ERROR_LOG("Invalid character in request parameters", host);
        return "";
    }
    std::string agent = userAgent;
    if (agent.size() >= 11 && strncasecmp(agent.c_str(), "User-Agent:", 11) == 0) {
        size_t start = agent.find_first_not_of(" \t", 11);
        agent = (start == std::string::npos) ? std::string() : agent.substr(start);
    }
    std::string ret = "GET " + urlRequest + " HTTP/1.1\r\n";
    ret += "Host: " + host + "\r\n";
    ret += "User-Agent: " + agent + "\r\n";
    ret += "Accept: */*\r\n";
    ret += "Accept-Encoding: identity\r\n";
    // one request per connection: the server closes the connection after the response
    ret += "Connection: close\r\n";
    return ret + "\r\n";
}

/**
 * returns the body of a response (data after the header)
 *
 * @param data response received in the callback
 * @param contentData body
 * @return false if data has no complete header
 */
bool PrjTgHttpsLib::getContentData(const std::string &data, std::string &contentData)
{
    uint64_t headerSize = DownloadDataHandler::findHeaderLength(data.data(), data.size());
    if (headerSize == __UINT64_MAX__) {
        return false;
    }

    contentData = data.substr(headerSize);
    return true;
}

/**
 * finds a header field of a response, field names are compared case-insensitively
 *
 * @param data response received in the callback
 * @param name field name without ':', e.g. "Location" or "Content-Type"
 * @param value field value without surrounding whitespace
 * @return false if data has no complete header or the field is not in it
 */
bool PrjTgHttpsLib::getHeaderField(const std::string &data, const std::string &name, std::string &value)
{
    uint64_t headerSize = DownloadDataHandler::findHeaderLength(data.data(), data.size());
    return DownloadDataHandler::findHeaderValue(data.data(), data.size(), headerSize, name.c_str(), value);
}
