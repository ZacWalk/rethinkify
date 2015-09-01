
class aes256
{
private:
    uint8_t key[32];
    uint8_t enckey[32];
    uint8_t deckey[32];

public:
    aes256(uint8_t *key);
    aes256(const std::vector<unsigned char> &key);
    ~aes256();

    void encrypt_ecb(uint8_t * /* plaintext */);
    void decrypt_ecb(uint8_t * /* cipertext */);

};

class sha256
{
private:

    uint32_t total[2];
    uint32_t state[8];
    uint8_t buffer[64];

    void process(const uint8_t data[64]);

public:

    sha256();
    void update(const uint8_t *input, uint32_t length);
    void finish(uint8_t digest[32]);

};

inline std::wstring to_base64(unsigned char const* bytes_to_encode, unsigned int in_len) 
{
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::wstring ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';

    }

    return ret;

}

inline std::wstring to_hex(const unsigned char *src, long len)
{
    static wchar_t  digits [] = L"0123456789abcdef";
    long n = 0, sn = 0;
    std::wstring result;

    while (sn < len)
    {
        auto ch = src[sn++];
        result += digits[(ch & 0xf0) >> 4];
        result += digits[ch & 0x0f];
    }

    return result;
}

inline std::wstring to_hex(const std::vector<unsigned char> &src)
{
    static wchar_t  digits [] = L"0123456789abcdef";
    std::wstring result;

    for (auto ch : src)
    {
        result += digits[(ch & 0xf0) >> 4];
        result += digits[ch & 0x0f];
    }

    return result;
}

inline int char_to_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;

}

inline std::vector<unsigned char> hex_to_data(const wchar_t *hex)
{
    std::vector<unsigned char> retVal;

    auto len = wcslen(hex);
    auto highPart = ((len % 2) == 0);

    if (!highPart)
        retVal.push_back(0);

    auto c = hex;

    while(*c)
    {
        if (highPart)
            retVal.push_back(0x10 * char_to_hex(*c));
        else
            retVal.back() += char_to_hex(*c);

        highPart = !highPart;
        c++;
    }

    return retVal;
}

inline void calc_sha256(char *str, unsigned char output[32])
{
    sha256 h;
    h.update((const uint8_t*) str, (long) strlen(str));
    h.finish(output);
}

