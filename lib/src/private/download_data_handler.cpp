#include "download_data_handler.h"
#include <cstring>
#include <strings.h>
#include <cstdlib>
#include <algorithm>
#include <limits>
#include <sstream>
#include <string_view>

DownloadDataHandler::DownloadDataHandler()
    : m_data(nullptr)
    , m_dataAllocSize(0)
    , m_addedSize(0)
    , m_triedSetCorrectSize(false)
    , m_statusCode(-1)
    , m_statusLineParsed(false)
{

}

DownloadDataHandler::~DownloadDataHandler()
{
    if (m_data) {
        free(m_data);
    }
}

// Initial buffer size
#define INITIAL_ALLOC_SIZE (static_cast<size_t>(100000UL))
// Upper limit for allocating the whole response at once based on Content-Length.
// A larger (or fake) Content-Length does not allocate more up front; the buffer
// then grows as data actually arrives.
#define MAX_PREALLOC_SIZE (static_cast<size_t>(64UL * 1024UL * 1024UL))

/**
 * makes sure m_data can hold at least neededSize bytes
 *
 * @return false if memory could not be allocated, m_data is unchanged then
 */
bool DownloadDataHandler::reserve(size_t neededSize)
{
    if (neededSize <= m_dataAllocSize) {
        return true;
    }

    size_t newSize;
    if (m_dataAllocSize == 0) {
        newSize = std::max(neededSize, INITIAL_ALLOC_SIZE);
    } else {
        // grow geometrically so large downloads don't realloc/copy too often
        if (m_dataAllocSize > std::numeric_limits<size_t>::max() / 2) {
            newSize = neededSize;
        } else {
            newSize = std::max(neededSize, m_dataAllocSize * 2);
        }
        // once the header is received, try once to allocate the full response
        if (!m_triedSetCorrectSize) {
            size_t expectedSize = getExpectedTotalSize();
            if (expectedSize != 0) {
                m_triedSetCorrectSize = true;
                newSize = std::max(neededSize, expectedSize);
            }
        }
    }

    // realloc(nullptr, n) works like malloc(n)
    char *newData = static_cast<char *>(realloc(m_data, newSize));
    if (!newData) {
        // old m_data is still valid and owned by us
        return false;
    }
    m_data = newData;
    m_dataAllocSize = newSize;
    return true;
}

bool DownloadDataHandler::addData(const char *data, const size_t size)
{
    if (size == 0) {
        return true;
    }
    // +1 for the terminating '\0'
    if (size > std::numeric_limits<size_t>::max() - m_addedSize - 1) {
        return false;
    }
    if (!reserve(m_addedSize + size + 1)) {
        return false;
    }
    memcpy(m_data + m_addedSize, data, size);

    m_addedSize += size;
    m_data[m_addedSize] = '\0';

    parseStatusLine();
    return true;
}

void DownloadDataHandler::discardFront(size_t size)
{
    if (!m_data || size == 0) {
        return;
    }
    if (size > m_addedSize) {
        size = m_addedSize;
    }
    memmove(m_data, m_data + size, m_addedSize - size);
    m_addedSize -= size;
    m_data[m_addedSize] = '\0';
    m_statusCode = -1;
    m_statusLineParsed = false;
    m_triedSetCorrectSize = false;
    parseStatusLine();
}

/**
 * parses the status code of a status line, e.g. "HTTP/1.1 503 Service Unavailable" -> 503
 *
 * @return status code, -1 if the line is not a valid status line
 */
static int parseStatusCode(const std::string &statusLine)
{
    std::istringstream stream(statusLine);
    std::string httpVersion;
    // read into a local variable: on failure operator>> writes 0
    int statusCode = -1;
    if (!(stream >> httpVersion) || !(stream >> statusCode) || statusCode < 100 || statusCode > 999) {
        return -1;
    }
    return statusCode;
}

// status line longer than this is treated as invalid
#define MAX_STATUS_LINE_LENGTH 8192

/**
 * parses the status line (e.g. "HTTP/1.1 200 OK\r\n") once it has been received.
 * Parsing is tried only once; if the line is invalid, status code stays -1.
 */
void DownloadDataHandler::parseStatusLine()
{
    if (m_statusLineParsed || !m_data || m_addedSize == 0) {
        return;
    }
    // the status line ends with "\r\n" (or non-standard "\n")
    size_t searchSize = std::min(m_addedSize, static_cast<size_t>(MAX_STATUS_LINE_LENGTH));
    const void *found = memchr(m_data, '\n', searchSize);
    if (!found) {
        if (m_addedSize >= MAX_STATUS_LINE_LENGTH) {
            m_statusLineParsed = true;  // no status line, give up
        }
        return;
    }
    size_t lineEnd = static_cast<size_t>(static_cast<const char *>(found) - m_data);
    if (lineEnd > 0 && m_data[lineEnd - 1] == '\r') {
        lineEnd--;
    }
    m_statusLineParsed = true;
    m_statusCode = parseStatusCode(std::string(m_data, lineEnd));
}


/**
 * @return size of the whole response (header + content + '\0') based on
 * Content-Length, 0 if not known yet or larger than MAX_PREALLOC_SIZE
 */
size_t DownloadDataHandler::getExpectedTotalSize() const
{
    uint64_t headerSize = getHeaderLength();
    if (headerSize == __UINT64_MAX__) {
        return 0;
    }
    uint64_t contentSize = getContentLength(headerSize);
    if (contentSize == __UINT64_MAX__) {
        return 0;
    }
    // checked separately so the sum below cannot overflow
    if (contentSize > MAX_PREALLOC_SIZE || headerSize > MAX_PREALLOC_SIZE) {
        return 0;
    }
    uint64_t total = headerSize + contentSize + 1;
    if (total > MAX_PREALLOC_SIZE) {
        return 0;
    }
    return static_cast<size_t>(total);
}

const char* DownloadDataHandler::getData() const
{
    return m_data;
}

size_t DownloadDataHandler::getDataSize() const
{
    return m_addedSize;
}

std::string DownloadDataHandler::getDataString() const
{
    if (!m_data) {
        return std::string();
    }
    return std::string(m_data, m_addedSize);
}

uint64_t DownloadDataHandler::getHeaderLength() const
{
    if (!m_data) {
        return __UINT64_MAX__;
    }
    return findHeaderLength(m_data, m_addedSize);
}

/**
 * finds the end of the HTTP header: the first empty line, "\r\n\r\n" or
 * (non-standard) "\n\n", whichever comes first. Works on binary data.
 *
 * @return header length including the empty line (= offset of the body),
 * __UINT64_MAX__ if the header end is not found
 */
uint64_t DownloadDataHandler::findHeaderLength(const char *data, size_t size)
{
    std::string_view view(data, size);
    size_t crlfPos = view.find("\r\n\r\n");
    size_t lfPos = view.find("\n\n");
    if (crlfPos == std::string_view::npos && lfPos == std::string_view::npos) {
        return __UINT64_MAX__;
    }
    if (lfPos == std::string_view::npos || (crlfPos != std::string_view::npos && crlfPos < lfPos)) {
        return static_cast<uint64_t>(crlfPos + 4);
    }
    return static_cast<uint64_t>(lfPos + 2);
}

/**
 * finds a header field, field names are compared case-insensitively
 *
 * @param name field name without ':', e.g. "Content-Length"
 * @param headerSize header length including the empty line
 * @param value field value without surrounding whitespace
 * @return false if the field is not in the header
 */
bool DownloadDataHandler::getHeaderValue(const char *name, uint64_t headerSize, std::string &value) const
{
    return findHeaderValue(m_data, m_addedSize, headerSize, name, value);
}

bool DownloadDataHandler::findHeaderValue(const char *data, size_t size, uint64_t headerSize, const char *name, std::string &value)
{
    if (!data || headerSize == __UINT64_MAX__) {
        return false;
    }
    size_t end = static_cast<size_t>(std::min(headerSize, static_cast<uint64_t>(size)));
    size_t nameLength = strlen(name);
    // skip the status line
    const void *found = memchr(data, '\n', end);
    while (found) {
        size_t lineStart = static_cast<size_t>(static_cast<const char *>(found) - data) + 1;
        if (lineStart >= end) {
            break;
        }
        found = memchr(data + lineStart, '\n', end - lineStart);
        size_t lineEnd = found ? static_cast<size_t>(static_cast<const char *>(found) - data) : end;
        if (lineEnd > lineStart && data[lineEnd - 1] == '\r') {
            lineEnd--;
        }
        if (lineEnd - lineStart > nameLength
            && data[lineStart + nameLength] == ':'
            && strncasecmp(data + lineStart, name, nameLength) == 0) {
            size_t valueStart = lineStart + nameLength + 1;
            while (valueStart < lineEnd && (data[valueStart] == ' ' || data[valueStart] == '\t')) {
                valueStart++;
            }
            size_t valueEnd = lineEnd;
            while (valueEnd > valueStart && (data[valueEnd - 1] == ' ' || data[valueEnd - 1] == '\t')) {
                valueEnd--;
            }
            value.assign(data + valueStart, valueEnd - valueStart);
            return true;
        }
    }
    return false;
}

/**
 * @return Content-Length value, __UINT64_MAX__ if missing or invalid
 */
uint64_t DownloadDataHandler::getContentLength(const uint64_t &headerSize) const
{
    std::string value;
    if (!getHeaderValue("Content-Length", headerSize, value) || value.empty()) {
        return __UINT64_MAX__;
    }
    uint64_t length = 0;
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] < '0' || value[i] > '9') {
            return __UINT64_MAX__;
        }
        uint64_t digit = static_cast<uint64_t>(value[i] - '0');
        if (length > (__UINT64_MAX__ - 1 - digit) / 10) {
            return __UINT64_MAX__; // overflow
        }
        length = length * 10 + digit;
    }
    return length;
}

/**
 * @return ContentIsChunked if Transfer-Encoding's last coding is "chunked"
 */
DownloadDataHandler::Chunked DownloadDataHandler::isChunked(const uint64_t &headerSize) const
{
    if (headerSize == __UINT64_MAX__) {
        return DownloadDataHandler::Chunked::ContentIsNotFullyReceived;
    }
    std::string value;
    if (!getHeaderValue("Transfer-Encoding", headerSize, value)) {
        return DownloadDataHandler::Chunked::ContentIsNotChunked;
    }
    // e.g. "gzip, chunked": chunked must be the last transfer coding
    size_t comma = value.rfind(',');
    size_t start = (comma == std::string::npos) ? 0 : comma + 1;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        start++;
    }
    if (value.size() - start == 7 && strncasecmp(value.c_str() + start, "chunked", 7) == 0) {
        return DownloadDataHandler::Chunked::ContentIsChunked;
    }
    return DownloadDataHandler::Chunked::ContentIsNotChunked;
}

int DownloadDataHandler::getStatusCode() const
{
    return m_statusCode;
}
