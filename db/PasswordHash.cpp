#include "PasswordHash.h"

#include <iomanip>
#include <random>
#include <sstream>

#include <openssl/rand.h>
#include <openssl/sha.h>

namespace {
std::string bytesToHex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) { oss << std::setw(2) << static_cast<int>(data[i]); }
    return oss.str();
}
std::string randomSalt() {
    unsigned char salt[16];
    if (RAND_bytes(salt, sizeof(salt)) == 1) { return bytesToHex(salt, sizeof(salt)); }
    static thread_local std::mt19937_64 rng(std::random_device{}());
    for (unsigned char& ch : salt) { ch = static_cast<unsigned char>(rng() & 0xff); }
    return bytesToHex(salt, sizeof(salt));
}
std::string sha256Hex(const std::string& input) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    return bytesToHex(digest, sizeof(digest));
}
} // namespace

std::string makePasswordHash(const std::string& password) {
    std::string salt = randomSalt();
    return "sha256$" + salt + "$" + sha256Hex(salt + ":" + password);
}
bool verifyPassword(const std::string& password, const std::string& storedHash) {
    const std::string prefix = "sha256$";
    if (storedHash.rfind(prefix, 0) != 0) { return false; }
    size_t saltStart = prefix.size();
    size_t saltEnd = storedHash.find('$', saltStart);
    if (saltEnd == std::string::npos) { return false; }
    std::string salt = storedHash.substr(saltStart, saltEnd - saltStart);
    std::string expected = prefix + salt + "$" + sha256Hex(salt + ":" + password);
    return expected == storedHash;
}
