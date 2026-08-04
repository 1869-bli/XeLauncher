#pragma once
#include <string>

bool httpGet(const std::wstring& url, std::string& out, int timeoutMs = 15000);
bool httpDownloadFile(const std::wstring& url, const std::string& dstUtf8, int timeoutMs = 30000);
