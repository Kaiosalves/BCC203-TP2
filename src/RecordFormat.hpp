#pragma once

#include <string>

#include "Item.hpp"

namespace RecordFormat {

Item ParseLine(std::string line);
std::string FormatProvaoLine(const Item &item);
std::string FormatDisplayLine(const Item &item);

}  // namespace RecordFormat
