#include <memory>
#include <ts/string.h>
#include <ts/log.h>

_TS_NAMESPACE_USING

_TS_NAMESPACE_BEGIN

static const unsigned char _to_upper[]={
    /*.    .    .    .    .    .    .    .    .    .    .    .    .    .    .    .*/
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
    /*.    .    .    .    .    .    .    .    .    .    .    .    .    .    .    .*/
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
    /*     !    "    #    $    %    &    '    (    )    *    +    ,    -    .    /*/
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,
    /*0    1    2    3    4    5    6    7    8    9    :    ;    <    =    >    ?*/
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
    /*@    A    B    C    D    E    F    G    H    I    J    K    L    M    N    O*/
    0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,
    /*P    Q    R    S    T    U    V    W    X    Y    Z    [    \    ]    ^    _*/
    0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,
    /*`    a    b    c    d    e    f    g    h    i    j    k    l    m    n    o*/
    0x60,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,
    /*p    q    r    s    t    u    v    w    x    y    z    {    |    }    ~    .*/
    0x70,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x7B,0x7C,0x7D,0x7E,0x7F,
    /*.    .    .    .    .    .    .    .    .    .    .    .    .    .    .    .*/
    0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,
    /*.    .    .    .    .    .    .    .    .    .    .    .    .    .    .    .*/
    0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,
    /*.    .    .    .    .    .    .    .    .    .    .    .    .    .    .    .*/
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
    /*.    .    .    .    .    .    .    .    .    .    .    .    .    .    .    .*/
    0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
    /*.    .    .    .    .    .    .    .    .    .    .    .    .    .    .    .*/
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,
    /*.    .    .    .    .    .    .    .    .    .    .    .    .    .    .    .*/
    0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,
    /*.    .    .    .    .    .    .    .    .    .    .    .    .    .    .    .*/
    0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
    /*.    .    .    .    .    .    .    .    .    .    .    .    .    .    .    .*/
    0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF,
};

namespace string {
    char* stristr(char* s, const char* pattern) {
        char *cp = (char *) s;
        char *s1, *s2;
        
        if ( !s || !pattern || !*pattern )
            return nullptr;
        
        while (*cp) {
            s1 = cp;
            s2 = (char *) pattern;
            
            while ( *s1 && *s2 && (_to_upper[(unsigned char)*s1] == _to_upper[(unsigned char)*s2]) ) {
                s1++; s2++;
            }
            
            if (!*s2)
                return(cp);
            
            cp++;
        }
        
        return(nullptr);
    }

    const char* stristr(const char* s, const char* pattern) {
        return stristr(const_cast<char*>(s), pattern);
    }
    
    
    std::string format(const char *fmt, ...) {
        int old_size = (int)strlen(fmt);
        std::shared_ptr<char> buf(new char[old_size]);
        va_list ap;
        
        va_start(ap, fmt);
        int new_size = vsnprintf(buf.get(), old_size, fmt, ap);
        va_end(ap);
        if (new_size < 0)
            return "";
        
        buf.reset(new char[new_size + 1]);
        va_start(ap, fmt);
        new_size = vsnprintf(buf.get(), new_size + 1, fmt, ap);
        va_end(ap);
        if (new_size < 0)
            return "";
        
        buf.get()[new_size] = 0;
        
        return std::string(buf.get());
    }
    
    std::string& format(std::string&s, const char *fmt, ...) {
        int old_size = (int)strlen(fmt);
        s.resize(old_size);
        va_list ap;
        
        va_start(ap, fmt);
        int new_size = vsnprintf(const_cast<char*>(s.c_str()), old_size, fmt, ap);
        va_end(ap);
        if (new_size < 0)
            return s;
        
        s.resize(new_size + 1);
        va_start(ap, fmt);
        new_size = vsnprintf(const_cast<char*>(s.c_str()), new_size + 1, fmt, ap);
        va_end(ap);
        if (new_size < 0)
            return s;
        
        s.resize(new_size);
        
        return s;
    }
    
    std::string& toupper(std::string&s) {
        int sz = (int)s.size();
        char* p = const_cast<char*>(s.c_str());
        while (sz--) {
            *p = _to_upper[*p];
        }
        return s;
    }

    /*to string*/
    std::string tostr(const uint8_t* ptr, int len) {
        std::string s;
        int left = len;
        const uint8_t* p = ptr;
        while (left) {
            if (left >= 16) {
                char text[33];
                sprintf(text, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
                        p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15]
                        );
                text[32] = 0;
                s += text;
                p += 16;
                left -= 16;
            }
            else if(left >= 8) {
                char text[17];
                sprintf(text, "%02X%02X%02X%02X%02X%02X%02X%02X",
                        p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]
                        );
                text[16] = 0;
                s += text;
                p += 8;
                left -= 8;
            }
            else if(left >= 4) {
                char text[9];
                sprintf(text, "%02X%02X%02X%02X",
                        p[0],p[1],p[2],p[3]
                        );
                text[8] = 0;
                s += text;
                p += 4;
                left -= 4;
            }
            else if(left >= 2) {
                char text[5];
                sprintf(text, "%02X%02X",
                        p[0],p[1]
                        );
                text[4] = 0;
                s += text;
                p += 2;
                left -= 2;
            }
            else {
                char text[3];
                sprintf(text, "%02X",
                        p[0]
                        );
                text[2] = 0;
                s += text;
                p += 1;
                left -= 1;
            }
        }
        return s;
    }
    
    std::string dump(const uint8_t* p, int len) {
        std::string s;
        char    line[128];
        
        int lines = 0;
        for(int i = 0; i < len; i+=16,lines++) {
            int batc = ((len - i) > 16) ? 16 : (len - i);
            snprintf(line, sizeof(line) - 1, "\t%04X: ", i);
            int pos = 7, j = 0;
            for(j = 0; j < batc; j++) {
                if(j == 8) {
                    strncpy(&line[pos], "| ", sizeof(line) - pos - 1);
                    pos += 2;
                }
                snprintf(&line[pos], sizeof(line) - pos - 1, "%02X ", (unsigned char)p[i + j]);
                pos += 3;
            }
            for(; j < 16; j++) {
                if(j == 8) {
                    strncpy(&line[pos], "| ", sizeof(line) - pos - 1);
                    pos += 2;
                }
                strncpy(&line[pos], "   ", sizeof(line) - pos - 1);
                pos += 3;
            }
            line[pos++] = ' ';
            char* dot = &line[pos];
            memcpy(&line[pos], &p[i], batc); pos += batc;
            for(int k = 0; k < batc; k++) {
                if(*dot <= 0 || !isprint(*dot)) *dot = '.';
                dot++;
            }
            line[pos++] = '\n';
            line[pos] = 0;
            s += line;
        }
        return s;
    }
    
    const char*  eatdot(const char* path, std::string& eaten) {
        const char* p = path;
        const char* dot = strrchr(p, '.');
        if (dot) {
            eaten.assign(p, (int)(dot - p));
            return dot + 1;
        }
        else {
            eaten = p;
            return nullptr;
        }
    }

};


_TS_NAMESPACE_END
