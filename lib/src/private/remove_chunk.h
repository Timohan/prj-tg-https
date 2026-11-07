/*!
 * \file
 * \brief file remove_chunk.h
 *
 * Remove chunk
 *
 * Copyright of Timo Hannukkala. All rights reserved.
 *
 * \author Timo Hannukkala <timohannukkala@hotmail.com>
 */

#ifndef REMOVE_CHUNK_H
#define REMOVE_CHUNK_H

#include <string>
#include <cstdint>
#include <cstddef>

/*!
 * \brief RemoveChunk
 *
 * Incremental decoder for "Transfer-Encoding: chunked" responses.
 * parse() can be called again each time more data has been received; it
 * continues from where the previous call stopped, so data is parsed only once.
 */
class RemoveChunk
{
public:
    enum Result {
        ChunkNeedMoreData = 0,  // response not fully received yet
        ChunkCompleted,         // last chunk and trailers received
        ChunkError              // malformed chunked data
    };

    RemoveChunk();

    /*!
     * \param data all data received so far (header + chunked body)
     * \param size size of data
     * \param headerSize header length including the empty line (= offset of the body)
     */
    Result parse(const char *data, size_t size, uint64_t headerSize);

    /*!
     * moves the result (header + decoded body) to parsedData,
     * valid after parse() has returned ChunkCompleted. In the header,
     * Transfer-Encoding is replaced by the Content-Length of the decoded body.
     */
    void takeParsedData(std::string &parsedData);

private:
    enum State {
        StateChunkSize = 0,     // expecting "<hex>[;extensions]\r\n"
        StateChunkData,         // expecting chunk data followed by "\r\n"
        StateTrailers,          // after last chunk: trailer lines until empty line
        StateCompleted,
        StateError
    };

    State m_state;
    bool m_started;
    size_t m_position;          // parse position in data
    uint64_t m_chunkSize;       // size of the current chunk
    std::string m_parsedData;   // header (without empty line) + decoded body
    size_t m_headerLength;      // length of the header in m_parsedData
    std::string m_lineEnd;      // "\r\n" or "\n", as used in the received header

    void copyHeader(const char *data, size_t headerSize);

    Result parseLine(const char *data, size_t size, size_t &lineEnd, size_t &nextLine);
    static bool findLineEnd(const char *data, size_t size, size_t position, size_t &lineEnd, size_t &nextLine);
    static bool parseChunkSize(const char *line, size_t length, uint64_t &chunkSize);
};

#endif // REMOVE_CHUNK_H
