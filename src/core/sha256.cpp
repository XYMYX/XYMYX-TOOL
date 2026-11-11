// sha256.cpp - the SHA-256 hash function (FIPS 180-4).
//
// A hash function turns any amount of data into a fixed 256-bit "fingerprint".
// Change one bit of the input and the fingerprint changes completely, which
// is why comparing hashes is a good way to check that a download is intact.
//
// The algorithm in three sentences: the data is cut into 64-byte blocks.
// Eight 32-bit numbers (the "state") start at fixed values. Every block is
// mixed into the state by 64 rounds of bit operations; the final state is
// the hash.

#include "core/sha256.h"

#include <cstring>
#include <fstream>
#include <vector>

namespace {

// One constant per round. They are the first 32 bits of the fractional parts
// of the cube roots of the first 64 prime numbers. Nobody chose them freely,
// which is the point: it shows there is no hidden back door in the numbers.
constexpr std::uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

// Rotate right: bits pushed out on the right come back in on the left.
// Unlike a plain shift, no information is lost.
inline std::uint32_t rotr(std::uint32_t x, int n) {
  return (x >> n) | (x << (32 - n));
}

}

// The starting state: the first 32 bits of the fractional parts of the square
// roots of the first eight primes. Same "nothing up my sleeve" idea as above.
Sha256::Sha256()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
             0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

// Mixes one 64-byte block into the state.
void Sha256::transform(const std::uint8_t* block) {
  // Step 1: the message schedule. The 64 bytes become 16 words, read
  // big-endian (most significant byte first) because the standard says so.
  // Building each word from single bytes makes this work on any CPU.
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
           (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
           static_cast<std::uint32_t>(block[i * 4 + 3]);
  }
  // The 16 words are stretched to 64, one per round. Each new word depends on
  // four earlier ones, so every input bit ends up influencing many rounds.
  for (int i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  // Step 2: 64 rounds on a working copy of the state (a..h).
  std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
  for (int i = 0; i < 64; ++i) {
    const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    // "Choose": for each bit, e decides whether the bit comes from f or g.
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t t1 = h + s1 + ch + kRound[i] + w[i];
    const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    // "Majority": each bit is whatever at least two of a, b, c agree on.
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = s0 + maj;
    // Shift the eight values along by one and feed the new mix in at a and e.
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  // Step 3: add the result onto the old state. All additions wrap around at
  // 32 bits, which is exactly what unsigned arithmetic in C++ does. This
  // addition is what makes the function hard to run backwards.
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

// Accepts data in pieces of any size. Full 64-byte blocks are processed at
// once; a leftover piece waits in buffer_ until the next call completes it.
void Sha256::update(const std::uint8_t* data, std::size_t size) {
  totalBytes_ += size;
  if (buffered_ > 0) {
    const std::size_t take = std::min(size, buffer_.size() - buffered_);
    std::memcpy(buffer_.data() + buffered_, data, take);
    buffered_ += take;
    data += take;
    size -= take;
    if (buffered_ < buffer_.size()) return;
    transform(buffer_.data());
    buffered_ = 0;
  }
  while (size >= 64) {
    transform(data);
    data += 64;
    size -= 64;
  }
  if (size > 0) {
    std::memcpy(buffer_.data(), data, size);
    buffered_ = size;
  }
}

// Finishes the hash. Call it once; the object cannot be reused afterwards.
std::string Sha256::hex() {
  // Padding: the message must end exactly on a block boundary. The standard
  // appends one 0x80 byte, then zeros, then the original length in bits as a
  // 64-bit big-endian number. Including the length means "abc" and "abc"
  // followed by zeros produce different hashes.
  const std::uint64_t bits = totalBytes_ * 8;
  std::uint8_t padding[72] = {0x80};
  // The length takes the last 8 bytes of a block, so the data plus padding
  // must stop at byte 56. If more than 55 bytes are already waiting, there is
  // no room and the padding runs on into one extra block (120 = 64 + 56).
  const std::size_t padLength = (buffered_ < 56) ? (56 - buffered_) : (120 - buffered_);
  std::uint8_t length[8];
  for (int i = 0; i < 8; ++i) length[i] = static_cast<std::uint8_t>(bits >> (56 - i * 8));
  update(padding, padLength);
  update(length, 8);

  // Write the eight state words as 64 hexadecimal characters, 4 bits each.
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (std::uint32_t word : state_) {
    for (int shift = 28; shift >= 0; shift -= 4) out.push_back(digits[(word >> shift) & 0xF]);
  }
  return out;
}

std::string sha256Text(const std::string& text) {
  Sha256 hash;
  hash.update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  return hash.hex();
}

std::optional<std::string> sha256File(const std::filesystem::path& path) {
  // Binary mode is essential. In text mode Windows would translate line
  // endings while reading, and we would hash different bytes than are on disk.
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  Sha256 hash;
  // Read in 1 MB pieces: large enough to be fast, small enough that a huge
  // file never has to fit into memory.
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    // The last read is usually short. gcount() says how much really arrived.
    const std::streamsize got = in.gcount();
    if (got > 0) {
      hash.update(reinterpret_cast<const std::uint8_t*>(buffer.data()),
                  static_cast<std::size_t>(got));
    }
  }
  // Reaching the end sets "eof", which is fine. "bad" means a real read
  // error. Returning a hash of half a file would be worse than returning none.
  if (in.bad()) return std::nullopt;
  return hash.hex();
}
