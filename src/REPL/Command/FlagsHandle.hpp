//
// Copyright (C) 2018-2019 NCC Group
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

#ifndef XENDBG_FLAGSHANDLE_HPP
#define XENDBG_FLAGSHANDLE_HPP

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ArgsHandle.hpp"
#include "Flag.hpp"

namespace xd::repl::cmd {

    class FlagsHandle {
    private:
      using FlagHandle = std::optional<ArgsHandle>;
      using FlagsList = std::vector<std::pair<std::pair<char, std::string>, ArgsHandle>>;

    private:
      template <typename F>
      FlagHandle get_predicate(F pred) const {
        auto found = std::find_if(_flags.begin(), _flags.end(), pred);

        if (found == _flags.end())
          return std::nullopt;

        return found->second;
      }

    public:
      void put(const Flag& flag, ArgsHandle args) {
        auto flag_names = std::make_pair(flag.get_short_name(), flag.get_long_name());
        _flags.push_back(std::make_pair(flag_names, args));
      }

      bool has(char short_name) const {
        return get(short_name).has_value();
      }
      bool has(const std::string& long_name) const {
        return get(long_name).has_value();
      }

      FlagHandle get(char short_name) const {
        return get_predicate([short_name](const auto& f) {
          return f.first.first == short_name;
        });
      }

      FlagHandle get(std::string long_name) const {
        return get_predicate([long_name](const auto& f) {
          return f.first.second == long_name;
        });
      }

    private:
      FlagsList _flags;
    };
}

#endif //XENDBG_FLAGSHANDLE_HPP
