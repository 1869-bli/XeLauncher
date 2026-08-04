#include "net.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <fstream>

#include "json.h"

namespace {

struct ParsedUrl {
    bool ok = false;
    bool https = true;
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring path;  // includes leading '/' and query
};

ParsedUrl parseUrl(const std::wstring& url) {
    ParsedUrl u;
    std::wstring rest = url;
    if (rest.rfind(L"https://", 0) == 0) {
        u.https = true;
        rest = rest.substr(8);
    } else if (rest.rfind(L"http://", 0) == 0) {
        u.https = false;
        rest = rest.substr(7);
    } else {
        return u;
    }
    size_t slash = rest.find(L'/');
    std::wstring authority = slash == std::wstring::npos ? rest : rest.substr(0, slash);
    u.path = slash == std::wstring::npos ? L"/" : rest.substr(slash);
    size_t colon = authority.find(L':');
    if (colon != std::wstring::npos) {
        u.host = authority.substr(0, colon);
        u.port = (INTERNET_PORT)_wtoi(authority.substr(colon + 1).c_str());
    } else {
        u.host = authority;
    }
    if (u.host.empty()) return u;
    if (u.port == 0) u.port = u.https ? 443 : 80;
    u.ok = true;
    return u;
}

}  // namespace

bool httpGet(const std::wstring& url, std::string& out, int timeoutMs) {
    ParsedUrl u = parseUrl(url);
    if (!u.ok) return false;
    out.clear();

    HINTERNET hSession = WinHttpOpen(L"XeLauncher/1.2.1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    HINTERNET hConnect = WinHttpConnect(hSession, u.host.c_str(), u.port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    const wchar_t* verb = L"GET";
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, verb, u.path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            u.https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    WinHttpSetTimeouts(hRequest, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                           0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                WINHTTP_NO_HEADER_INDEX) &&
            status >= 200 && status < 300) {
            std::string data;
            while (true) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, chunk.data(), avail, &read) || read == 0) break;
                data.append(chunk.data(), read);
            }
            out = std::move(data);
            ok = true;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

bool httpDownloadFile(const std::wstring& url, const std::string& dstUtf8, int timeoutMs) {
    std::string data;
    if (!httpGet(url, data, timeoutMs)) return false;
    std::ofstream ofs(dstUtf8, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(data.data(), (std::streamsize)data.size());
    return (bool)ofs;
}
