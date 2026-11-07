#include "remove_chunk.h"
#include <cstring>
#include <strings.h>
#include <limits>

// limit for a chunk size line or a trailer line; anything longer is malformed
#define MAX_CHUNK_LINE_LENGTH 4096

RemoveChunk::RemoveChunk()
    : m_state(StateChunkSize)
    , m_started(false)
    , m_position(0)
    , m_chunkSize(0)
    , m_headerLength(0)
    , m_lineEnd("\r\n")
{

}

/**
 * copies the header to m_parsedData without its empty line and without the
 * Transfer-Encoding and Content-Length fields (they do not match the decoded body)
 */
void RemoveChunk::copyHeader(const char *data, size_t headerSize)
{
    m_parsedData.clear();
    size_t lineStart = 0;
    while (lineStart < headerSize) {
        const void *found = memchr(data + lineStart, '\n', headerSize - lineStart);
        size_t nextLine = found ? static_cast<size_t>(static_cast<const char *>(found) - data) + 1 : headerSize;
        size_t lineEnd = nextLine;
        if (lineEnd > lineStart && data[lineEnd - 1] == '\n') {
            lineEnd--;
        }
        if (lineEnd > lineStart && data[lineEnd - 1] == '\r') {
            lineEnd--;
        }
        if (lineStart == 0) {
            m_lineEnd = (nextLine - lineEnd == 2) ? "\r\n" : "\n";
        }
        if (lineEnd == lineStart) {
            break; // empty line: end of header
        }
        bool isTransferEncoding = lineEnd - lineStart > 18 && strncasecmp(data + lineStart, "Transfer-Encoding:", 18) == 0;
        bool isContentLength = lineEnd - lineStart > 15 && strncasecmp(data + lineStart, "Content-Length:", 15) == 0;
        if (!isTransferEncoding && !isContentLength) {
            m_parsedData.append(data + lineStart, nextLine - lineStart);
        }
        lineStart = nextLine;
    }
    m_headerLength = m_parsedData.size();
}

/**
 * finds the end of the line starting at position, accepts "\r\n" and "\n"
 *
 * @param lineEnd position of the line end (line content is [position, lineEnd))
 * @param nextLine position where the next line starts
 * @return false if the line end has not been received yet
 */
bool RemoveChunk::findLineEnd(const char *data, size_t size, size_t position, size_t &lineEnd, size_t &nextLine)
{
    if (position >= size) {
        return false;
    }
    const void *found = memchr(data + position, '\n', size - position);
    if (!found) {
        return false;
    }
    nextLine = static_cast<size_t>(static_cast<const char *>(found) - data) + 1;
    lineEnd = nextLine - 1;
    if (lineEnd > position && data[lineEnd - 1] == '\r') {
        lineEnd--;
    }
    return true;
}

/**
 * finds the line starting at m_position, fails if the line is too long
 *
 * @return ChunkCompleted if a full line was found, ChunkNeedMoreData or ChunkError otherwise
 */
RemoveChunk::Result RemoveChunk::parseLine(const char *data, size_t size, size_t &lineEnd, size_t &nextLine)
{
    if (findLineEnd(data, size, m_position, lineEnd, nextLine)) {
        if (lineEnd - m_position > MAX_CHUNK_LINE_LENGTH) {
            return ChunkError;
        }
        return ChunkCompleted;
    }
    if (size - m_position > MAX_CHUNK_LINE_LENGTH) {
        return ChunkError;
    }
    return ChunkNeedMoreData;
}

/**
 * parses chunk size line: hex digits, optionally followed by whitespace and
 * ";extensions" (e.g. "1a", "000", "5;name=value")
 *
 * @return false if the line is not a valid chunk size
 */
bool RemoveChunk::parseChunkSize(const char *line, size_t length, uint64_t &chunkSize)
{
    uint64_t value = 0;
    size_t i = 0;
    for (; i < length; i++) {
        char c = line[i];
        uint64_t digit;
        if (c >= '0' && c <= '9') {
            digit = static_cast<uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<uint64_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<uint64_t>(c - 'A' + 10);
        } else {
            break;
        }
        if (value > (std::numeric_limits<uint64_t>::max() >> 4)) {
            return false; // overflow
        }
        value = (value << 4) | digit;
    }
    if (i == 0) {
        return false; // no hex digits
    }
    // rest may only be whitespace and/or chunk extensions
    for (; i < length; i++) {
        if (line[i] == ';') {
            break;
        }
        if (line[i] != ' ' && line[i] != '\t') {
            return false;
        }
    }
    chunkSize = value;
    return true;
}

RemoveChunk::Result RemoveChunk::parse(const char *data, size_t size, uint64_t headerSize)
{
    if (m_state == StateCompleted) {
        return ChunkCompleted;
    }
    if (m_state == StateError) {
        return ChunkError;
    }
    if (!data || headerSize > size) {
        return ChunkNeedMoreData;
    }
    if (!m_started) {
        m_started = true;
        m_position = static_cast<size_t>(headerSize);
        copyHeader(data, static_cast<size_t>(headerSize));
    }

    while (true) {
        size_t lineEnd = 0;
        size_t nextLine = 0;
        switch (m_state) {
            case StateChunkSize: {
                Result lineResult = parseLine(data, size, lineEnd, nextLine);
                if (lineResult == ChunkNeedMoreData) {
                    return ChunkNeedMoreData;
                }
                if (lineResult == ChunkError
                    || !parseChunkSize(data + m_position, lineEnd - m_position, m_chunkSize)) {
                    m_state = StateError;
                    return ChunkError;
                }
                m_position = nextLine;
                m_state = (m_chunkSize == 0) ? StateTrailers : StateChunkData;
                break;
            }
            case StateChunkData: {
                // wait until the whole chunk and the line break after it are received
                size_t available = size - m_position;
                if (m_chunkSize >= available) {
                    return ChunkNeedMoreData;
                }
                size_t chunkSize = static_cast<size_t>(m_chunkSize);
                size_t afterData = m_position + chunkSize;
                size_t lineBreakLength;
                if (data[afterData] == '\n') {
                    lineBreakLength = 1;
                } else if (data[afterData] == '\r') {
                    if (afterData + 1 >= size) {
                        return ChunkNeedMoreData;
                    }
                    if (data[afterData + 1] != '\n') {
                        m_state = StateError;
                        return ChunkError;
                    }
                    lineBreakLength = 2;
                } else {
                    // chunk data is not followed by a line break: wrong chunk size
                    m_state = StateError;
                    return ChunkError;
                }
                m_parsedData.append(data + m_position, chunkSize);
                m_position = afterData + lineBreakLength;
                m_state = StateChunkSize;
                break;
            }
            case StateTrailers: {
                Result lineResult = parseLine(data, size, lineEnd, nextLine);
                if (lineResult == ChunkNeedMoreData) {
                    return ChunkNeedMoreData;
                }
                if (lineResult == ChunkError) {
                    m_state = StateError;
                    return ChunkError;
                }
                bool emptyLine = (lineEnd == m_position);
                m_position = nextLine;
                if (emptyLine) {
                    // empty line ends the chunked body: complete the header
                    size_t bodySize = m_parsedData.size() - m_headerLength;
                    m_parsedData.insert(m_headerLength, "Content-Length: " + std::to_string(bodySize) + m_lineEnd + m_lineEnd);
                    m_state = StateCompleted;
                    return ChunkCompleted;
                }
                // trailer header line: ignored
                break;
            }
            case StateCompleted:
                return ChunkCompleted;
            case StateError:
            default:
                return ChunkError;
        }
    }
}

void RemoveChunk::takeParsedData(std::string &parsedData)
{
    parsedData.swap(m_parsedData);
    m_parsedData.clear();
}
