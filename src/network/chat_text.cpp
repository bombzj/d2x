#include "chat_text.hpp"
#include "core/utf8.hpp"
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#else
#include <iconv.h>
#include <langinfo.h>
#include <locale.h>
#endif

namespace d2x::net {
namespace {
#ifndef _WIN32
std::optional<std::string> convert(std::string_view text,bool encode) {
    // A native Linux client uses the environment's character encoding as its
    // platform ACP equivalent; do not change the process-wide C locale.
    const auto locale=newlocale(LC_CTYPE_MASK,"",nullptr);if(!locale) return {};
    const std::string encoding=nl_langinfo_l(CODESET,locale);freelocale(locale);
    const auto codec=iconv_open(encode?encoding.c_str():"UTF-8",encode?"UTF-8":encoding.c_str());
    if(codec==iconv_t(-1)) return {};
    std::string result(text.size()*4+8,'\0');
    auto *source=const_cast<char *>(text.data());auto *target=result.data();
    size_t input=text.size(),output=result.size();
    const auto converted=iconv(codec,&source,&input,&target,&output);iconv_close(codec);
    if(converted==size_t(-1) || input) return {};
    result.resize(result.size()-output);return result;
}
#endif
}
bool validChatInput(std::string_view text) {
    const auto codes=strictUtf8(text);
    return codes && codes->size()<=255 && std::all_of(codes->begin(),codes->end(),[](uint32_t code) {
        return code>=32 && code!=127 && code<=0xffff;
    });
}
std::optional<std::string> encodeChatText(std::string_view text) {
    if(!validChatInput(text)) return {};
    if(text.empty()) return std::string{};
#ifdef _WIN32
    const auto codes=strictUtf8(text);std::wstring wide;
    for(const auto code:*codes) wide+=wchar_t(code);
    // D2Client 71034..7104D: CP_ACP and flags 0x250, not CP_UTF8.
    const int count=WideCharToMultiByte(CP_ACP,0x250,wide.data(),int(wide.size()),nullptr,0,nullptr,nullptr);
    if(count<=0) return {};
    std::string result(size_t(count),'\0');
    if(!WideCharToMultiByte(CP_ACP,0x250,wide.data(),int(wide.size()),result.data(),count,nullptr,nullptr)) return {};
    return result;
#else
    return convert(text,true);
#endif
}
std::optional<std::string> decodeChatText(std::string_view text,uint8_t language,uint8_t localLanguage) {
    const bool multibyte=language==6 || language==7 || language==9 || language==12;
    std::string result;
    if(!multibyte) {
        for(const auto byte:text) appendUtf8(result,uint8_t(byte));
        return result;
    }
    if(text.empty()) return result;
#ifdef _WIN32
    // D2Client 7B4F0: these four languages use MultiByteToWideChar(ACP,1).
    const int count=MultiByteToWideChar(CP_ACP,1,text.data(),int(text.size()),nullptr,0);
    if(count<=0) return {};
    std::wstring wide(size_t(count),L'\0');
    if(!MultiByteToWideChar(CP_ACP,1,text.data(),int(text.size()),wide.data(),count)) return {};
    for(const auto code:wide) {
        if(code>=0xd800 && code<=0xdfff) return {};
        appendUtf8(result,uint16_t(code));
    }
#else
    const auto decoded=convert(text,false);if(!decoded) return {};result=*decoded;
#endif
    // 7B52E..7B570: CHT/CHS are compatible. Other mismatched native
    // languages can display ASCII only, even when the ACP conversion succeeds.
    const bool chinese=(language==7 || language==12) && (localLanguage==7 || localLanguage==12);
    if(language!=localLanguage && !chinese &&
        std::any_of(result.begin(),result.end(),[](unsigned char c){return c>=128;})) return {};
    return result;
}
}
