﻿
#pragma once

#include <experimental/filesystem>
#include <fstream>
#include <map>
#include <memory>

#include "serialization/journal.pb.h"

namespace principia {
namespace journal {

class Player {
 public:
  using PointerMap = std::map<std::uint64_t, void*>;

  explicit Player(std::experimental::filesystem::path const& path);

  // Replays the next message in the journal.  Returns false at end of journal.
  bool Play();

  // Returns the last method that was replayed.
  serialization::Method const& last_method() const;

 private:
  // Reads one message from the stream.  Returns a |nullptr| at end of stream.
  std::unique_ptr<serialization::Method> Read();

  template<typename Profile>
  bool RunIfAppropriate(serialization::Method const& method);

  PointerMap pointer_map_;
  std::ifstream stream_;
  std::unique_ptr<serialization::Method> last_method_;

  friend class PlayerTest;
  friend class RecorderTest;
};

}  // namespace journal
}  // namespace principia

#include "journal/player_body.hpp"
