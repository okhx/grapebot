#include "util/sha3.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#define CHECK(condition) \
    do { if (!(condition)) return __LINE__; } while (false)

namespace {

std::string digestOf(const std::string& message) {
    return grape::hashing::sha3_512Hex(std::span(
        reinterpret_cast<const std::uint8_t*>(message.data()), message.size()));
}

std::string repeated(char value, std::size_t count) {
    return std::string(count, value);
}

}

int main() {
    CHECK(digestOf("") ==
          "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
          "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26");
    CHECK(digestOf("abc") ==
          "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e"
          "10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0");
    CHECK(digestOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "04a371e84ecfb5b8b77cb48610fca8182dd457ce6f326a0fd3d7ec2f1e91636d"
          "ee691fbe0c985302ba1b0d8dc78c086346b533b49c030d99a27daf1139d6e75e");
    CHECK(digestOf("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                   "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
          "afebb2ef542e6579c50cad06d2e578f9f8dd6881d7dc824d26360feebf18a4fa"
          "73e3261122948efcfd492e74e82e2189ed0fb440d187f382270cb455f21dd185");

    CHECK(digestOf(repeated('a', 71)) ==
          "070faf98d2a8fddf8ed886408744dc06456096c2e045f26f3c7b010530e6bbb3"
          "db535a54d636856f4e0e1e982461cb9a7e8e57ff8895cff1619af9f0e486e28c");
    CHECK(digestOf(repeated('a', 72)) ==
          "a8ae722a78e10cbbc413886c02eb5b369a03f6560084aff566bd597bb7ad8c1c"
          "cd86e81296852359bf2faddb5153c0a7445722987875e74287adac21adebe952");
    CHECK(digestOf(repeated('a', 73)) ==
          "23e6a8815f8201dbbf6a5463be8dcadb1acea9df5f8998954e59ac9565cf6d29"
          "b17aa27a5e8b0fc06343db6122d6e544d27583ddc78504d08203217e7e65b6bd");
    CHECK(digestOf(repeated('a', 143)) ==
          "1dfc536c0ef79e004ec6f18e3b24fd6c4c3076556424ef369e8734312d6594ff"
          "9b92a8f02d2980ab51c191a9cc3cf47d06265e81d306d4098cdf2b6bada1db27");
    CHECK(digestOf(repeated('a', 144)) ==
          "446cd4d7ba19510dcc776b21045bc68d424b5b840e14685e149bb238b5f473c0"
          "356b69e04f0f5785eefce20ff09e678b080d8aac64568c5edf001cd32b2ed7a8");
    CHECK(digestOf(repeated('a', 145)) ==
          "84a7b171615f4f0024b772defd5ea21a536bb1e52306fa7ad412c532ac919f6b"
          "645e412aa7f5d979808c8ae03ca7d159363dcdb179c9b03f908e3b3526cbf4de");

    std::vector<std::uint8_t> everyByte(256);
    for (std::size_t i = 0; i < everyByte.size(); ++i)
        everyByte[i] = static_cast<std::uint8_t>(i);
    CHECK(grape::hashing::sha3_512Hex(everyByte) ==
          "3a843af1f872928f0bbbb513207a1a8e14e3d911269fff521292d07dbd5e2e52"
          "0d6c2634292801184ffa54fd5f1e992ccfdaff8162f5c5f6d1ea79dbcae97e1d");

    CHECK(digestOf(repeated('a', 1000000)) ==
          "3c3a876da14034ab60627c077bb98f7e120a2a5370212dffb3385a18d4f38859"
          "ed311d0a9d5141ce9cc5c66ee689b266a8aa18ace8282a0e0db596c90b0a7b87");

    CHECK(digestOf("abc").size() == 128);

    return 0;
}
