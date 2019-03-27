#include <memory>
#include <sstream>
#include <iostream>
#include <set>
#include <ts/json.h>
#include <ts/xml.h>
#include <ts/string.h>
#include <ts/log.h>

_TS_NAMESPACE_USING

_TS_NAMESPACE_BEGIN

namespace xml {
#define isstring(_c_)               (_c_ == '\'' || _c_ == '\"')
#if defined(_MSC_VER) || defined(ANDROID) || defined(_OS_LINUX_)
# define ishexnumber(_c_)            (_c_ == 'x' || (_c_ >= '0' && _c_ <= '9') || (_c_ >= 'a' && _c_ <= 'f')  || (_c_ >= 'A' && _c_ <= 'F') )
#endif

    static const std::map<char,char> _unscape_table {
        {'n', '\n'},    // newline
        {'t', '\t'},    // horizontal tab
        {'v', '\v'},    // vertical tab
        {'b', ' '},    // backspace replaced with space
        {'r', '\r'},    // carriage return
        {'f', '\f'},    // form feed
        {'a', '\a'},    // alert
        {'\\', '\\'},    // backslash
        {'?', '\?'},    // question mark
        {'\'', '\''},    // single quote
        {'\"', '\"'},    // double quote
    };

    std::string unscapeString(const std::string& s) {
        std::string sr = s;
        std::string::size_type pos = sr.find('\\');
        while (pos != std::string::npos) {
            const char* p = sr.c_str() + pos + 1;
            if (*p == 0) break;
            auto it = _unscape_table.find(*p);
            if (it != _unscape_table.end()) {
                sr.replace(pos, 2, &it->second, 1);
            }
            pos = sr.find('\\', pos + 1);
        }

        return sr;
    }
    
    bool skip_parantheses(std::string& err, const char*& ptr, int len, int& line) {
        static std::pair<char, char> setParantheses {'<','>'};
        return json::skip_parantheses(err, ptr, len, line, setParantheses);
    }
    
    bool    skip_value(std::string& err, const char*& p, int len, int& line, bool& isnum, bool& hasdot) {
        const char* e = p + len;
        const char* tk = nullptr;
        
        if (isstring(*p)) {
            tk = p;
            {
                char _ew = *p++;
                while(p < e && (*p != _ew || *(p-1) == '\\')) {
                    if (*p == '\n' && *(p-1) == '\\'){line++;}
                    p++;
                }
            }
            if (tk == p || *tk != *p) {
                /*error*/
                ts::string::format(err, "illegal string : nearby %.64s!", p - 2);
                return false;
            }
            p++;
            return true;
        }
        else {
            tk = p;
            bool has_hex = false;
            {
                bool _h1 = false, _h2 = false; has_hex = 0; hasdot = 0; if (*p == '-' || *p == '+') {p++; } \
                while(p < e && (isdigit(*p) || (_h1 = (*p == '.')) || (_h2 = ishexnumber(*p)))){p++; hasdot |= _h1; has_hex |= _h2; }
            }
            if (tk == p || (*p != ' ' && *p != '/' && *p != '>')) {
                //deal as string
                while (p < e && *p != ' ' && *p != '/' && *p != '>') {
                    if (*p == '\n' && *(p-1) == '\\'){line++;}
                    p++;
                }
                if (*p != ' ' && *p != '/' && *p != '>') {
                    /*error*/
                    ts::string::format(err, "illegal string : nearby %.64s!", p - 2);
                    return false;
                }
            }
            return true;
        }
    }
    
    bool    parserElement(const char*& p, int len, ts::pie& element, int& line, std::string& err, std::set<std::string>& tags_filter) {
        const char* src = p;
        const char* e = p + ((len != -1) ? len : len = (int)strlen(src));
        const char* tk = nullptr;
        
        if (!ts::json::skip_unmeaning(err, p, len, line)) {
            return false;
        }
        if (*p == 0) { //empty
            return true;
        }
        else if (*p != '<') {
            /*error*/
            ts::string::format(err, "[%d] unexpected token nearby %.64s...!", line, p);
            return false;
        }
        
        if (element.isMap() == false) {
            element = std::map<std::string, ts::pie>{};
        }
        std::map<std::string, ts::pie>& props = element.map();
        element.map()["childs"] = std::vector<ts::pie>{};
        std::vector<ts::pie>& childs = element.map()["childs"].array();

        p++;

        do {
            if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line)) {
                return false;
            }
            
            tk = p;
            while(p < e && (isalnum(*p) || *p == '-')){p++;}
            
            std::string sTag(tk, (int)(p - tk));
            
            if (sTag.length() == 0) {
                //test is comment ?
                if (strncmp(p, "!--", 3) == 0) {
                    p += 3;
                    while (p < e) {
                        while (p < e && *p != '-') {
                            if (*p == '\n') {
                                line++;
                            }
                            p++;
                        }
                        if (p < e && *(p + 1) == '-' && *(p + 2) == '>') {
                            p += 3;
                            break;
                        }
                        p++;
                    }
                    if (p < e) {
                        continue;
                    }
                }
                ts::string::format(err, "[%d] unexpected token nearby %.64s...!", line, p - 2);
                return false;
            }
            
            props["tag"] = sTag;
            
            if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line)) {
                return false;
            }
            
            if (*p == '/') {//empty tag
                p++;
                if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line) || *p != '>') {
                    return false;
                }
                p++;
                return true;
            }
            
            //parsing properties
            while (p < e) {
                if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line)) {
                    return false;
                }
                
                if (*p == '>') {
                    p++;
                    break;
                }
                
                tk = p;
                while(p < e && (isalnum(*p) || *p == '$' || *p == '#' || *p == '-' || *p == '_' || *p == ':' || *p == '@' || *p == '.')){p++;}

                if (p == tk) {
                    ts::string::format(err, "[%d] unexpected token nearby %.64s...!", line, p - 2);
                    return false;
                }
                std::string sID(tk, (int)(p - tk));
                
                if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line)) {
                    return false;
                }
                
                if (*p != '=') { //value type property
                    props[sID] = "true";
                    props[sID]._flags |= ts::json::flags_is_boolean;
                    continue;
                }
                
                p++;
                
                if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line)) {
                    return false;
                }
                
                const char* pv = p;
                bool isnum = false, hasdot = false;
                
                if (!skip_value(err, p, len - (int)(p - src), line, isnum, hasdot)) {
                    return false;
                }
                
                const char* pe = p;
                if (*pv == '\'' || *pv == '\"') {
                    pv++;
                    pe--;
                }

                std::string sValue(pv, (int)(pe - pv));
                
                if (isnum == false) {
                    props[sID] = sValue;
                }
                else if(hasdot) {
                    props[sID] = std::stod(sValue);
                }
                else {
                    props[sID] = std::stoll(sValue);
                }
                
                if (*p == '/') {//touch the end
                    p++;
                    if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line) || *p != '>') {
                        return false;
                    }
                    p++;
                    return true;
                }
            }
            
            if (strncasecmp(sTag.c_str(), "br", 2) == 0) {
                continue;
            }
            else if (tags_filter.find(sTag) != tags_filter.end()) {
                const char* pscrbe = p;
                while (p < e) {
                    if (*p == '\n') {
                        p++;
                        line++;
                    }
                    else if( *p == '<') { //found <
                        const char* pscrend = p;
                        p++;
                        if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line)) {
                            return false;
                        }
                        if (*p == '/') {
                            p++;
                            if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line)) {
                                return false;
                            }
                            if (strncasecmp(p, sTag.c_str(), sTag.length()) == 0) {//got it
                                p += sTag.length();
                                if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line) || *p != '>') {
                                    ts::string::format(err, "[%d] </script> not found!", line);
                                    return false;
                                }
                                childs.push_back(std::string(pscrbe, (int)(pscrend - pscrbe)));
                                p++;
                                break;
                            }
                        }
                    }
                    else {
                        p++;
                    }
                }
                continue;
            }

            //parser childs
            while (p < e) {
                if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line)) {
                    return false;
                }
                tk = p;
                static std::pair<char, char> term_chars {'<','<'};
                json::skip_until_commit(err, p, len - (int)(p - src), line, term_chars);
                if (tk != p) {
                    childs.push_back(std::string(tk, (int)(p - tk)));
                }
                else if (*p == '<') {
                    p++;
                    if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line)) {
                        return false;
                    }
                    if (*p == '/') { //tag end
                        p++;
                        if (!ts::json::skip_unmeaning(err, p, len - (int)(p - src), line)) {
                            return false;
                        }
                        tk = p;
                        while(p < e && (isalnum(*p) || *p == '-')){p++;}
                        std::string sTagEnd(tk, (int)(p - tk));
                        if (sTag != sTagEnd) {
                            ts::string::format(err, "unexpected tag name %s!", sTagEnd.c_str());
                            return false;
                        }
                        static std::pair<char, char> term_chars_end {'>','>'};
                        json::skip_until_commit(err, p, len - (int)(p - src), line, term_chars_end);
                        p++;
                        return true;
                    }
                    else { //sub
                        p--;
                        ts::pie& sub = *childs.insert(childs.end(), std::map<std::string, ts::pie>{});
                        if (!parserElement(p, len - (int)(p - src), sub, line, err, tags_filter)) {
                            return false;
                        }
                    }
                }
            }
        } while(0);
        return true;
    }
    
    bool parse(ts::pie& out, const char* src, std::string& err, const char* tags_excluding) {
        std::set<std::string> tags_filter;
        if (tags_excluding) {
            std::istringstream f(tags_excluding);
            std::string s;
            while (std::getline(f, s, ',')) {
                tags_filter.insert(s);
            }
        }
        
        out = std::vector<ts::pie>{};
        int line = 1;
        while (*src) {
            std::vector<ts::pie>& childs = out.array();
            ts::pie& sub = *childs.insert(childs.end(), std::map<std::string, ts::pie>{});
            if (!parserElement(src, (int)strlen(src), sub, line, err, tags_filter)) {
                return false;
            }
            if (sub.isMap() && sub.map().size() == 0) {
                childs.pop_back();
            }
        }
        return true;
    }
    
    std::string&    format(const ts::pie& js, std::string& out, bool quot, int indent) {
        static const struct __spaces {
            char v[64] = {};
            __spaces(void) {
                memset(v, ' ', sizeof(v));
            }
        } _spaces;
        if (indent > 64) indent = 64;
        
        std::type_index idx = js.type();
        if (idx == typeid(int64_t)) {
            char zb[64];
            snprintf(zb, sizeof(zb), "%lld", js.get<int64_t>());
            out += zb;
        }
        else if (idx == typeid(double)) {
            char zb[64];
            snprintf(zb, sizeof(zb), "%f", js.get<double>());
            out += zb;
        }
        else if (idx == typeid(std::string)) {
            if (js._flags & json::flags_is_jsfunction) {
                out += js.get<std::string>();
            }
            else {
                if (quot) out += "\"";
                out += unscapeString(js.get<std::string>());
                if (quot) out += "\"";
            }
        }
        else if (idx == typeid(std::vector<ts::pie>)) {
            for (auto& it : js.array()) {
                format(it, out, quot, indent);
            }
        }
        else if (idx == typeid(std::map<std::string, ts::pie>)) {
            if (out.size() && out.at(out.length() - 1) != '\n') {
                out += "\r\n";
            }
            out += std::string(_spaces.v, indent);
            out += "<";
            std::string sTag = "";
            auto ittag = js.map().find("tag");
            if (ittag != js.map().end()) {
                sTag = ittag->second.get<std::string>();
                out += sTag;
            }
            for (auto& it : js.map()) {
                if (it.first == "tag" || it.first == "childs" || it.second.isMap() || it.second.isArray()) {
                    continue;
                }
                out += " ";
                out += it.first;
                if (it.second.isString() && (it.second._flags & json::flags_is_boolean) && it.second.get<std::string>() == "true") {
                    continue;
                }
                out += "=";
                format(it.second, out, true, 0);
            }
            auto itchilds = js.map().find("childs");
            if (itchilds == js.map().end() || itchilds->second.array().size() == 0) {
                out += "></" + sTag + ">\r\n";
                return out;
            }
            out += ">";
            format(itchilds->second, out, quot, indent + 1);
            if (out.at(out.length() - 1) == '\n') {
                out += std::string(_spaces.v, indent);
            }
            out += "</";
            if (ittag != js.map().end()) {
                out += sTag;
            }
            out += ">\r\n";
        }
        return out;
    }
    
    std::string&    format(const ts::pie& js, std::string& out) {
        return format(js, out, false, 0);
    }
    
    bool            fromFile(ts::pie& out, const char* file, std::string& err, const char* tags_excluding) {
        FILE* fp = fopen(file, "rb");
        if (fp == nullptr) {
            log_error("file[%s] not found!", file);
            return false;
        }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        std::shared_ptr<char> szb(new char[sz + 2]);
        fread(szb.get(), sz, 1, fp);
        szb.get()[sz] = 0;
        fclose(fp);
        return parse(out, szb.get(), err, tags_excluding);
    }
    
    long            toFile(const ts::pie& js, const char* file) {
        std::string s;
        format(js, s);
        if (s.size()) {
            FILE* fp = fopen(file, "wb");
            if (fp == nullptr) {
                log_warning("can't open file[%s]!", file);
            }
            else {
                fwrite(s.c_str(), s.size(), 1, fp);
                fclose(fp);
            }
        }
        return (long)s.size();
    }
};

_TS_NAMESPACE_END
