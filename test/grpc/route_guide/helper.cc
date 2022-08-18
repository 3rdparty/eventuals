#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "test/grpc/route_guide/route_guide.grpc.pb.h"

////////////////////////////////////////////////////////////////////////

namespace routeguide {

////////////////////////////////////////////////////////////////////////

void GetDbFileContent(const std::string& db_path, std::string& db) {
  std::ifstream db_file(db_path);

  CHECK(db_file.is_open());

  std::stringstream db_stream;
  db_stream << db_file.rdbuf();
  db = db_stream.str();
}

////////////////////////////////////////////////////////////////////////

// A simple parser for the json db file. It requires the db file to have the
// exact form of [{"location": { "latitude": 123, "longitude": 456}, "name":
// "the name can be empty" }, { ... } ... The spaces will be stripped.
class Parser {
 public:
  explicit Parser(const std::string& db)
    : db_(db) {
    // Remove all spaces.
    db_.erase(std::remove_if(db_.begin(), db_.end(), isspace), db_.end());
    CHECK(Match("["));
  }

  bool Finished() {
    return current_ >= db_.size();
  }

  bool TryParseOne(Feature* feature) {
    CHECK(!Finished() && Match("{"));

    CHECK(Match(location_) && Match("{") && Match(latitude_));

    long temp = 0;
    ReadLong(&temp);
    feature->mutable_location()->set_latitude(temp);

    CHECK(Match(",") && Match(longitude_));

    ReadLong(&temp);
    feature->mutable_location()->set_longitude(temp);

    CHECK(Match("},") && Match(name_) && Match("\""));

    size_t name_start = current_;
    while (current_ != db_.size() && db_[current_++] != '"') {
    }

    CHECK_NE(current_, db_.size());

    feature->set_name(db_.substr(name_start, current_ - name_start - 1));

    if (!Match("},")) {
      CHECK_EQ(db_[current_ - 1], ']');
      CHECK_EQ(current_, db_.size());
    }

    return true;
  }

 private:
  bool Match(const std::string& prefix) {
    bool eq = db_.substr(current_, prefix.size()) == prefix;
    current_ += prefix.size();
    return eq;
  }

  void ReadLong(long* l) {
    size_t start = current_;
    while (current_ != db_.size()
           && db_[current_] != ','
           && db_[current_] != '}') {
      current_++;
    }
    // It will throw an exception if fails.
    *l = std::stol(db_.substr(start, current_ - start));
  }

  std::string db_;
  size_t current_ = 0;
  const std::string location_ = "\"location\":";
  const std::string latitude_ = "\"latitude\":";
  const std::string longitude_ = "\"longitude\":";
  const std::string name_ = "\"name\":";
};

////////////////////////////////////////////////////////////////////////

void ParseDb(const std::string& db, std::vector<Feature>* feature_list) {
  feature_list->clear();

  std::string db_content(db);
  Parser parser(db_content);

  Feature feature;
  while (!parser.Finished()) {
    feature_list->push_back(Feature());
    CHECK(parser.TryParseOne(&feature_list->back()));
  }
}

////////////////////////////////////////////////////////////////////////

} // namespace routeguide

////////////////////////////////////////////////////////////////////////
