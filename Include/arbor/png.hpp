#ifndef ARBOR_PNG_HPP
#define ARBOR_PNG_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace arbor {

std::vector<std::uint8_t> encode_png(const std::uint8_t* rgba, int width, int height);
bool write_png(const std::string& path, const std::uint8_t* rgba, int width, int height);

} // namespace arbor

#endif // ARBOR_PNG_HPP
