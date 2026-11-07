#include "prj_tg_https_lib_private.h"
#include "../prj_https_log.h"
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>
#include <cerrno>
#include <cstring>
#include <new>
#include <thread>
#include <system_error>
#include <openssl/err.h>
#include <openssl/x509v3.h>

// port used for all connections
#define HTTPS_PORT "443"
// timeout for TCP connect, per address
#define CONNECT_TIMEOUT_SECONDS 10
// socket timeout during TLS handshake and sending the request
#define HANDSHAKE_TIMEOUT_SECONDS 10
// socket receive timeout while downloading
#define READ_TIMEOUT_SECONDS 2
// download fails after this many consecutive receive timeouts without data
#define MAX_IDLE_COUNT 100
// SSL_read buffer size
#define MAX_BUFFER_SIZE 16384
// response header larger than this is rejected
#define MAX_HEADER_SIZE 65536

std::atomic<uint32_t> PrjTgHttpsLibPrivate::m_currentId(0);

PrjTgHttpsLibPrivate::PrjTgHttpsLibPrivate()
    : m_sslCtx(createSslContext())
    , m_stopping(false)
    , m_activeThreads(0)
{

}

/**
 * cancels running downloads and waits until all download threads have finished.
 * Callbacks are not called anymore after this has started.
 */
PrjTgHttpsLibPrivate::~PrjTgHttpsLibPrivate()
{
    m_stopping = true;
    std::unique_lock<std::mutex> lock(m_threadMutex);
    while (m_activeThreads > 0) {
        m_threadsFinished.wait(lock);
    }
    lock.unlock();
    if (m_sslCtx) {
        SSL_CTX_free(m_sslCtx);
    }
}

/**
 * creates SSL context shared by all connections of this instance
 *
 * @return SSL_CTX or nullptr if failed
 */
SSL_CTX *PrjTgHttpsLibPrivate::createSslContext()
{
    // OpenSSL >= 1.1 initializes itself, SSL_library_init() etc. are not needed
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        PRJ_HTTPS_ERROR_LOG("Failed to create SSL context", "");
        return nullptr;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    // verify the server certificate against the system CA certificates
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        PRJ_HTTPS_ERROR_LOG("Failed to load CA certificates", "");
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
    // OpenSSL 3: many servers close TCP without close_notify; report that as
    // a normal close (SSL_ERROR_ZERO_RETURN). downloadResponse verifies that the
    // response is complete, so truncated responses are still detected.
    SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif
    return ctx;
}

/**
 * @brief PrjTgHttpsLibPrivate::setConnect
 *
 * @param host ip address or host name
 * @param request request
 * @param callback callback of progress
 * @param urlIsUsed if false, host is "ip address", if true host is host name
 * @return uint32_t connect id (will be used with callback)
 */
uint32_t PrjTgHttpsLibPrivate::setConnect(std::string host, std::string request,
    std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> callback,
    bool urlIsUsed)
{
    uint32_t currentId = ++m_currentId;
    {
        std::lock_guard<std::mutex> lock(m_threadMutex);
        m_activeThreads++;
    }
    try {
        std::thread(&PrjTgHttpsLibPrivate::runDownload, this, currentId, host, request, callback, urlIsUsed).detach();
    } catch (const std::system_error &e) {
        PRJ_HTTPS_ERROR_LOG("Failed to create thread", e.what());
        threadFinished();
        report(callback, currentId, PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToCreateSocket, "");
    }
    return currentId;
}

void PrjTgHttpsLibPrivate::threadFinished()
{
    // notify while holding the lock: the destructor cannot destroy the
    // condition variable before this has returned from notify_all()
    std::lock_guard<std::mutex> lock(m_threadMutex);
    m_activeThreads--;
    m_threadsFinished.notify_all();
}

/**
 * calls callback, unless this instance is being destroyed
 */
void PrjTgHttpsLibPrivate::report(const std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> &callback,
                                  uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &data)
{
    if (m_stopping) {
        return;
    }
    callback(id, state, data);
}

/**
 * download thread
 */
void PrjTgHttpsLibPrivate::runDownload(uint32_t id, std::string host, std::string request,
                                       std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> callback,
                                       bool urlIsUsed)
{
    download(id, host, request, callback, urlIsUsed);
    threadFinished();
}

void PrjTgHttpsLibPrivate::download(uint32_t id, const std::string &host, const std::string &request,
                                    const std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> &callback,
                                    bool urlIsUsed)
{
    if (request.empty()) {
        // generateRequest() returns "" for invalid parameters
        PRJ_HTTPS_ERROR_LOG("Empty request", host);
        report(callback, id, PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToSendSSLRequest, "");
        return;
    }

    // name the certificate must be valid for: the host name, or for connectIp()
    // the Host header of the request (falls back to the ip address itself)
    std::string verifyHost = host;
    if (!urlIsUsed) {
        std::string requestHost = getHostFromRequest(request);
        if (!requestHost.empty()) {
            verifyHost = requestHost;
        }
    }

    int s = createConnect(id, host, !urlIsUsed, callback);
    if (s < 0) {
        return;
    }

    SSL *ssl = createSsl(s, verifyHost);
    if (!ssl) {
        PRJ_HTTPS_ERROR_LOG("Failed create SSL", verifyHost);
        report(callback, id, PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToCreateSSL, "");
        close(s);
        return;
    }

    setSocketTimeouts(s, HANDSHAKE_TIMEOUT_SECONDS);
    if (SSL_connect(ssl) <= 0) {
        long verifyResult = SSL_get_verify_result(ssl);
        if (verifyResult != X509_V_OK) {
            PRJ_HTTPS_ERROR_LOG("Certificate verification failed", verifyHost + ": " + X509_verify_cert_error_string(verifyResult));
        } else {
            PRJ_HTTPS_ERROR_LOG("Failed SSL Connect", verifyHost);
        }
        report(callback, id, PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToSSLConnect, "");
        SSL_free(ssl);
        close(s);
        return;
    }

    if (sendRequest(ssl, request) < 0) {
        PRJ_HTTPS_ERROR_LOG("Failed send request", request);
        report(callback, id, PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToSendSSLRequest, "");
        SSL_free(ssl);
        close(s);
        return;
    }

    setSocketTimeouts(s, READ_TIMEOUT_SECONDS);
    if (receivePackage(ssl, id, callback)) {
        // send close_notify; not allowed after a fatal SSL error
        SSL_shutdown(ssl);
    }
    SSL_free(ssl);
    close(s);
}

/**
 * creates SSL object for socket s, with SNI and certificate name verification
 *
 * @param verifyHost host name or ip address the certificate must be valid for
 * @return SSL or nullptr if failed
 */
SSL *PrjTgHttpsLibPrivate::createSsl(int s, const std::string &verifyHost)
{
    if (!m_sslCtx || verifyHost.empty()) {
        return nullptr;
    }
    SSL *ssl = SSL_new(m_sslCtx);
    if (!ssl) {
        return nullptr;
    }
    if (SSL_set_fd(ssl, s) != 1) {
        SSL_free(ssl);
        return nullptr;
    }
    if (isIpAddress(verifyHost)) {
        // no SNI for ip addresses, certificate must contain the ip address
        if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), verifyHost.c_str()) != 1) {
            SSL_free(ssl);
            return nullptr;
        }
    } else {
        // SSL_set_tlsext_host_name() takes a non-const pointer, it copies the name
        std::string hostName = verifyHost;
        if (SSL_set_tlsext_host_name(ssl, &hostName[0]) != 1) {
            SSL_free(ssl);
            return nullptr;
        }
        SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (SSL_set1_host(ssl, verifyHost.c_str()) != 1) {
            SSL_free(ssl);
            return nullptr;
        }
    }
    return ssl;
}

/**
 * @brief resolves host and connects to it (IPv4 and IPv6), tries all addresses
 *
 * @param hostIsIp if true, host must be an ip address (no DNS lookup)
 * @return int socket, -1 if failed
 */
int PrjTgHttpsLibPrivate::createConnect(uint32_t id, const std::string &host, bool hostIsIp,
                                        const std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> &callback)
{
    report(callback, id, PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_Connecting, "");

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;
    if (hostIsIp) {
        hints.ai_flags |= AI_NUMERICHOST;
    }

    struct addrinfo *res = nullptr;
    int gaiResult = getaddrinfo(host.c_str(), HTTPS_PORT, &hints, &res);
    if (gaiResult != 0) {
        PRJ_HTTPS_ERROR_LOG(hostIsIp ? "Invalid ip address" : "Failed to resolve host", host + ": " + gai_strerror(gaiResult));
        report(callback, id, PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToConnect, "");
        return -1;
    }

    bool socketCreated = false;
    int connectedSocket = -1;
    for (struct addrinfo *ai = res; ai != nullptr && !m_stopping; ai = ai->ai_next) {
        int s = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
        if (s < 0) {
            continue;
        }
        socketCreated = true;
        if (connectWithTimeout(s, ai->ai_addr, ai->ai_addrlen, CONNECT_TIMEOUT_SECONDS)) {
            connectedSocket = s;
            break;
        }
        close(s);
    }
    freeaddrinfo(res);

    if (connectedSocket < 0) {
        PRJ_HTTPS_ERROR_LOG("Failed to connect", host);
        report(callback, id, socketCreated
               ? PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToConnect
               : PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedToCreateSocket, "");
        return -1;
    }
    report(callback, id, PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_Connected, "");
    return connectedSocket;
}

/**
 * connect() with timeout (non-blocking connect + poll), socket stays blocking afterwards
 */
bool PrjTgHttpsLibPrivate::connectWithTimeout(int s, const struct sockaddr *address, socklen_t addressLength, int timeoutSeconds)
{
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
        return false;
    }
    bool connected = false;
    if (connect(s, address, addressLength) == 0) {
        connected = true;
    } else if (errno == EINPROGRESS) {
        struct pollfd pfd;
        pfd.fd = s;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        int pollResult;
        do {
            pollResult = poll(&pfd, 1, timeoutSeconds * 1000);
        } while (pollResult < 0 && errno == EINTR);
        if (pollResult > 0) {
            int socketError = 0;
            socklen_t length = sizeof(socketError);
            if (getsockopt(s, SOL_SOCKET, SO_ERROR, &socketError, &length) == 0 && socketError == 0) {
                connected = true;
            }
        }
    }
    if (fcntl(s, F_SETFL, flags) < 0) {
        return false;
    }
    return connected;
}

void PrjTgHttpsLibPrivate::setSocketTimeouts(int s, int timeoutSeconds)
{
    struct timeval tv;
    tv.tv_sec = timeoutSeconds;
    tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

bool PrjTgHttpsLibPrivate::isIpAddress(const std::string &host)
{
    unsigned char buf[sizeof(struct in6_addr)];
    return inet_pton(AF_INET, host.c_str(), buf) == 1
        || inet_pton(AF_INET6, host.c_str(), buf) == 1;
}

/**
 * @return value of the Host header in request without port, "" if not found
 */
std::string PrjTgHttpsLibPrivate::getHostFromRequest(const std::string &request)
{
    // skip the request line, then go through header lines until the empty line
    size_t lineBreak = request.find('\n');
    while (lineBreak != std::string::npos && lineBreak + 1 < request.size()) {
        size_t lineStart = lineBreak + 1;
        lineBreak = request.find('\n', lineStart);
        size_t lineEnd = (lineBreak == std::string::npos) ? request.size() : lineBreak;
        std::string line = request.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break; // end of header
        }
        if (line.size() > 5 && strncasecmp(line.c_str(), "Host:", 5) == 0) {
            size_t start = line.find_first_not_of(" \t", 5);
            if (start == std::string::npos) {
                return "";
            }
            size_t end = line.find_last_not_of(" \t");
            std::string host = line.substr(start, end - start + 1);
            if (!host.empty() && host[0] == '[') {
                // IPv6 address, e.g. "[::1]:443"
                size_t close = host.find(']');
                return close == std::string::npos ? "" : host.substr(1, close - 1);
            }
            size_t colon = host.find(':');
            if (colon != std::string::npos) {
                host.resize(colon);
            }
            return host;
        }
    }
    return "";
}

int PrjTgHttpsLibPrivate::sendRequest(SSL *ssl, const std::string &request)
{
    int len = SSL_write(ssl, request.c_str(), static_cast<int>(request.size()));
    if (len < static_cast<int>(request.size())) {
        return -1;
    }
    return len;
}

PrjTgHttpsLibPrivate::ReadResult PrjTgHttpsLibPrivate::classifyReadError(SSL *ssl, int receivedCount)
{
    int err = SSL_get_error(ssl, receivedCount);
    switch (err) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            // receive timeout (SO_RCVTIMEO) expired, connection still open
            return ReadWouldBlock;
        case SSL_ERROR_ZERO_RETURN:
            // peer sent close_notify
            return ReadConnectionClosed;
        case SSL_ERROR_SYSCALL:
            // OpenSSL 1.1: TCP closed without close_notify
            if (receivedCount == 0 && ERR_peek_error() == 0) {
                return ReadConnectionClosed;
            }
            return ReadError;
        default:
            return ReadError;
    }
}

/**
 * reads once from ssl, waits at most READ_TIMEOUT_SECONDS
 */
PrjTgHttpsLibPrivate::ReadResult PrjTgHttpsLibPrivate::readPackage(SSL *ssl, DownloadDataHandler *receivedData)
{
    char buf[MAX_BUFFER_SIZE];

    int receivedCount = SSL_read(ssl, buf, sizeof(buf));
    if (receivedCount <= 0) {
        return classifyReadError(ssl, receivedCount);
    }
    if (!receivedData->addData(buf, static_cast<size_t>(receivedCount))) {
        PRJ_HTTPS_ERROR_LOG("Out of memory while downloading, received bytes", std::to_string(receivedData->getDataSize()));
        return ReadError;
    }
    return ReadGotData;
}

/**
 * updates header size, content length and chunked state from the data received so far.
 *
 * The end of the response is determined as in RFC 9112 6.3:
 * 1. 204 and 304 responses have no body (1xx are skipped in downloadResponse)
 * 2. Transfer-Encoding: chunked -> chunked body
 * 3. Content-Length -> body of that size
 * 4. otherwise the body ends when the server closes the connection
 */
void PrjTgHttpsLibPrivate::parseHeader(const DownloadDataHandler &receivedData, ResponseInfo &info)
{
    if (info.headerSize == __UINT64_MAX__) {
        info.headerSize = receivedData.getHeaderLength();
    }
    if (info.headerSize == __UINT64_MAX__) {
        return;
    }
    int statusCode = receivedData.getStatusCode();
    if (statusCode == 204 || statusCode == 304) {
        info.chunked = DownloadDataHandler::Chunked::ContentIsNotChunked;
        info.contentSize = 0;
        return;
    }
    if (info.chunked == DownloadDataHandler::Chunked::ContentIsNotFullyReceived) {
        info.chunked = receivedData.isChunked(info.headerSize);
    }
    // Content-Length is ignored for chunked responses (RFC 9112 6.3)
    if (info.chunked == DownloadDataHandler::Chunked::ContentIsNotChunked
        && info.contentSize == __UINT64_MAX__) {
        info.contentSize = receivedData.getContentLength(info.headerSize);
    }
}

bool PrjTgHttpsLibPrivate::contentLengthReached(const DownloadDataHandler &receivedData, const ResponseInfo &info)
{
    return info.contentSize != __UINT64_MAX__
        && info.headerSize != __UINT64_MAX__
        && info.contentSize + info.headerSize <= receivedData.getDataSize();
}

/**
 * decodes newly received chunked data
 *
 * @return ChunkCompleted if all chunks have been received (result in chunkDecoder),
 * ChunkError if chunked data is malformed, ChunkNeedMoreData otherwise
 * (also when the response is not chunked)
 */
RemoveChunk::Result PrjTgHttpsLibPrivate::parseChunked(const DownloadDataHandler &receivedData, const ResponseInfo &info, RemoveChunk &chunkDecoder)
{
    if (info.chunked != DownloadDataHandler::Chunked::ContentIsChunked) {
        return RemoveChunk::ChunkNeedMoreData;
    }
    return chunkDecoder.parse(receivedData.getData(), receivedData.getDataSize(), info.headerSize);
}

/**
 * @return Redirected for 3xx responses (except 304 Not Modified), Completed otherwise
 */
PrjTgHttpsLib::PrjTgHttpsLibState PrjTgHttpsLibPrivate::completedState(int statusCode)
{
    if (statusCode >= 300 && statusCode < 400 && statusCode != 304) {
        return PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_Redirected;
    }
    return PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_Completed;
}

/**
 * downloads the response
 *
 * @param resultData received data (empty on failure)
 * @param sslUsable set to false after a fatal SSL error
 * @return Completed, Redirected or FailedErrorOnDownloading
 */
PrjTgHttpsLib::PrjTgHttpsLibState PrjTgHttpsLibPrivate::downloadResponse(SSL *ssl, uint32_t id, std::string &resultData, bool &sslUsable)
{
    ResponseInfo info;
    int idleCount = 0;
    DownloadDataHandler receivedData;
    RemoveChunk chunkDecoder;
    sslUsable = true;
    resultData.clear();

    while (true) {
        if (m_stopping) {
            return PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedErrorOnDownloading;
        }
        ReadResult result = readPackage(ssl, &receivedData);
        parseHeader(receivedData, info);
        // skip interim responses (e.g. "103 Early Hints"), the final response follows them
        while (info.headerSize != __UINT64_MAX__
               && receivedData.getStatusCode() >= 100 && receivedData.getStatusCode() < 200) {
            receivedData.discardFront(static_cast<size_t>(info.headerSize));
            info = ResponseInfo();
            parseHeader(receivedData, info);
        }

        if (info.headerSize == __UINT64_MAX__ && receivedData.getDataSize() > MAX_HEADER_SIZE) {
            PRJ_HTTPS_ERROR_LOG("Response header too large", std::to_string(id));
            return PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedErrorOnDownloading;
        }
        if (receivedData.getStatusCode() >= 400) {
            return PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedErrorOnDownloading;
        }

        // check completion after every read, so the result is delivered as soon as it is complete
        RemoveChunk::Result chunkResult = parseChunked(receivedData, info, chunkDecoder);
        if (chunkResult == RemoveChunk::ChunkCompleted) {
            chunkDecoder.takeParsedData(resultData);
            return completedState(receivedData.getStatusCode());
        }
        if (chunkResult == RemoveChunk::ChunkError) {
            PRJ_HTTPS_ERROR_LOG("Invalid chunked data", std::to_string(id));
            return PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedErrorOnDownloading;
        }
        if (contentLengthReached(receivedData, info)) {
            // header + exactly Content-Length bytes, anything after that is not part of the response
            resultData.assign(receivedData.getData(), static_cast<size_t>(info.headerSize + info.contentSize));
            return completedState(receivedData.getStatusCode());
        }

        // body without Content-Length and chunked encoding: ends when the connection is closed
        bool bodyEndsOnClose = info.headerSize != __UINT64_MAX__
            && info.contentSize == __UINT64_MAX__
            && info.chunked == DownloadDataHandler::Chunked::ContentIsNotChunked;

        switch (result) {
            case ReadGotData:
                idleCount = 0;
                break;
            case ReadWouldBlock:
                idleCount++;
                if (idleCount > MAX_IDLE_COUNT) {
                    PRJ_HTTPS_ERROR_LOG("Download timed out", std::to_string(id));
                    return PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedErrorOnDownloading;
                }
                break;
            case ReadConnectionClosed:
                if (bodyEndsOnClose) {
                    resultData = receivedData.getDataString();
                    return completedState(receivedData.getStatusCode());
                }
                PRJ_HTTPS_ERROR_LOG("Connection closed before response was complete", std::to_string(id));
                return PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedErrorOnDownloading;
            case ReadError:
            default:
                sslUsable = false;
                PRJ_HTTPS_ERROR_LOG("Error while downloading", std::to_string(id));
                return PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedErrorOnDownloading;
        }
    }
}

/**
 * downloads the response and calls callback with the result
 *
 * @return false if a fatal SSL error occurred (SSL_shutdown() must not be called)
 */
bool PrjTgHttpsLibPrivate::receivePackage(SSL *ssl, uint32_t id, const std::function<void(uint32_t id, PrjTgHttpsLib::PrjTgHttpsLibState state, const std::string &receivedData)> &callback)
{
    PrjTgHttpsLib::PrjTgHttpsLibState state;
    std::string resultData;
    bool sslUsable = true;
    try {
        state = downloadResponse(ssl, id, resultData, sslUsable);
    } catch (const std::bad_alloc &) {
        // a large download may not fit in memory when copied to std::string
        PRJ_HTTPS_ERROR_LOG("Out of memory while downloading", std::to_string(id));
        resultData.clear();
        state = PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_FailedErrorOnDownloading;
    }
    if (state != PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_Completed
        && state != PrjTgHttpsLib::PrjTgHttpsLibState::PrjTgHttpsLibState_Redirected) {
        resultData.clear();
    }
    // called outside of try, exceptions thrown by the callback are not caught here
    report(callback, id, state, resultData);
    return sslUsable;
}
