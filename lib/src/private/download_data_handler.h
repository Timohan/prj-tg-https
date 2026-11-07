/*!
 * \file
 * \brief file download_data_handler.h
 *
 * Handles the downloaded data
 *
 * Copyright of Timo Hannukkala. All rights reserved.
 *
 * \author Timo Hannukkala <timohannukkala@hotmail.com>
 */

#ifndef DOWNLOAD_DATA_HANDLER_H
#define DOWNLOAD_DATA_HANDLER_H

#include <string>
#include <cstdint>
#include <cstddef>

/*!
 * \brief DownloadDataHandler
 *
 * Buffer for the received response; parses the status code and header fields.
 */
class DownloadDataHandler
{
public:
    enum Chunked {
        ContentIsChunked = 0,
        ContentIsNotChunked,
        ContentIsNotFullyReceived
    };

    DownloadDataHandler();
    ~DownloadDataHandler();
    // owns a malloc'd buffer: copying would free it twice
    DownloadDataHandler(const DownloadDataHandler &) = delete;
    DownloadDataHandler &operator=(const DownloadDataHandler &) = delete;
    // returns false if the buffer could not be grown (out of memory), data is not added then
    bool addData(const char *data, const size_t size);
    // removes the first size bytes (e.g. an interim 1xx response) and parses the status line again
    void discardFront(size_t size);
    const char* getData() const;
    size_t getDataSize() const;
    std::string getDataString() const;

    // header length including the terminating empty line, i.e. offset where
    // the body starts. __UINT64_MAX__ if the header end is not received yet.
    uint64_t getHeaderLength() const;
    static uint64_t findHeaderLength(const char *data, size_t size);
    // finds header field name (case-insensitive) in data[0, headerSize), value without surrounding whitespace
    static bool findHeaderValue(const char *data, size_t size, uint64_t headerSize, const char *name, std::string &value);
    uint64_t getContentLength(const uint64_t &headerSize) const;
    Chunked isChunked(const uint64_t &headerSize) const;
    int getStatusCode() const;
private:
    char *m_data;
    size_t m_dataAllocSize;
    size_t m_addedSize;
    bool m_triedSetCorrectSize;
    int m_statusCode;   // -1 until a valid status line has been received
    bool m_statusLineParsed;

    void parseStatusLine();

    bool getHeaderValue(const char *name, uint64_t headerSize, std::string &value) const;
    size_t getExpectedTotalSize() const;
    bool reserve(size_t neededSize);
};

#endif // DOWNLOAD_DATA_HANDLER_H
