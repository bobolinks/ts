#include <memory>
#include <ts/json.h>
#include <ts/log.h>

_TS_NAMESPACE_USING

_TS_NAMESPACE_BEGIN

#define isslash(_c_)                (_c_ == '/' || _c_ == '\\')
#define skip_slash(_token_)         while(p < e && isslash(*p)){p++;} _token_ = p;
#define scan_name(_token_, _len_)   skip_slash(_token_); _len_ = 0; while(p < e && !isslash(*p)){p++; _len_++;}

#define isblank(_c_)                (_c_ == '\n' || _c_ == '\r' || _c_ == '\t' || _c_ == ' ')
#define isstring(_c_)               (_c_ == '\'' || _c_ == '\"')
#define issep(_c_)                  (_c_ == ',')

#if defined(_MSC_VER) || defined(ANDROID) || defined(_OS_LINUX_)
#define ishexnumber(_c_)            (_c_ == 'x' || (_c_ >= '0' && _c_ <= '9') || (_c_ >= 'a' && _c_ <= 'f')  || (_c_ >= 'A' && _c_ <= 'F') )
#endif

#define scan_blank(_token_)         while(p < e && isblank(*p)){p++;} _token_ = p;
#define scan_token(_token_)         scan_blank(_token_);
#define scan_id(_token_, _len_)     scan_blank(_token_); _len_ = 0; while(p < e && isalnum(*p)){p++; _len_++;}
#define scan_string(_token_, _len_) scan_blank(_token_); _len_ = 2; {char _ew = *p++; while(p < e && (*p != _ew || *(p-1) == '\\')){p++; _len_++;} p++;}
#define scan_ior(_token_, _len_, _has_dot_, _has_hex)    \
scan_blank(_token_); _len_ = 0; \
{bool _h1 = false, _h2 = false; _has_hex = 0; _has_dot_ = 0; if (*p == '-' || *p == '+') {p++; _len_++;} \
while(p < e && (isdigit(*p) || (_h1 = (*p == '.')) || (_h2 = ishexnumber(*p)))){p++; _len_++; _has_dot_ |= _h1; _has_hex |= _h2; }}

namespace json {
    bool    parserMap(const char* src, int len, std::map<std::string, ts::pie>& values, int& readx);
    bool    parserValue(const char* src, int len, ts::pie& value, int& readx) {
        const char* p = src;
        const char* e = src + ((len != -1) ? len : len = (int)strlen(src));
        const char* tk = nullptr;
        
        while (p < e) {
            scan_token(tk);
            if (p >= e) {
                /*error*/
                log_debug("illegal json source!");
                throw std::invalid_argument("illegal json source!");
            }
            if (*p == '/' && *(p + 1) == '*') { //is commentary
                p += 2;
                const char* pp = strstr(p, "*/");
                if (pp == nullptr) {
                    return false;
                }
                p = pp + 2;
                continue;
            }
            switch (*tk) {
                case '{': { /*map*/
                    int rx = 0;
                    value = std::map<std::string, ts::pie>{};
                    if (parserMap(p, len - (int)(p - src), value.map(), rx) == false) {
                        /*error*/
                        return false;
                    }
                    
                    p += rx;
                    
                    readx = (int)(p - src);
                    
                    return true;
                }break;
                    
                case '[': { /*list*/
                    p++;

                    value = std::vector<ts::pie>{};

                    scan_token(tk);
                    if (*tk == ']') { /*empty list*/
                        p++;
                        readx = (int)(p - src);
                        return true;
                    }
                    
                    while (p < e) {
                        int rx = 0;
                        ts::pie& sub = *value.array().insert(value.array().end(), ts::pie());
                        if (parserValue(p, len - (int)(p - src), sub, rx) == false) {
                            /*error*/
                            return false;
                        }
                        
                        p += rx;
                        
                        scan_token(tk);
                        
                        if (*tk == ']') {
                            p++;
                            break;
                        }
                        else if (*tk != ',') {
                            /*warning*/
                            p++;
                        }
                        else { /*again*/
                            p++;
                            scan_token(tk);
                            if (*tk == ']') {
                                p++;
                                break;
                            }
                        }
                    }
                    
                    readx = (int)(p - src);
                    
                    return true;
                }break;
                    
                default: {
                    const char* val = nullptr;
                    int vallen = 0;
                    
                    if (isstring(*tk)) {
                        scan_string(val, vallen);
                        if (vallen <= 0 || *val != *(val + vallen - 1)) {
                            /*error*/
                            log_debug("illegal string : nearby %16s!", val);
                            throw std::invalid_argument("illegal json source!");
                        }
                        value = std::string(val + 1, vallen - 2);
                        readx = (int)(p - src);
                        return true;
                    }
                    else {
                        bool has_dot = false, has_hex = false;
                        const char* p_save = p;
                        scan_ior(val, vallen, has_dot, has_hex);
                        if (vallen <= 0 || (*p != ' ' && *p != ',' && *p != ']' && *p != '}')) { //deal as string
                            p = p_save;
                            scan_blank(val); vallen = 0;
                            while (*p && *p != ' ' && *p != ',' && *p != ']' && *p != '}') {
                                p++;
                                vallen++;
                            }
                            value = std::string(val, vallen);
                            readx = (int)(p - src);
                            return true;
                        }
                        //is number
                        if (has_dot) {
                            value = atof(val);
                        }
                        else {
                            value = atoll(val);
                        }
                        readx = (int)(p - src);
                        return true;
                    }
                }break;
            }
        }
        
        return false;
    }
    
    bool    parserMap(const char* src, int len, std::map<std::string, ts::pie>& values, int& readx) {
        const char* p = src;
        const char* e = src + ((len != -1) ? len : len = (int)strlen(src));
        const char* tk = nullptr;
        
        scan_token(tk);
        if (p >= e || *tk != '{') {
            /*error*/
            log_debug("illegal json source!");
            throw std::invalid_argument("illegal json source!");
        }
        
        { /*map*/
            p++;
            
            scan_token(tk);
            if (*tk == '}') { /*empty map*/
                p++;
                readx = (int)(p - src);
                return true;
            }
            
            while (p < e) {
                const char* sid = nullptr;
                int sidlen = 0;
                
                scan_blank(sid); sidlen = 0;
                while(p < e && !isblank(*p) && *p != ':'){p++; sidlen++;}
                if (sidlen <= 0) {
                    /*error*/
                    log_debug("illegal id found!");
                    throw std::invalid_argument("illegal json source!");
                }
                
                //remove "
                if (*sid == '\"' && *(sid + sidlen - 1) == '\"') {
                    sid++;
                    sidlen -= 2;
                }
                
                std::string sId(sid, sidlen);
                
                const char* comma = nullptr;
                scan_token(comma);
                
                p++;
                
                if (*comma != ':') {
                    /*error*/
                    log_debug("missing : nearby %16s!", p - 1);
                    throw std::invalid_argument("illegal json source!");
                }
                
                ts::pie& value = values[sId];
                
                int rx = 0;
                if (parserValue(p, len - (int)(p - src), value, rx) == false) {
                    /*error*/
                    return false;
                }
                
                p += rx;
                
                scan_token(tk);
                
                if (*tk == '}') {
                    p++;
                    break;
                }
                else if (*tk != ',') {
                    /*warning*/
                    p++;
                }
                else { /*again*/
                    p++;
                    scan_token(tk);
                    if (*tk == '}') {
                        p++;
                        break;
                    }
                }
            }
            
            readx = (int)(p - src);
        }
        return true;
    }

    bool parse(ts::pie& out, const char* src) {
        int readx = 0;
//        out = std::map<std::string, ts::pie>{};
        return parserValue(src, (int)strlen(src), out, readx);
    }
    
    std::string&    format(const ts::pie& js, std::string& out, bool quot, int align, int indent) {
        static const struct __spaces {
            char v[64] = {};
            __spaces(void) {
                memset(v, ' ', sizeof(v));
            }
        } _spaces;
        int indsize = align * indent;
        if (indsize > 64) indsize = 64;
        
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
            out += "\"";
            out += js.get<std::string>();
            out += "\"";
        }
        else if (idx == typeid(std::vector<ts::pie>)) {
            out += "[";
            if (indsize) {
                out += "\r\n";
            }
            for (auto& it : js.array()) {
                format(it, out, quot, align+1, indent);
                out += ",";
                if (indsize) {
                    out += "\r\n";
                }
            }
            if (js.array().size()) {
                out.replace(out.length() - (indsize ? 3 : 1), indsize ? 3 : 1, "");
            }
            if (indsize) {
                out += "\r\n";
            }
            if (indsize) {
                out += std::string(_spaces.v, indsize);
            }
            out += "]";
        }
        else if (idx == typeid(std::map<std::string, ts::pie>)) {
            if (indsize) {
                out += std::string(_spaces.v, indsize);
                out += "{\r\n";
            }
            else {
                out += "{";
            }
            for (auto& it : js.map()) {
                if (indsize) {
                    out += std::string(_spaces.v, indsize + indent);
                }
                if (quot)out += "\"";
                out += it.first;
                if (quot)out += "\"";
                out += ":";
                if (indsize) out += "\t";
                format(it.second, out, quot, align+1, indent);
                out += ",";
                if (indsize) {
                    out += "\r\n";
                }
            }
            if (js.map().size()) {
                out.replace(out.length() - (indsize ? 3 : 1), indsize ? 3 : 1, "");
            }
            if (indsize) {
                out += "\r\n";
            }
            if (indsize) {
                out += std::string(_spaces.v, indsize);
            }
            out += "}";
        }
        return out;
    }
    
    std::string&    format(const ts::pie& js, std::string& out, bool quot, bool align) {
        return format(js, out, quot, (int)(align?1:0), (int)(align ? 2 : 0));
    }
    
    bool            fromFile(ts::pie& out, const char* file) {
        FILE* fp = fopen(file, "rb");
        if (fp == nullptr) {
            log_error("file[%s] not found!", file);
            return false;
        }
        fseek(fp, SEEK_END, 0);
        long sz = ftell(fp);
        fseek(fp, SEEK_SET, 0);
        std::shared_ptr<char> szb(new char[sz + 2]);
        fread(szb.get(), sz, 1, fp);
        szb.get()[sz] = 0;
        fclose(fp);
        return parse(out, szb.get());
    }
    
    long            toFile(const ts::pie& js, const char* file, bool align) {
        std::string s;
        format(js, s, false, align);
        if (s.size()) {
            FILE* fp = fopen(file, "rb");
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

