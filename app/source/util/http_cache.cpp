#include "util/http_cache.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

#include <curl/curl.h>
#include "Utils.hpp"

namespace util
{
namespace
{

std::string metaPathFor(const std::string& destPath)
{
    return destPath + ".meta";
}

std::string parentDirOf(const std::string& path)
{
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return "";
    return path.substr(0, pos);
}

void loadMeta(const std::string& destPath, std::string& etag, std::string& lastModified)
{
    std::ifstream meta(metaPathFor(destPath));
    if (!meta)
        return;
    std::string line;
    while (std::getline(meta, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        if (key == "etag")
            etag = value;
        else if (key == "last_modified")
            lastModified = value;
    }
}

void saveMeta(const std::string& destPath, const std::string& etag, const std::string& lastModified)
{
    std::ofstream meta(metaPathFor(destPath), std::ios::binary | std::ios::trunc);
    if (!meta)
        return;
    meta << "etag=" << etag << "\n";
    meta << "last_modified=" << lastModified << "\n";
}

struct HeaderState
{
    std::string etag;
    std::string lastModified;
};

static std::string trimHeaderValue(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static bool startsWithNoCase(const std::string& s, const char* prefix, size_t len)
{
    if (s.size() < len)
        return false;
    for (size_t i = 0; i < len; i++)
    {
        if (std::tolower((unsigned char)s[i]) != prefix[i])
            return false;
    }
    return true;
}

static size_t headerCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t realsize = size * nmemb;
    auto* state = static_cast<HeaderState*>(userdata);
    std::string line(ptr, realsize);
    if (startsWithNoCase(line, "etag:", 5))
        state->etag = trimHeaderValue(line.substr(5));
    else if (startsWithNoCase(line, "last-modified:", 14))
        state->lastModified = trimHeaderValue(line.substr(14));
    return realsize;
}

static size_t fileWriteCallback(void* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* fp = static_cast<FILE*>(userdata);
    return fwrite(ptr, size, nmemb, fp);
}

struct XferState
{
    std::function<void(double)> cb;
};

#if LIBCURL_VERSION_NUM >= 0x072000
static int xferCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t)
#else
static int xferCallback(void* clientp, double dltotal, double dlnow, double, double)
#endif
{
    auto* state = static_cast<XferState*>(clientp);
    if (!state || !state->cb)
        return 0;
    if (dltotal <= 0)
        return 0;
    double p = (double)dlnow / (double)dltotal;
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;
    state->cb(p);
    return 0;
}

} // namespace

bool downloadCached(const std::string& url, const std::string& destPath,
    bool* outNotModified, const std::function<void(double)>& progressCb)
{
    if (outNotModified)
        *outNotModified = false;
    if (url.empty() || destPath.empty())
        return false;

    std::string parent = parentDirOf(destPath);
    if (!parent.empty())
        mkpath(parent);

    std::string etag, lastModified;
    loadMeta(destPath, etag, lastModified);

    CURL* c = curl_easy_init();
    if (!c)
        return false;

    curl_easy_setopt(c, CURLOPT_CAINFO, tlsCaBundlePath().c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "hpak/1.0.0");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 15L);
    // Ask for compressed transfers (repo.json is text); libcurl handles
    // decompression transparently. No effect on already-compressed files.
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");

    struct curl_slist* reqHeaders = nullptr;
    std::string ifNoneMatch, ifModifiedSince;
    if (!etag.empty())
    {
        ifNoneMatch = "If-None-Match: " + etag;
        reqHeaders = curl_slist_append(reqHeaders, ifNoneMatch.c_str());
    }
    if (!lastModified.empty())
    {
        ifModifiedSince = "If-Modified-Since: " + lastModified;
        reqHeaders = curl_slist_append(reqHeaders, ifModifiedSince.c_str());
    }
    if (reqHeaders)
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, reqHeaders);

    HeaderState respHeaders;
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &respHeaders);

    XferState xfer{progressCb};
    if (progressCb)
    {
#if LIBCURL_VERSION_NUM >= 0x072000
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xferCallback);
#else
        curl_easy_setopt(c, CURLOPT_PROGRESSFUNCTION, xferCallback);
#endif
        curl_easy_setopt(c, CURLOPT_PROGRESSDATA, &xfer);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    }
    else
    {
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);
    }

    // Write to a temp file first so failures never corrupt the cached body.
    std::string partPath = destPath + ".part";
    FILE* fp = fopen(partPath.c_str(), "wb");
    if (!fp)
    {
        curl_slist_free_all(reqHeaders);
        curl_easy_cleanup(c);
        return false;
    }
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fileWriteCallback);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, fp);

    CURLcode res = curl_easy_perform(c);
    long code = 0;
    if (res == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    fclose(fp);

    curl_slist_free_all(reqHeaders);
    curl_easy_cleanup(c);

    if (res != CURLE_OK)
    {
        std::remove(partPath.c_str());
        return false;
    }
    if (code == 304)
    {
        // Not modified: keep the cached body (and its meta) as-is.
        std::remove(partPath.c_str());
        if (outNotModified)
            *outNotModified = true;
        return true;
    }
    if (code < 200 || code >= 300)
    {
        std::remove(partPath.c_str());
        return false;
    }

    // Atomic publish of the new body + metadata.
    std::remove(destPath.c_str());
    if (std::rename(partPath.c_str(), destPath.c_str()) != 0)
    {
        std::remove(partPath.c_str());
        return false;
    }
    saveMeta(destPath, respHeaders.etag, respHeaders.lastModified);
    return true;
}

} // namespace util
