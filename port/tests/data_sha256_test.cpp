#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "data/sha256.hpp"

namespace {

std::string Hash(const std::string &text) {
    return kzdata::Sha256Hex({reinterpret_cast<const unsigned char *>(text.data()), text.size()});
}

} // namespace

TEST(DataSha256, MatchesPublishedVectors) {
    EXPECT_EQ(Hash(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(Hash("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(Hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(DataSha256, PiecesHashAsTheWhole) {
    std::vector<unsigned char> bytes(1000);
    for (std::size_t i = 0; i < bytes.size(); i++) {
        bytes[i] = static_cast<unsigned char>(i * 7);
    }
    kzdata::Sha256 pieces;
    for (std::size_t at = 0; at < bytes.size(); at += 37) {
        pieces.Update(std::span(bytes).subspan(at, std::min<std::size_t>(37, bytes.size() - at)));
    }
    EXPECT_EQ(pieces.FinishHex(), kzdata::Sha256Hex(bytes));
}
